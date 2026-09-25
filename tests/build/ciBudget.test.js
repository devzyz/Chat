'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const { spawnSync } = require('node:child_process');
const workflow = /** 读取指定工作流的 UTF-8 文本。 */ name => fs.readFileSync(path.join(__dirname, '../../.github/workflows', name), 'utf8');
const ci = workflow('ci.yml');
const job = /** 提取指定作业块用于路由与依赖合同断言。 */ (text, name) => text.split(`  ${name}:`)[1]?.split(/\r?\n  [\w-]+:/)[0];

test('develop uses quick regression; master, weekly and manual runs use full regression', /** 验证 develop 使用快速回归，master、每周及手动任务执行完整回归。 */ () => {
    const condition = job(ci, 'linux').match(/^    if: (.+)$/m)[1];
    const full = new Function('github', `return ${condition};`);
    for (const [event_name, ref, base_ref, expected] of [
        ['pull_request', 'refs/pull/7/merge', 'develop', false],
        ['push', 'refs/heads/develop', '', false],
        ['pull_request', 'refs/pull/7/merge', 'master', true],
        ['push', 'refs/heads/master', '', true],
        ['schedule', 'refs/heads/develop', '', true],
        ['workflow_dispatch', 'refs/heads/feature', '', true]
    ]) assert.equal(full({ event_name, ref, base_ref }), expected);
    assert.match(ci, /cron: '17 19 \* \* 0'/);
    assert.match(ci, /push:\s+branches: \[develop, master\]/);
    assert.match(ci, /pull_request:\s+branches: \[develop, master\]/);
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
    assert.match(job(ci, 'full'), /needs: \[windows, linux\]/);
    const releaseWorkflow = workflow('release.yml');
    assert.match(job(releaseWorkflow, 'smoke'), /needs: package/);
    assert.match(job(releaseWorkflow, 'publish'), /needs: smoke/);
    assert.doesNotMatch(job(releaseWorkflow, 'publish'), /always\(\)|environment:/);
    assert.doesNotMatch(releaseWorkflow, /BuildCandidate|RestoreServers|settings_receipt/);
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
    const expression = windows.match(/BUILD_PACKAGES: \$\{\{ (.+) \}\}/)[1];
    const packageEnabled = new Function('github', `return ${expression};`);
    for (const [event_name, ref, base_ref, expected] of [
        ['pull_request', 'refs/pull/7/merge', 'develop', false],
        ['push', 'refs/heads/develop', '', false],
        ['pull_request', 'refs/pull/7/merge', 'master', true],
        ['push', 'refs/heads/master', '', true],
        ['schedule', 'refs/heads/develop', '', true],
        ['workflow_dispatch', 'refs/heads/develop', '', true]
    ]) assert.equal(packageEnabled({ event_name, ref, base_ref }), expected);
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
