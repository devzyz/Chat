'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const test = require('node:test');
const { spawnSync } = require('node:child_process');
const workflow = /** 读取指定工作流的 UTF-8 文本。 */ name => fs.readFileSync(path.join(__dirname, '../../.github/workflows', name), 'utf8');
const ci = workflow('ci.yml');
const job = /** 提取指定作业块用于路由与依赖合同断言。 */ (text, name) => text.split(`  ${name}:`)[1]?.split(/\r?\n  [\w-]+:/)[0];

test('only PR and develop pushes cancel obsolete work', /** 验证快速运行可取消过时版本，发布、周检和手动运行保持独立。 */ () => {
    const concurrency = ci.split('concurrency:')[1].split('jobs:')[0];
    const cancel = new Function('github', `return ${concurrency.match(/cancel-in-progress: \$\{\{ (.+) \}\}/)[1]};`);
    const group = new Function('github', 'format', `return ${concurrency.match(/group: ci-\$\{\{ (.+) \}\}/)[1]};`);
    const format = /** 展开此工作流使用的单值组名格式。 */ (value, id) => value.replace('{0}', id);
    for (const [event_name, ref, expected] of [
        ['pull_request', 'refs/pull/7/merge', true], ['push', 'refs/heads/develop', true],
        ['push', 'refs/heads/master', false], ['schedule', 'refs/heads/develop', false],
        ['workflow_dispatch', 'refs/heads/develop', false]
    ]) {
        const event = { event_name, ref, event: { pull_request: { number: 7 } }, run_id: 1 };
        assert.equal(cancel(event), expected);
        assert.equal(group(event, format) === group({ ...event, run_id: 2 }, format), expected);
    }
});

test('documentation builds skip only behind successful scope and static checks', /** 验证文档分流保留实际静态门禁和失败传播，其他路线仍构建。 */ () => {
    const windows = workflow('windows-ci.yml');
    assert.match(job(windows, 'static-check'), /docs_only: \$\{\{ steps\.scope\.outputs\.docs_only \}\}/);
    assert.match(job(windows, 'static-check'), /run: node scripts\/ci\/ciScope\.js/);
    assert.match(job(ci, 'windows'), /needs: plan/);
    assert.doesNotMatch(job(windows, 'static-check'), /if:.*docs_only/);
    for (const name of ['servers-release', 'client-release', 'varify-release']) {
        assert.match(job(windows, name), /needs: static-check\s+if: needs\.static-check\.outputs\.docs_only != 'true'/);
    }
    assert.match(job(ci, 'regression'), /if: \$\{\{ always\(\) \}\}/);
    assert.match(job(ci, 'regression'), /test "\$RESULT" = success/);
});

test('Linux cold restore is explicit and report validation still runs after upstream failure', /** 验证冷恢复与周检升级解耦，业务失败不阻止生成服务失败报告。 */ () => {
    assert.match(job(ci, 'linux'), /cold_build: \$\{\{ needs\.plan\.outputs\.cold_linux == 'true' \}\}/);
    const downstream = job(workflow('linux-ci.yml'), 'downstream-contract');
    const service = downstream.split('- name: Check service reports and cleanup')[1].split('- name:')[0];
    const business = downstream.split('- name: Check business reports and cleanup')[1].split('- name:')[0];
    assert.match(service, /if: \$\{\{ !cancelled\(\) \}\}/);
    assert.match(service, /python3 tests\/services\/gate\.py/);
    assert.doesNotMatch(service, /BUSINESS_RESULT/);
    assert.match(business, /if: \$\{\{ !cancelled\(\) \}\}/);
    assert.match(business, /test "\$BUSINESS_RESULT" = success/);
    assert.match(business, /verify-service-reports\.js/);
});

test('platforms consume one policy and remain independent of Windows tools', /** 验证平台只依赖统一策略，不重复解释事件或依赖原生工具链。 */ () => {
    assert.match(job(ci, 'plan'), /run: node scripts\/ci\/ciPolicy\.js/);
    assert.doesNotMatch(job(ci, 'plan'), /toolchain\.js|GH_TOKEN|gh api/);
    for (const name of ['windows', 'linux']) {
        assert.match(job(ci, name), /needs: plan/);
        assert.doesNotMatch(job(ci, name), /github\.event_name|github\.ref|github\.base_ref|needs: toolchain/);
    }
    assert.match(job(ci, 'linux'), /if: needs\.plan\.outputs\.mode == 'full'/);
    assert.match(job(ci, 'windows'), /build_packages: \$\{\{ needs\.plan\.outputs\.mode == 'full' \}\}/);
    assert.match(ci, /cron: '17 19 \* \* 0'/);
    assert.match(ci, /push:\s+branches: \[develop, master\]/);
    assert.match(ci, /pull_request:\s+branches: \[develop, master\]/);
    assert.match(ci, /types: \[opened, synchronize, reopened, edited, ready_for_review\]/);
    assert.match(ci, /mode:\s+description:[^\n]+\s+type: choice\s+options: \[quick, full\]\s+default: full/);
});

test('publication requires master push and all full checks; failed smoke cannot publish', /** 验证仅 master 推送且完整检查成功后可发布，冒烟失败不能绕过。 */ () => {
    const release = job(ci, 'release');
    const condition = release.match(/^    if: (.+)$/m)[1];
    const publish = new Function('github', `return ${condition};`);
    assert.equal(publish({ event_name: 'push', ref: 'refs/heads/master' }), true);
    for (const event_name of ['schedule', 'workflow_dispatch', 'pull_request']) {
        assert.equal(publish({ event_name, ref: 'refs/heads/master' }), false);
    }
    assert.equal(publish({ event_name: 'push', ref: 'refs/heads/develop' }), false);
    assert.match(release, /needs: full/);
    assert.match(job(ci, 'full'), /needs: \[plan, windows, linux\]/);
    const releaseWorkflow = workflow('release.yml');
    assert.match(job(releaseWorkflow, 'smoke'), /needs: package/);
    assert.match(job(releaseWorkflow, 'publish'), /needs: smoke/);
    assert.doesNotMatch(job(releaseWorkflow, 'publish'), /always\(\)|environment:/);
    assert.doesNotMatch(releaseWorkflow, /BuildCandidate|RestoreServers|settings_receipt/);
});

test('full gate stays active when policy fails and optional acceptance cannot become a false pass', /** 验证策略失败或输出缺失时全量门禁仍执行失败检查。 */ () => {
    const expression = job(ci, 'full').match(/if: \$\{\{ (.+) \}\}/)[1];
    const enabled = new Function('needs', 'always', `return ${expression};`);
    for (const result of ['success', 'failure', 'cancelled', 'skipped', '']) {
        for (const mode of ['quick', 'full', undefined]) {
            assert.equal(enabled({ plan: { result, outputs: { mode } } }, /** 模拟汇总始终执行。 */ () => true),
                result !== 'success' || mode === 'full');
        }
    }
    assert.match(job(ci, 'regression'), /needs: \[plan, windows\]/);
    assert.match(job(ci, 'full'), /needs: \[plan, windows, linux\]/);
    const acceptance = new Function('needs', 'github', 'inputs', 'always',
        `return ${job(ci, 'real-acceptance').match(/if: \$\{\{ (.+) \}\}/)[1]};`);
    assert.equal(acceptance({ plan: { outputs: {} } }, { event_name: 'workflow_dispatch' },
        { real_acceptance: true }, /** 策略失败仍需给出专项失败状态。 */ () => true), true);
});

test('required gate shells reject failed policy and every unsuccessful platform result', /** 执行真实汇总脚本，验证失败、取消、跳过与空结果不会放行。 */ () => {
    const git = process.platform === 'win32' ? spawnSync('where.exe', ['git'], { encoding: 'utf8' }) : null;
    const bash = process.env.CHAT_TEST_BASH || (git ? path.resolve(path.dirname(git.stdout.trim().split(/\r?\n/)[0]),
        '../bin/bash.exe') : 'bash');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-ci-results-'));
    try {
        for (const name of ['regression', 'full', 'real-acceptance']) {
            const run = job(ci, name).split('        run: ')[1];
            const script = run.startsWith('|') ? run.split(/\r?\n/).filter(
                /** 仅提取 YAML 中实际执行的脚本。 */ line => line.startsWith('          ')).map(
                /** 去除脚本的 YAML 缩进。 */ line => line.slice(10)).join('\n') : run.trim();
            const fields = name === 'regression' ? ['PLAN_RESULT', 'RESULT'] : ['PLAN_RESULT', 'WINDOWS_RESULT', 'LINUX_RESULT'];
            const baseline = Object.fromEntries(fields.map(/** 建立所有依赖成功的基线。 */ field => [field, 'success']));
            for (const field of fields) {
                for (const value of ['success', 'failure', 'cancelled', 'skipped', '']) {
                    const summary = path.join(root, 'summary');
                    fs.writeFileSync(summary, '');
                    const result = spawnSync(bash, ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script], {
                        env: { ...process.env, ...baseline, [field]: value, SOURCE_SHA: 'a'.repeat(40), GITHUB_STEP_SUMMARY: summary },
                        encoding: 'utf8', timeout: 5000
                    });
                    assert.ifError(result.error);
                    assert.equal(result.status === 0, value === 'success', `${name}/${field}/${value}: ${result.stderr}`);
                    if (name === 'real-acceptance') {
                        assert.equal(fs.readFileSync(summary, 'utf8').includes('acceptance passed'), value === 'success');
                    }
                }
            }
        }
    } finally { fs.rmSync(root, { recursive: true, force: true }); }
});

test('cold Windows restore and Linux business steps retain setup and cleanup time', /** 验证冷恢复和业务步骤保留足够的启动、执行与清理时间。 */ () => {
    const windows = job(workflow('windows-ci.yml'), 'servers-release');
    assert.ok(Number(windows.match(/timeout-minutes: (\d+)/)[1]) >= 174 + 30);
    for (const name of ['disposable-services', 'two-server-contract']) {
        const body = job(workflow('linux-ci.yml'), name);
        assert.ok(Number(body.match(/timeout-minutes: (\d+)/)[1]) >= 20);
    }
    assert.doesNotMatch(workflow('linux-ci.yml'), /phase3dChecks|checks: read/);
});

test('Windows builds production servers once and validates registration before all lanes', /** 验证生产 Server 只构建一次，所有测试路线先核对注册。 */ () => {
    const windows = workflow('windows-ci.yml');
    assert.doesNotMatch(job(windows, 'servers-release'), /-Task BuildServers/);
    assert.equal((windows.match(/-Task CheckTestStructure/g) || []).length, 1);
    for (const [name, task] of [['static-check', 'RunScriptTests'], ['servers-release', 'RunServerTests'],
        ['client-release', 'RunClientTests'], ['varify-release', 'RunVarifyTests']]) {
        const body = job(windows, name);
        assert.match(body, new RegExp(`-Task ${task} -SkipTestStructureCheck`));
        if (name !== 'static-check') assert.match(body, /needs: static-check/);
    }
    const runner = fs.readFileSync(path.join(__dirname, '../../scripts/windows-local.ps1'), 'utf8');
    const server = runner.split('function Run-ServerTests {')[1].split('function ')[0];
    assert.match(server, /\/t:GateServer;StatusServer;ChatServer;ResourceServer;ServerUnitTests/);
    assert.match(server, /Assert-RegressionReport/);
});

test('structure-check bypass is rejected outside the prerequisite CI lanes', { skip: process.platform !== 'win32' }, /** 验证注册检查跳过参数只允许受前置门禁约束的 CI 测试路线。 */ () => {
    for (const [task, githubActions] of [['RunVarifyTests', 'false'], ['CheckTestStructure', 'true']]) {
        const result = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            path.join(__dirname, '../../scripts/windows-local.ps1'), '-Task', task, '-SkipTestStructureCheck'],
        { encoding: 'utf8', env: { ...process.env, GITHUB_ACTIONS: githubActions }, timeout: 30000 });
        assert.notEqual(result.status, 0);
        assert.match(result.stdout + result.stderr, /requires a CI test lane after CheckTestStructure/);
    }
});

test('quick regression omits packages while every full lane retains all release inputs', /** 验证候选打包仅在规定事件启用，快速回归不重复打包。 */ () => {
    const windows = workflow('windows-ci.yml');
    assert.match(windows, /BUILD_PACKAGES: \$\{\{ inputs\.build_packages \}\}/);
    for (const name of ['Create independent server ZIP packages', 'Upload server release directories',
        'Deploy and package Qt client', 'Upload Qt client ZIP', 'Package VarifyServer', 'Upload VarifyServer ZIP']) {
        const step = windows.split(`- name: ${name}`)[1]?.split(/\r?\n      - /)[0];
        assert.ok(step, name);
        assert.match(step, /if: env\.BUILD_PACKAGES == 'true'/);
    }
    assert.equal((workflow('linux-ci.yml').match(/tests\/services\/phase3dEvidence\.test\.js/g) || []).length, 1);
});

test('Linux retains PASS validation without the retired compatibility selector or unused output', /** 验证 Linux 预检真实结果及下游服务报告汇入发布依赖。 */ () => {
    const linux = workflow('linux-ci.yml');
    const runner = fs.readFileSync(path.join(__dirname, '../../scripts/linux-ci.sh'), 'utf8');
    assert.doesNotMatch(runner, /3C-08|CHAT_RELEASE_INVENTORY|compatibility\/bootstrap/);
    assert.doesNotMatch(linux, /preflight-status|outputs\.status|echo 'status=PASS'/);
    assert.ok(linux.includes("jq -e '.status == \"PASS\"' out/phase3c/preflight/linux-preflight.json"));
    assert.match(job(linux, 'downstream-contract'), /verify-service-reports\.js/);
    assert.match(job(ci, 'release'), /needs: full/);
});

test('ResourceServer participates in build, storage regression and release staging', /** 验证资源服务生产目标、存储回归及发布阶段接入统一入口。 */ () => {
    const windows = workflow('windows-ci.yml');
    const runner = fs.readFileSync(path.join(__dirname, '../../scripts/windows-local.ps1'), 'utf8');
    assert.match(runner, /\/t:GateServer;StatusServer;ChatServer;ResourceServer'/);
    assert.match(runner, /\$resourceTestProject\)/);
    assert.match(runner, /server_resource_integration\.xml/);
    assert.match(runner, /Filter = 'StoreTest\.\*'/);
    for (const name of ['Verify independent app-local server directories', 'Create independent server ZIP packages']) {
        const step = windows.split(`- name: ${name}`)[1].split(/\r?\n      - /)[0];
        assert.match(step, /'ResourceServer'/);
    }
});

test('full Linux CI keeps client coverage and separately builds without Qt', /** 验证完整 CI 显式保留客户端，同时构建禁用 Qt 的服务端目标。 */ () => {
    const presets = JSON.parse(fs.readFileSync(path.join(__dirname, '../../CMakePresets.json'), 'utf8'));
    const variables = presets.configurePresets.find(/** 定位完整 Linux CI 配置。 */ item => item.name === 'linux-x64-release').cacheVariables;
    assert.equal(variables.CHAT_BUILD_CLIENT, 'ON');
    assert.equal(variables.BUILD_TESTING, 'ON');
    assert.equal(variables.CHAT_ENABLE_HOSTED_PREFLIGHT, 'ON');
    const linux = workflow('linux-ci.yml');
    assert.match(linux, /-DCHAT_BUILD_CLIENT=OFF -DBUILD_TESTING=OFF/);
    assert.match(linux, /-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE/);
    assert.match(linux, /cmake --build out\/build\/linux-server-only --target GateServer StatusServer ChatServer/);
    assert.match(workflow('windows-ci.yml'), /tests\/build\/server_only_configuration\.cmake/);
});


test('first-release acceptance requires an explicit manual request and preserves normal CI', /** 验证所有普通事件均不启用真实环境首版验收，只有手动布尔开关生效。 */ () => {
    assert.match(ci, /real_acceptance:\s+description:[^\n]+\s+type: boolean\s+default: false/);
    assert.match(job(ci, 'linux'), /real_acceptance: \$\{\{ needs\.plan\.outputs\.real_acceptance == 'true' \}\}/);
    const gate = job(ci, 'real-acceptance');
    assert.match(gate, /always\(\).*github.event_name == 'workflow_dispatch'.*inputs.real_acceptance == true/);
    assert.match(gate, /needs: \[plan, windows, linux\]/);
    assert.match(gate, /test "\$WINDOWS_RESULT" = success\s+test "\$LINUX_RESULT" = success/);
    assert.doesNotMatch(gate, /continue-on-error|workflow_dispatch[^\n]*master|release.yml/);
});

test('requested acceptance builds same-source resources and validates complete evidence', /** 验证可选资源构建、真实服务开关和报告校验连接一致，禁止只开容器就成功。 */ () => {
    const linux = workflow('linux-ci.yml');
    assert.match(linux, /real_acceptance:\s+type: boolean\s+default: false/);
    assert.match(linux, /if: inputs.real_acceptance[\s\S]*CHAT_BUILD_ACCEPTANCE_RESOURCES=ON/);
    assert.match(linux, /--target ResourceServer resource_http_test_host resource_transfer_tests/);
    assert.match(job(linux, 'two-server-contract'), /CHAT_REAL_ACCEPTANCE: \$\{\{ inputs.real_acceptance/);
    assert.match(job(linux, 'downstream-contract'), /CHAT_REAL_ACCEPTANCE: \$\{\{ inputs.real_acceptance/);
    const verify = fs.readFileSync(path.join(__dirname, '../../scripts/ci/verify-service-reports.js'), 'utf8');
    assert.match(verify, /validate\(root, sha, '3D', realAcceptanceRequested\(process.env.CHAT_REAL_ACCEPTANCE\)\)/);
    assert.match(linux, /source-sha.txt\)" = "\$\(git rev-parse HEAD\)"/);
    assert.match(linux, /widgetEvidence.js/);
});


test('real acceptance shell rejects every unsuccessful dependency before writing success', /** 实际执行工作流脚本，防止 AND 列表失败被后续摘要覆盖。 */ () => {
    const block = job(ci, 'real-acceptance').split('        run: |')[1];
    const script = block.split(/\r?\n/).filter(/** 提取工作流实际 Bash 命令。 */ line => line.startsWith('          '))
        .map(/** 去除 YAML 缩进而保留 shell 语义。 */ line => line.slice(10)).join('\n');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-ci-gate-'));
    const summary = path.join(root, 'summary.txt');
    // Git for Windows provides the same Bash semantics without relying on WSL.
    const git = process.platform === 'win32' ? spawnSync('where.exe', ['git'], { encoding: 'utf8' }) : null;
    const bash = process.env.CHAT_TEST_BASH || (git ? path.resolve(path.dirname(git.stdout.trim().split(/\r?\n/)[0]),
        '../bin/bash.exe') : 'bash');
    try {
        for (const windows of ['success', 'failure', 'cancelled', 'skipped', '']) {
            for (const linux of ['success', 'failure', 'cancelled', 'skipped', '']) {
                fs.writeFileSync(summary, '');
                const result = spawnSync(bash, ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script], {
                    env: { ...process.env, PLAN_RESULT: 'success', WINDOWS_RESULT: windows, LINUX_RESULT: linux,
                        SOURCE_SHA: 'a'.repeat(40), GITHUB_STEP_SUMMARY: summary }, encoding: 'utf8', timeout: 5000 });
                assert.ifError(result.error);
                const pass = windows === 'success' && linux === 'success';
                assert.equal(result.status === 0, pass, `${windows}/${linux}: ${result.stderr}`);
                assert.equal(fs.readFileSync(summary, 'utf8').includes('acceptance passed'), pass);
            }
        }
    } finally { fs.rmSync(root, { recursive: true, force: true }); }
});
