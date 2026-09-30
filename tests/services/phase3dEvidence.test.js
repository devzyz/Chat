'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { groups, reportGroups, validate, realAcceptanceRequested } = require('./phase3dEvidence');
const { writeReports } = require('./serviceReports');
const { createTopology } = require('./twoServerTopology');

test('foundation evidence binds exact cases, bytes, SHA, distinct clients and cleanup', /** 验证证据绑定精确 Test ID、文件字节、源码 SHA、独立客户端及完整清理。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-3d-evidence-'));
    const previous = process.env.CHAT_CANDIDATE_SHA;
    const sha = 'a'.repeat(40);
    const write = /** 将合成 JSON 写入本测试拥有的证据文件。 */ (name, value) => fs.writeFileSync(path.join(root, name), JSON.stringify(value));
    const cases = Array.from({ length: 7 }, /** 生成完整基础合同的合成通过记录。 */ (_, index) => ({ id: `E03-CONTRACT-${String(index + 6).padStart(2, '0')}`,
        name: 'synthetic validator input', pass: true }));
    const topology = createTopology('b'.repeat(32), {
        gate: 30001, status: 30002, varify: 30003, chatA: 30004, rpcA: 30005, chatB: 30006, rpcB: 30007 });
    topology.clients = topology.users.map(/** 为每个逻辑用户生成独立客户端身份和匹配实例端点。 */ (user, index) => ({ logical: user.logical, pid: index + 100,
        uid: index + 1, active: true, host: topology.host, port: topology.servers[index].port }));
    const restore = /** 恢复完整且自洽的基础报告、拓扑与清理证据。 */ () => {
        writeReports(root, '3D-00', cases, { groups, manifest: 'phase3d-reports.json', level: 'E2E' });
        write('topology.json', topology);
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) write(name, { complete: true });
    };
    try {
        process.env.CHAT_CANDIDATE_SHA = sha;
        restore(); validate(root, sha);
        const finalize = /** 有界运行真实最终证据入口并取得退出状态。 */ () => spawnSync(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, CHAT_SERVICE_SELECTOR: '3D-00', GITHUB_ACTIONS: 'false' }, timeout: 3000 });
        write('teardown.json', { complete: true });
        assert.equal(finalize().status, 0);
        fs.unlinkSync(path.join(root, 'phase3d-reports.json'));
        assert.equal(finalize().status, 1);
        assert.match(fs.readFileSync(path.join(root, groups[0].file), 'utf8'), /<failure/);
        restore();
        assert.throws(/** 用错误源码 SHA 校验证据，预期拒绝串用。 */ () => validate(root, 'c'.repeat(40)));
        const reportPath = path.join(root, groups[0].file);
        fs.appendFileSync(reportPath, 'changed bytes');
        assert.throws(/** 校验被篡改字节的报告，预期摘要不匹配。 */ () => validate(root, sha));
        restore();
        writeReports(root, '3D-00', cases.slice(1), { groups, manifest: 'phase3d-reports.json' });
        assert.throws(/** 校验缺少基础用例的报告，预期失败。 */ () => validate(root, sha));
        restore();
        const wrongIds = cases.map(/** 复制基础记录以独立修改 Test ID。 */ value => ({ ...value })); wrongIds[0].id = 'E03-CONTRACT-99';
        writeReports(root, '3D-00', wrongIds, { groups, manifest: 'phase3d-reports.json' });
        assert.throws(/** 校验数量相同但 Test ID 错误的报告。 */ () => validate(root, sha));
        restore(); write('topology.json', { ...topology, clients: [topology.clients[0], topology.clients[0]] });
        assert.throws(/** 校验重复客户端身份的拓扑，预期拒绝。 */ () => validate(root, sha));
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) {
            restore(); write(name, { complete: false }); assert.throws(/** 校验未完成的应用清理、进程清理或脱敏证据。 */ () => validate(root, sha));
        }
        restore();
        assert.throws(/** 用更大范围选择器校验只有基础用例的证据，预期失败。 */ () => validate(root, sha, '3D-01'));
        const journey = Array.from({ length: 7 }, /** 生成完整好友旅程用例的合成通过记录。 */ (_, index) => ({
            id: `E03-JOURNEY-${String(index + 1).padStart(2, '0')}`,
            name: 'synthetic journey validator input', pass: true }));
        const writeJourney = /** 写入基础合同与指定旅程用例形成的报告。 */ values => writeReports(root, '3D-01', [...cases, ...values], {
            groups: reportGroups('3D-01'), manifest: 'phase3d-reports.json', level: 'E2E' });
        writeJourney(journey);
        validate(root, sha, '3D-01');
        writeJourney(journey.slice(1));
        assert.throws(/** 校验缺少旅程用例的证据，预期失败。 */ () => validate(root, sha, '3D-01'));
        writeJourney(journey.map(/** 把第四条旅程用例改为失败。 */ (value, index) => ({ ...value, pass: index !== 3 })));
        assert.throws(/** 校验含失败旅程用例的证据，预期拒绝。 */ () => validate(root, sha, '3D-01'));
        writeJourney(journey);
        fs.appendFileSync(path.join(root, 'linux_phase3d_journey.xml'), 'modified');
        assert.throws(/** 校验字节被篡改的旅程报告，预期失败。 */ () => validate(root, sha, '3D-01'));
        assert.throws(/** 验证未知 Phase 3D 选择器被拒绝。 */ () => reportGroups('unknown'));
        restore();
        const messaging = Array.from({ length: 8 }, /** 生成完整跨实例消息用例的合成通过记录。 */ (_, index) => ({
            id: `E03-XMSG-${String(index + 1).padStart(2, '0')}`,
            name: 'synthetic messaging validator input', pass: true }));
        const writeMessaging = /** 写入基础、旅程及指定消息用例报告。 */ values => writeReports(root, '3D-02', [...cases, ...journey, ...values], {
            groups: reportGroups('3D-02'), manifest: 'phase3d-reports.json', level: 'E2E' });
        writeMessaging(messaging);
        validate(root, sha, '3D-02');
        writeMessaging(messaging.slice(1));
        assert.throws(/** 校验缺少消息用例的证据，预期失败。 */ () => validate(root, sha, '3D-02'));
        writeMessaging(messaging.map(/** 把第六条消息用例改为失败。 */ (value, index) => ({ ...value, pass: index !== 5 })));
        assert.throws(/** 校验含失败消息用例的证据，预期拒绝。 */ () => validate(root, sha, '3D-02'));
        writeMessaging(messaging);
        fs.appendFileSync(path.join(root, 'linux_phase3d_messaging.xml'), 'modified');
        assert.throws(/** 校验字节被篡改的消息报告，预期失败。 */ () => validate(root, sha, '3D-02'));
        restore();
        write('topology.json', { ...topology, recoveredClients: [{ ...topology.clients[0],
            previousPid: topology.clients[0].pid, pid: 9999 }] });
        const recovery = Array.from({ length: 4 }, /** 生成完整历史恢复用例的合成通过记录。 */ (_, index) => ({
            id: `E03-RECOVER-${String(index + 1).padStart(2, '0')}`,
            name: 'synthetic recovery validator input', pass: true }));
        const writeRecovery = /** 写入前置报告及指定恢复用例形成的证据。 */ values => writeReports(root, '3D-03-history', [...cases, ...journey, ...messaging, ...values], {
            groups: reportGroups('3D-03-history'), manifest: 'phase3d-reports.json', level: 'E2E' });
        writeRecovery(recovery);
        validate(root, sha, '3D-03-history');
        write('topology.json', topology);
        assert.throws(/** 校验缺少恢复客户端身份的拓扑，预期拒绝。 */ () => validate(root, sha, '3D-03-history'));
        write('topology.json', { ...topology, recoveredClients: [{ ...topology.clients[0],
            previousPid: topology.clients[0].pid, pid: 9999 }] });
        writeRecovery(recovery.slice(1));
        assert.throws(/** 校验缺少恢复用例的证据，预期失败。 */ () => validate(root, sha, '3D-03-history'));
        writeRecovery(recovery.map(/** 把第三条恢复用例改为失败。 */ (value, index) => ({ ...value, pass: index !== 2 })));
        assert.throws(/** 校验含失败恢复用例的证据，预期拒绝。 */ () => validate(root, sha, '3D-03-history'));
        writeRecovery(recovery);
        fs.appendFileSync(path.join(root, 'linux_phase3d_recovery.xml'), 'modified');
        assert.throws(/** 校验字节被篡改的恢复报告，预期失败。 */ () => validate(root, sha, '3D-03-history'));
        assert.equal(reportGroups('3D-03').at(-1).expected, 11);
    } finally {
        if (previous === undefined) delete process.env.CHAT_CANDIDATE_SHA;
        else process.env.CHAT_CANDIDATE_SHA = previous;
        fs.rmSync(root, { recursive: true });
    }
});


test('requested release acceptance fails closed on evidence and teardown gaps', /** 验证按需门禁拒绝缺项、跳过、错源码及不完整清理。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-release-evidence-'));
    const previous = process.env.CHAT_CANDIDATE_SHA;
    const sha = 'd'.repeat(40);
    const write = /** 写入本测试拥有的合成证据。 */ (name, value) =>
        fs.writeFileSync(path.join(root, name), JSON.stringify(value));
    const selected = reportGroups('3D', true);
    const cases = selected.flatMap(/** 生成各组连续 Test ID 供校验器测试，不作为业务证据。 */ (group, groupIndex) =>
        Array.from({ length: group.expected }, /** 为每条合成记录设置精确编号。 */ (_, index) => ({
            id: `${group.prefix}${String(index + (groupIndex === 0 ? 6 : 1)).padStart(2, '0')}`,
            name: 'synthetic validator input', pass: true })));
    const topology = createTopology('e'.repeat(32), {
        gate: 31001, status: 31002, varify: 31003, chatA: 31004, rpcA: 31005, chatB: 31006, rpcB: 31007 });
    topology.clients = topology.users.map(/** 构造互不共享的客户端身份。 */ (user, index) => ({ logical: user.logical,
        pid: 100 + index, uid: index + 1, active: true, host: topology.host, port: topology.servers[index].port }));
    topology.recoveredClients = [{ ...topology.clients[0], previousPid: 100, pid: 200 }];
    topology.releaseClients = ['releasea', 'releaseb', 'releasec', 'released'].map(
        /** 为校验器构造四组互异的账号和进程身份。 */ (logical, index) => ({ logical, pid: 500 + index,
            uid: 10 + index, active: true }));
    topology.serverRestarts = ['ChatA', 'ChatB'].map(/** 构造已重启的服务进程身份。 */ (logical, index) => ({ logical,
        previous: { pid: 300 + index, creationTime: '100' }, current: { pid: 400 + index, creationTime: '200' },
        advertisedPort: topology.servers[index].port }));
    const restore = /** 恢复完整的校验器输入及清理证据。 */ (requested = true) => {
        writeReports(root, '3D', requested ? cases : cases.filter(/** 默认报告不包含未请求的真实验收。 */ value =>
            !value.id.startsWith('E03-RELEASE-')), {
            groups: reportGroups('3D', requested), manifest: 'phase3d-reports.json', level: 'E2E' });
        write('topology.json', topology);
        write('fault-relay.json', { complete: true, droppedAck: true, replayedNotification: true, committedId: '1' });
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json', 'teardown.json']) {
            write(name, { complete: true });
        }
        write('real-acceptance.json', { requested, sourceSha: sha,
            status: requested ? 'passed' : 'not-requested', cleanupComplete: requested });
    };
    const check = /** 按已请求的完整真实验收执行验证。 */ () => validate(root, sha, '3D', true);
    const finalize = /** 执行真实最终门禁入口，不操作 Docker。 */ () => spawnSync(process.execPath,
        [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, CHAT_SERVICE_SELECTOR: '3D', CHAT_REAL_ACCEPTANCE: '1', GITHUB_ACTIONS: 'false' }, timeout: 3000 });
    try {
        process.env.CHAT_CANDIDATE_SHA = sha;
        assert.equal(realAcceptanceRequested('0'), false);
        assert.equal(realAcceptanceRequested('1'), true);
        assert.throws(/** 拒绝拼错开关导致的静默跳过。 */ () => realAcceptanceRequested('true'));
        assert.throws(/** 非完整选择器不能请求首版验收。 */ () => reportGroups('3D-03', true));
        restore(); check(); assert.equal(finalize().status, 0);
        restore(); write('topology.json', { ...topology, releaseClients: [] }); assert.throws(check);
        for (const key of ['pid', 'uid']) {
            restore(); write('topology.json', { ...topology, releaseClients: topology.releaseClients.map(
                /** 合成共享账号或进程，门禁必须拒绝。 */ client => ({ ...client, [key]: 777 })) });
            assert.throws(check);
        }
        restore(false); validate(root, sha, '3D'); assert.throws(check);
        assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'real-acceptance.json'))).status, 'not-requested');
        restore(); assert.throws(/** 未请求时不能接受标记通过的专项证据。 */ () => validate(root, sha, '3D'));
        restore(); fs.unlinkSync(path.join(root, 'linux_first_release.xml')); assert.throws(check);
        restore(); fs.unlinkSync(path.join(root, 'real-acceptance.json')); assert.throws(check);
        restore(); write('real-acceptance.json', { requested: true, sourceSha: 'f'.repeat(40),
            status: 'passed', cleanupComplete: true }); assert.throws(check);
        restore(); writeReports(root, '3D', cases.slice(0, -1), {
            groups: selected, manifest: 'phase3d-reports.json', level: 'E2E' }); assert.throws(check);
        restore(); writeReports(root, '3D', cases.map(/** 将真实验收最后一条合成结果设为失败。 */ (value, index) =>
            ({ ...value, pass: index !== cases.length - 1 })), {
            groups: selected, manifest: 'phase3d-reports.json', level: 'E2E' }); assert.throws(check);
        restore();
        const reportPath = path.join(root, 'linux_first_release.xml');
        const skipped = fs.readFileSync(reportPath, 'utf8').replace('</testcase>', '<skipped/></testcase>');
        fs.writeFileSync(reportPath, skipped);
        const manifest = JSON.parse(fs.readFileSync(path.join(root, 'phase3d-reports.json')));
        manifest.reports.at(-1).sha256 = require('node:crypto').createHash('sha256').update(skipped).digest('hex');
        write('phase3d-reports.json', manifest); assert.throws(check);
        restore();
        const skippedCount = fs.readFileSync(reportPath, 'utf8').replace('failures="0"', 'failures="0" skipped="1"');
        fs.writeFileSync(reportPath, skippedCount);
        manifest.reports.at(-1).sha256 = require('node:crypto').createHash('sha256').update(skippedCount).digest('hex');
        write('phase3d-reports.json', manifest); assert.throws(check);
        for (const name of ['application-teardown.json', 'process-teardown.json', 'teardown.json']) {
            restore(); write(name, { complete: false }); assert.throws(check);
            assert.equal(finalize().status, 1);
            assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'real-acceptance.json'))).status, 'failed');
        }
    } finally {
        if (previous === undefined) delete process.env.CHAT_CANDIDATE_SHA;
        else process.env.CHAT_CANDIDATE_SHA = previous;
        fs.rmSync(root, { recursive: true });
    }
});
