'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { DependencyCoordinator, loadConfiguration, poll, runCommand } = require('./dependencyCoordinator');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const lock = require('./services.lock.json');
const { reportGroups, writeReports } = require('./serviceReports');
const { verifyDependencies } = require('./serviceRuntime');
const { contractStep, caseDiagnostic } = require('./dependencyCoordinator');
const { validateRelocated, validateLibrarySource } = require('./messageRuntime');

test('business substeps retain only fixed stages and numeric response diagnostics', /** 验证诊断定位失败子步骤但不泄漏原始响应、正文或凭据。 */ async () => {
    for (const [error, category] of [
        [Object.assign(new Error('private payload'), { name: 'TimeoutError' }), 'deadline'],
        [new assert.AssertionError({ actual: 1001, expected: 0 }), 'response-1001'],
        [new assert.AssertionError({ actual: 'private payload', expected: 0 }), 'assertion'],
        [new Error('password=private'), 'operation-failed']
    ]) {
        await assert.rejects(contractStep('client-create', /** 注入不应进入报告的原始错误。 */ async () => { throw error; }),
            /** 核对经过脱敏的阶段与分类。 */ failure => {
                assert.deepEqual(caseDiagnostic(failure), { stage: 'client-create', category });
                assert.doesNotMatch(failure.message, /private/);
                return true;
            });
    }
    await assert.rejects(contractStep('private-user', /** 不应执行非法阶段的回调。 */ async () => {}), /Unknown/);
    assert.equal(await contractStep('gate-login', /** 验证正常结果原样返回。 */ async () => 42), 42);
});

test('client libraries may come from Qt and vcpkg but not adjacent or unlisted roots', /** 验证客户端运行库仅允许显式 Qt/vcpkg 安装树，不接受相邻同名前缀目录。 */ () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-runtime-roots-'));
    try {
        const roots = ['qt', 'vcpkg', 'qt-other'].map(/** 创建隔离的依赖来源夹具目录。 */ name => {
            const folder = path.join(temporary, name);
            fs.mkdirSync(folder);
            fs.writeFileSync(path.join(folder, 'library.so'), 'fixture');
            return fs.realpathSync(folder);
        });
        const allowed = roots.slice(0, 2);
        for (const root of allowed) assert.equal(validateLibrarySource(path.join(root, 'library.so'), allowed),
            path.join(root, 'library.so'));
        assert.throws(/** 未列入安装树的文件不能随客户端打包。 */ () =>
            validateLibrarySource(path.join(roots[2], 'library.so'), allowed), /outside locked/);
        assert.throws(/** 单独指定 Qt 时不能隐式接受 vcpkg 来源。 */ () =>
            validateLibrarySource(path.join(roots[1], 'library.so'), [roots[0]]), /outside locked/);
    } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
});

test('service diagnostics preserve safe MySQL categories without raw query or credentials', /** 验证服务诊断仅保留白名单分类，任意 SQL 或敏感错误不得进入证据。 */ () => {
    const { caseDiagnostic } = require('./dependencyCoordinator');
    assert.deepEqual(caseDiagnostic(new Error('MysqlDeadlineExceeded')),
        { stage: 'service-case', category: 'MysqlDeadlineExceeded' });
    assert.deepEqual(caseDiagnostic(new Error('MysqlSessionUnavailable')),
        { stage: 'service-case', category: 'MysqlSessionUnavailable' });
    assert.deepEqual(caseDiagnostic(new Error('MysqlError:1064')),
        { stage: 'service-case', category: 'MysqlError:1064' });
    assert.equal(caseDiagnostic(new Error('MysqlError:1064 SELECT secret')), undefined);
    assert.equal(caseDiagnostic(new Error('password=not-for-evidence')), undefined);
});

test('message runtime checks the complete app-local dependency manifest', /** 验证迁移后的动态依赖与清单精确一致，拒绝缺失、重复及越界库路径。 */ () => {
    const bundle = '/tmp/owned-message';
    const system = 'libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x1)\n';
    const local = `libssl.so.3 => ${bundle}/libssl.so.3 (0x2)\n`;
    assert.doesNotThrow(/** 接受系统库与清单内包本地库的组合。 */ () => validateRelocated(system + local, bundle, ['libssl.so.3']));
    assert.doesNotThrow(/** 接受仅依赖系统库的空清单。 */ () => validateRelocated(system, bundle, []));
    assert.throws(/** 拒绝清单声明但实际缺失的库。 */ () => validateRelocated(system, bundle, ['libssl.so.3']));
    assert.throws(/** 拒绝实际存在但清单未声明的库。 */ () => validateRelocated(system + local, bundle, []));
    assert.throws(/** 拒绝含父目录穿越的库名称。 */ () => validateRelocated(system + local, bundle, ['../libssl.so.3']));
    assert.throws(/** 拒绝清单中重复的库条目。 */ () => validateRelocated(system + local, bundle, ['libssl.so.3', 'libssl.so.3']));
    assert.throws(/** 拒绝仍解析到构建依赖目录的库。 */ () => validateRelocated(system + local.replace(bundle, '/build/vcpkg/lib'), bundle, ['libssl.so.3']));
    assert.throws(/** 拒绝 ldd 报告未找到的库。 */ () => validateRelocated('libssl.so.3 => not found', bundle, ['libssl.so.3']));
});

test('schema selection requires all migration cases independently of infrastructure', /** 验证迁移和消息报告必须包含各自完整用例，不能被基础服务报告代替。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-migration-reports-'));
    try {
        const cases = Array.from({ length: 12 }, /** 生成完整基础服务用例的合成记录。 */ (_, index) => ({
            id: `T10-SVC-${String(index + 1).padStart(2, '0')}`, name: 'fixture', pass: true
        }));
        writeReports(root, '3C-03', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_migration.xml'), 'utf8'), /<failure /);
        cases.push(...Array.from({ length: 12 }, /** 生成完整 schema 迁移用例的合成记录。 */ (_, index) => ({
            id: `T10-MIG-${String(index + 1).padStart(2, '0')}`, name: 'migration', pass: true
        })));
        writeReports(root, '3C-03', cases);
        assert.doesNotMatch(fs.readFileSync(path.join(root, 'linux_migration.xml'), 'utf8'), /<failure /);
        writeReports(root, '3C-05', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_message.xml'), 'utf8'), /<failure /);
        cases.push(...Array.from({ length: 20 }, /** 生成完整消息持久化用例的合成记录。 */ (_, index) => ({
            id: `T10-MSG-${String(index + 1).padStart(2, '0')}`, name: 'message', pass: true
        })));
        writeReports(root, '3C-05', cases);
        assert.doesNotMatch(fs.readFileSync(path.join(root, 'linux_message.xml'), 'utf8'), /<failure /);
        cases.at(-1).pass = false;
        writeReports(root, '3C-05', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_message.xml'), 'utf8'), /<failure /);
        assert.equal(reportGroups('3C-data-adapters').length, 6);
    } finally { fs.rmSync(root, { recursive: true, force: true }); }
});

test('service artifacts require bundled hiredis and reject build-tree or missing dependencies', /** 验证服务运行依赖的系统白名单与显式包内 hiredis 约束。 */ () => {
    const root = '/tmp/service-launcher';
    const system = 'libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x1)\n';
    const bundled = `libhiredis.so.1 => ${root}/libhiredis.so.1 (0x2)`;
    assert.doesNotThrow(/** 接受仅包含允许系统库的服务依赖。 */ () => verifyDependencies(system, root));
    assert.doesNotThrow(/** 显式要求 hiredis 时接受所属包内的库。 */ () => verifyDependencies(system + bundled, root, true));
    assert.throws(/** 未允许 hiredis 时拒绝额外非系统依赖。 */ () => verifyDependencies(system + bundled, root));
    for (const wrong of [
        'libhiredis.so.1 => /work/.ci/vcpkg_installed/lib/libhiredis.so.1 (0x2)',
        'libhiredis.so.1 => /tmp/service-launcher-other/libhiredis.so.1 (0x2)',
        'libhiredis.so.1 => not found',
        'libunexpected.so.1 => /lib/x86_64-linux-gnu/libunexpected.so.1 (0x2)',
        'libhiredis.so.1 => /lib/x86_64-linux-gnu/libhiredis.so.1 (0x2)'
    ]) assert.throws(/** 逐项拒绝外部、缺失或未允许的动态库。 */ () => verifyDependencies(system + wrong, root, true));
    assert.throws(/** 显式要求 hiredis 时拒绝缺失依赖。 */ () => verifyDependencies(system, root, true));
});

test('adapter reports are separate and missing selected cases fail closed', /** 验证报告选择器、缺项和失败状态如实保留。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-reports-'));
    try {
        const cases = Array.from({ length: 12 }, /** 生成固定数量的基础服务合成通过记录。 */ (_, index) => ({
            id: `T10-SVC-${String(index + 1).padStart(2, '0')}`, name: 'bounded contract', pass: true, seconds: 0
        }));
        writeReports(root, '3C-06', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_services.xml'), 'utf8'), /tests="12" failures="0"/);
        assert.match(fs.readFileSync(path.join(root, 'varify_smtp.xml'), 'utf8'), /<failure /);
        assert.equal(reportGroups('3C-adapters').length, 4);
        assert.throws(/** 验证未知服务报告选择器被拒绝。 */ () => reportGroups('unexpected'), /selector/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('outer cleanup cannot pass when a required adapter report is absent or failed', /** 验证最终证据入口不能用基础报告掩盖缺失的适配器报告。 */ async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-reports-'));
    try {
        fs.writeFileSync(path.join(root, 'teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'process-teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'service-endpoints.json'), '{}');
        fs.writeFileSync(path.join(root, 'linux_services.xml'), '<testsuite tests="12" failures="0"/>');
        await assert.rejects(runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false', CHAT_SERVICE_SELECTOR: '3C-06' }
        }), /command failed/);
        assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'teardown.json'), 'utf8')).complete, false);
        fs.writeFileSync(path.join(root, 'teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'linux_services.xml'), '<testsuite tests="12" failures="0"/>');
        fs.writeFileSync(path.join(root, 'varify_smtp.xml'), '<testsuite tests="1" failures="1"><testcase><failure/></testcase></testsuite>');
        await assert.rejects(runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false', CHAT_SERVICE_SELECTOR: '3C-06' }
        }), /command failed/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

/** 创建具备可变重启端口的容器替身，验证精确所有权和端点刷新。 */
function lifecycleFixture() {
    const config = { host: '127.0.0.1', ids: { mailpit: 'c'.repeat(64), redis: 'a'.repeat(64) },
        ports: { redis: 32100, mysql: 32101, smtp: 32102, mailpit: 32103 },
        prefix: 'owned:', recipient: 'owned@example.invalid' };
    const containers = {};
    for (const service of ['mailpit', 'redis']) {
        containers[service] = { Id: config.ids[service], Config: { Image: lock.services[service].image },
            State: { Running: true }, NetworkSettings: { Ports: Object.fromEntries(
                lock.services[service].ports.map(/** 为锁文件端口生成预期 loopback 映射。 */ (port) => [`${port}/tcp`, [{ HostIp: '127.0.0.1',
                    HostPort: String(config.ports[port === 1025 ? 'smtp' : service]) }]])) } };
    }
    const coordinator = new DependencyCoordinator(config, { Redis: /** 提供无需联网的 Redis 类型占位，当前夹具只验证容器生命周期。 */ class {}, nodemailer: {},
        runCommand: /** 模拟精确 ID 的 Docker inspect、启动和停止，并在重启后改变端口。 */ async (command, args) => {
            assert.equal(command, 'docker');
            const service = Object.keys(config.ids).find(/** 按精确容器 ID 找到被操作的所属服务。 */ (name) => config.ids[name] === args.at(-1));
            assert.ok(service, 'only exact owned ID may be addressed');
            if (args[0] === 'inspect') return JSON.stringify([containers[service]]);
            assert.ok(['start', 'stop'].includes(args[0]));
            containers[service].State.Running = args[0] === 'start';
            if (args[0] === 'start') {
                for (const bindings of Object.values(containers[service].NetworkSettings.Ports)) {
                    bindings[0].HostPort = String(Number(bindings[0].HostPort) + 100);
                }
            }
            return '';
        } });
    return { coordinator, config, containers };
}

test('restart refreshes both Mailpit ports and Redis port in the shared configuration', /** 验证已验证容器重启后原位刷新共享端口，SMTP 使用新端点。 */ async () => {
    const { coordinator, config } = lifecycleFixture();
    const ports = config.ports;
    for (const service of ['mailpit', 'redis']) {
        await coordinator.inspect(service);
        await coordinator.lifecycle(service, 'stop');
        await coordinator.lifecycle(service, 'start');
    }
    assert.equal(coordinator.config, config);
    assert.equal(config.ports, ports);
    assert.deepEqual(ports, { redis: 32200, mysql: 32101, smtp: 32202, mailpit: 32203 });
    let smtpPort;
    coordinator.nodemailer = { createTransport: /** 捕获 SMTP 客户端实际使用的端口。 */ (options) => { smtpPort = options.port; return {}; } };
    coordinator.smtp();
    assert.equal(smtpPort, ports.smtp);
});

test('initial ownership still requires the workflow port and rejects unverified restart', /** 验证初次端口不匹配时不能取得容器所有权，也不能刷新或启动。 */ async () => {
    const { coordinator, config, containers } = lifecycleFixture();
    containers.mailpit.NetworkSettings.Ports['8025/tcp'][0].HostPort = '32203';
    await assert.rejects(coordinator.inspect('mailpit'));
    assert.equal(coordinator.owned.has('mailpit'), false);
    await assert.rejects(coordinator.lifecycle('mailpit', 'start'), /unverified/);
    await assert.rejects(coordinator.inspect('mailpit', true), /unverified/);
    assert.equal(config.ports.mailpit, 32103);
});

test('restart rejects invalid identity and bindings without publishing partial ports', /** 验证重启后的身份、镜像和端口破坏均被拒绝且共享配置不被部分更新。 */ async () => {
    const corruptions = [
        /** 注入错误容器 ID。 */ (container) => { container.Id = 'd'.repeat(64); },
        /** 注入不属于锁文件的镜像。 */ (container) => { container.Config.Image = 'unowned-image'; },
        /** 注入容器未运行状态。 */ (container) => { container.State.Running = false; },
        /** 移除必须存在的端口绑定。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'] = []; },
        /** 把绑定地址改为非 loopback 地址。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostIp = '0.0.0.0'; },
        /** 注入超出范围的端口。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '65536'; },
        /** 注入禁止的零端口。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '0'; },
        /** 注入带尾随字符的伪数字端口。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '32203junk'; },
        /** 注入与现有依赖冲突的端口。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'][0].HostPort = '32100'; },
        /** 注入多重绑定，使单一端点约束失败。 */ (container) => { container.NetworkSettings.Ports['8025/tcp'].push({ HostIp: '127.0.0.1', HostPort: '32204' }); }
    ];
    for (const corrupt of corruptions) {
        const { coordinator, config } = lifecycleFixture();
        await coordinator.inspect('mailpit');
        await coordinator.lifecycle('mailpit', 'stop');
        const before = { ...config.ports };
        const command = coordinator.runCommand;
        coordinator.runCommand = /** 在 inspect 返回时注入指定破坏，其他命令保持夹具行为。 */ async (...args) => {
            const result = await command(...args);
            if (args[1][0] !== 'inspect') return result;
            const [container] = JSON.parse(result);
            corrupt(container);
            return JSON.stringify([container]);
        };
        await assert.rejects(coordinator.lifecycle('mailpit', 'start'), /** 核对重启端点校验失败以稳定阶段和分类传播。 */ (error) => {
            assert.deepEqual(error.diagnostic, { stage: 'mailpit-restart-endpoints', category: 'ERR_ASSERTION' });
            return true;
        });
        assert.deepEqual(config.ports, before);
    }
});

test('refreshed Mailpit endpoint drives real HTTP health and cleanup requests', { timeout: 5000 }, /** 通过真实 HTTP 验证 Mailpit 重启后健康检查与清理使用刷新端点。 */ async (t) => {
    const requests = [];
    let messages = [{ ID: 'owned-message' }];
    const server = require('node:http').createServer(/** 模拟 readyz、邮件查询和定向删除，并记录实际请求。 */ (request, response) => {
        requests.push({ method: request.method, url: request.url });
        if (request.url === '/readyz') { response.end('ready'); return; }
        if (request.method === 'DELETE') { messages = []; response.end(); return; }
        response.setHeader('Content-Type', 'application/json');
        response.end(JSON.stringify({ messages }));
    });
    await new Promise(/** 启动本用例 Mailpit HTTP 替身。 */ (resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(/** 测试结束时关闭全部连接及所属 HTTP 监听器。 */ () => new Promise(/** 主动关闭 HTTP 连接并等待监听器退出。 */ (resolve) => { server.closeAllConnections(); server.close(resolve); }));
    const { coordinator, config, containers } = lifecycleFixture();
    const oldPorts = config.ports;
    await coordinator.inspect('mailpit');
    await coordinator.lifecycle('mailpit', 'stop');
    const command = coordinator.runCommand;
    coordinator.runCommand = /** 模拟重启后 Mailpit 映射到真实测试 HTTP 端口。 */ async (...args) => {
        const result = await command(...args);
        if (args[1][0] === 'start') {
            containers.mailpit.NetworkSettings.Ports['8025/tcp'][0].HostPort = String(server.address().port);
        }
        return result;
    };
    await coordinator.lifecycle('mailpit', 'start');
    assert.equal(config.ports, oldPorts);
    assert.equal(config.ports.mailpit, server.address().port);
    await poll(/** 探测刷新后的 Mailpit 健康接口。 */ () => coordinator.mailApi('/readyz'), 1000);
    assert.deepEqual(await coordinator.clearData(), []);
    assert.equal(requests[0].url, '/readyz');
    assert.ok(requests.some(/** 查找定向邮件删除请求，证明清理实际执行。 */ ({ method, url }) => method === 'DELETE' && url === '/api/v1/messages'));
    assert.equal(messages.length, 0);
});

test('bootstrap rotates both root accounts without switching authenticated account', { timeout: 5000 }, /** 验证 MySQL 初始化始终使用本地 root 账号轮换凭据，密码不进入命令行。 */ async (t) => {
    const server = net.createServer(/** 发送最小 MySQL 协议握手供网络就绪探测。 */ (socket) => socket.end(Buffer.from([1, 0, 0, 0, 10])));
    await new Promise(/** 启动本用例 MySQL 握手端点。 */ (resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(/** 登记测试结束后的握手监听器清理。 */ () => new Promise(/** 等待握手监听器关闭。 */ (resolve) => server.close(resolve)));
    const passwords = { localhost: '', '%': '' };
    const calls = [];
    const coordinator = new DependencyCoordinator({ ids: { mysql: 'b'.repeat(64) },
        host: '127.0.0.1', ports: { mysql: server.address().port } }, {
        Redis: /** 提供不联网的 Redis 类型占位，后续方法由夹具替换。 */ class {}, nodemailer: {},
        runCommand: /** 模拟本地和通配 root 账号密码校验及轮换，核对 SQL 经标准输入传递。 */ async (executable, args, options) => {
            const host = args.includes('--protocol=SOCKET') ? 'localhost' : '%';
            assert.ok(options.env.MYSQL_PWD === passwords[host], 'selected root account rejects password');
            assert.equal(executable, 'docker');
            assert.ok(!args.some(/** 检测命令参数是否含临时数据库密码。 */ (arg) => arg.includes(coordinator.password)));
            calls.push(options.input);
            if (options.input === 'SELECT 1;') return '1';
            if (options.input === 'SELECT CURRENT_USER();') return 'root@localhost';
            if (options.input.includes('SELECT Host')) return 'localhost\n%';
            const change = options.input.match(/^ALTER USER 'root'@'(localhost|%)' IDENTIFIED BY '([a-f0-9]+)';$/);
            assert.ok(change, 'only known bootstrap statements');
            passwords[change[1]] = change[2];
            return '';
        }
    });
    coordinator.owned.add('mysql');
    coordinator.redis = /** 提供成功的 Redis 初始化替身，隔离当前 MySQL 账号合同。 */ async () => ({ ping: /** 返回预期 Redis 健康回复。 */ async () => 'PONG', config: /** 确认 Redis 配置操作成功。 */ async () => 'OK', /** 无真实连接需要释放，仅满足初始化客户端接口。 */ disconnect() {} });
    coordinator.mailApi = /** 将 Mailpit 健康检查固定为成功以隔离 MySQL 合同。 */ async () => true;
    await coordinator.bootstrap();
    assert.equal(await coordinator.sql('SELECT 1;'), '1');
    assert.equal(passwords.localhost, coordinator.password);
    assert.equal(passwords['%'], coordinator.password);
    assert.ok(calls.some(/** 查找通配 root 账号密码轮换语句。 */ (sql) => sql.includes("ALTER USER 'root'@'%'")));
});

test('bootstrap failures expose only fixed stage and safe error category', { timeout: 5000 }, /** 验证初始化错误分类固定，原始异常中的随机敏感内容不会泄漏。 */ async (t) => {
    const server = net.createServer(/** 发送 MySQL 握手字节使测试进入账号检查阶段。 */ (socket) => socket.end(Buffer.from([1, 0, 0, 0, 10])));
    await new Promise(/** 启动本用例的临时握手监听器。 */ (resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(/** 登记握手监听器的测试结束清理。 */ () => new Promise(/** 等待临时握手监听器释放端口。 */ (resolve) => server.close(resolve)));
    const secret = require('node:crypto').randomBytes(24).toString('hex');
    const coordinator = new DependencyCoordinator({ ids: { mysql: 'b'.repeat(64) },
        host: '127.0.0.1', ports: { mysql: server.address().port } }, {
        Redis: /** 提供无外部连接的 Redis 类型占位。 */ class {}, nodemailer: {},
        runCommand: /** 健康查询成功后注入含敏感内容的账号错误。 */ async (executable, args, options) => {
            if (options.input === 'SELECT 1;') return '1';
            throw Object.assign(new Error(secret), { code: secret, diagnostic: { stage: secret } });
        }
    });
    coordinator.owned.add('mysql');
    await assert.rejects(coordinator.bootstrap(), /** 核对安全阶段与分类，并确认异常序列化和消息都不含原始敏感值。 */ (error) => {
        assert.deepEqual(error.diagnostic, { stage: 'mysql-account', category: 'operation-failed' });
        assert.ok(!JSON.stringify(error).includes(secret));
        assert.ok(!error.message.includes(secret));
        return true;
    });
});

test('service lock matches hosted workflow images and dynamically mapped ports', /** 核对依赖锁、截止设置与 Linux 工作流产物和运行入口的一致性。 */ () => {
    const workflow = fs.readFileSync(path.resolve(__dirname, '../../.github/workflows/linux-ci.yml'), 'utf8');
    assert.equal(lock.schemaVersion, 1);
    assert.ok(lock.healthDeadlineMs > 0 && lock.healthDeadlineMs <= 120000);
    assert.ok(lock.commandDeadlineMs > 0 && lock.commandDeadlineMs <= 5000);
    assert.equal(lock.gracefulStopSeconds, 10);
    const cmake = fs.readFileSync(path.resolve(__dirname, '../../CMakeLists.txt'), 'utf8');
    assert.ok(cmake.includes('set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")'));
    assert.ok(workflow.includes('cp out/build/linux-x64-release/bin/service_run out/phase3c/service-launcher/'));
    const serviceJob = workflow.split('  disposable-services:')[1].split('  downstream-contract:')[0];
    const jobConfiguration = serviceJob.split('    steps:')[0];
    assert.ok(!jobConfiguration.includes('${{ job.'), 'job context is only available in step env');
    for (const name of ['Prove bounded disposable service lifecycle', 'Preserve fail-closed outer evidence']) {
        const step = serviceJob.split(`      - name: ${name}`)[1].split('      - name:')[0];
        assert.ok(step.includes('        env:'), `${name} must inject its own runtime service identity`);
        for (const service of ['redis', 'mysql', 'mailpit']) {
            assert.ok(step.includes(`CHAT_${service.toUpperCase()}_CONTAINER: \${{ job.services.${service}.id }}`));
        }
    }
    for (const service of Object.values(lock.services)) {
        assert.match(service.image, /:[a-z0-9.]+@sha256:[a-f0-9]{64}$/);
        assert.ok(workflow.includes(`image: ${service.image}`));
        for (const port of service.ports) assert.ok(workflow.includes(`127.0.0.1::${port}`));
        assert.ok(service.health && service.source.startsWith('https://'));
    }
});

test('configuration rejects personal hosts, invalid ports and unowned container IDs', /** 验证依赖配置严格要求所属身份、loopback 地址及合法端口。 */ () => {
    assert.throws(/** 拒绝缺失所有必要字段的配置。 */ () => loadConfiguration({}), /configuration/);
    const env = {
        CHAT_SERVICE_RUN_ID: 'a'.repeat(32), CHAT_SERVICE_HOST: '127.0.0.1',
        CHAT_REDIS_PORT: '32100', CHAT_MYSQL_PORT: '32101', CHAT_SMTP_PORT: '32102', CHAT_MAILPIT_PORT: '32103',
        CHAT_REDIS_CONTAINER: 'a'.repeat(64), CHAT_MYSQL_CONTAINER: 'b'.repeat(64),
        CHAT_MAILPIT_CONTAINER: 'c'.repeat(64)
    };
    assert.equal(loadConfiguration(env).database, `chat_${'a'.repeat(32)}`);
    assert.throws(/** 拒绝指向非 loopback 地址的配置。 */ () => loadConfiguration({ ...env, CHAT_SERVICE_HOST: '192.168.1.1' }), /configuration/);
    assert.throws(/** 拒绝端口值中包含尾随字符。 */ () => loadConfiguration({ ...env, CHAT_MYSQL_PORT: '3306junk' }), /configuration/);
    assert.throws(/** 拒绝任意容器名称代替精确所属 ID。 */ () => loadConfiguration({ ...env, CHAT_MYSQL_CONTAINER: 'shared-mysql' }), /configuration/);
});

test('failed health checks consume a finite deadline and never become PASS', /** 验证始终未就绪的探针在短期限内失败。 */ async () => {
    await assert.rejects(poll(/** 持续返回未就绪以触发健康截止。 */ async () => false, 30), /health deadline/);
});

test('subprocess failure and timeout discard potentially sensitive command output', /** 验证子进程非零退出与超时均失败，且不回显敏感标准错误。 */ async () => {
    await assert.rejects(runCommand(process.execPath, ['-e', 'process.stderr.write("secret");process.exit(2)']),
        /** 核对失败仅包含固定退出码诊断。 */ (error) => error.message === 'command failed (2)');
    await assert.rejects(runCommand(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], { timeout: 50 }),
        /command deadline/);
});

test('missing outer evidence remains failed and emits all required diagnostics', /** 验证缺少证据时最终入口生成明确失败报告而不是默认通过。 */ async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-evidence-'));
    try {
        await assert.rejects(runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false' }
        }), /command failed/);
        assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'teardown.json'))).complete, false);
        assert.ok(fs.existsSync(path.join(root, 'service-endpoints.json')));
        assert.match(fs.readFileSync(path.join(root, 'linux_services.xml'), 'utf8'), /failures="1"/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('complete inner and process cleanup preserves real service report', /** 验证完整真实报告和清理证据可通过最终入口且原报告不被替换。 */ async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-service-evidence-'));
    try {
        fs.writeFileSync(path.join(root, 'teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'process-teardown.json'), '{"complete":true}');
        fs.writeFileSync(path.join(root, 'service-endpoints.json'), '{}');
        const report = '<testsuite name="actual" tests="12" failures="0"/>';
        fs.writeFileSync(path.join(root, 'linux_services.xml'), report);
        await runCommand(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, GITHUB_ACTIONS: 'false' }
        });
        assert.equal(fs.readFileSync(path.join(root, 'linux_services.xml'), 'utf8'), report);
    } finally { fs.rmSync(root, { recursive: true }); }
});
