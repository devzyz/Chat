const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { test } = require('node:test');
const { validate, identity, canRefresh, trustedRun, windowsPassed, select } = require('../../scripts/ci/toolchain');
const bootstrap = require('../../scripts/ci/windows-toolchain.json');
const root = path.resolve(__dirname, '../..');
// 读取仓库相对路径文本用于工作流合同校验。
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const successfulWindowsJobs = ['Static configuration checks', 'Server Release build', 'Qt client Release',
    // 创建四个 Windows 作业成功的 API 样例。
    'VarifyServer dependency and package check'].map(name =>
    ({ name: `windows / ${name}`, conclusion: 'success' }));

// 验证只选择成功可信运行的工具工件，过期或 API 失败不能静默降级。
test('selection retains previous approved tools after a failed refresh and rejects expired or unavailable records', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-toolchain-'));
    const env = { GITHUB_REPOSITORY: 'owner/repo', GITHUB_EVENT_NAME: 'pull_request', GITHUB_REF: 'refs/pull/8/merge' };
    let expired = false;
    let acquisitions = 0;
    let latestWindowsSucceeded = false;
    let expectedRun = 2;
    // 模拟仓库、工件和工作流运行 API，未知请求立即失败。
    const request = endpoint => {
        if (endpoint === 'repos/owner/repo') return { default_branch: 'develop' };
        if (endpoint.endsWith('/workflows/ci.yml')) return { id: 12 };
        if (endpoint.includes('/jobs?')) return { total_count: 4,
            jobs: endpoint.includes('/runs/3/') && !latestWindowsSucceeded ? [] : successfulWindowsJobs };
        if (endpoint.includes('/artifacts?')) return { artifacts: [
            { workflow_run: { id: 3 }, expired: false }, { workflow_run: { id: 2 }, expired }
        ] };
        if (endpoint.includes('/runs/')) {
            const id = Number(endpoint.split('/').pop());
            return { id, workflow_id: 12, head_branch: 'develop', event: 'schedule',
                conclusion: id === 3 ? 'failure' : 'success' };
        }
        throw new Error(`Unexpected API call ${endpoint}`);
    };
    // 核对被下载的运行身份并写入受控锁文件。
    const acquire = (repo, runId, directory) => {
        acquisitions++;
        assert.equal(repo, 'owner/repo');
        assert.equal(runId, expectedRun);
        fs.writeFileSync(path.join(directory, 'windows-toolchain.json'), JSON.stringify(bootstrap));
    };
    // 消费测试发布结果而不写入真实工作流输出。
    const options = { env, request, acquire, publish() {} };
    try {
        assert.equal(select(dir, options).source, 'Windows-validated run 2');
        assert.equal(select(dir, options).run_id, '2');
        assert.equal(acquisitions, 2);
        expired = true;
        // 执行工件选择以验证过期工件拒绝。
        assert.throws(() => select(dir, options), /expired/);
        assert.throws(
            // 执行 API 不可用场景以验证失败传播。
            () => select(dir, { ...options,
            // 注入 API 不可用异常。
            request() { throw new Error('API unavailable'); } }), /API unavailable/);
        // A refresh can recover from expired artifacts, but still cannot publish before full success.
        const refresh = select(dir, { ...options, env: { ...env, GITHUB_EVENT_NAME: 'schedule', GITHUB_REF: 'refs/heads/develop' } });
        assert.equal(refresh.refresh, true);
        assert.equal(acquisitions, 2);
        latestWindowsSucceeded = true;
        expectedRun = 3;
        const recovered = select(dir, options);
        assert.equal(recovered.source, 'Windows-validated run 3');
        assert.equal(recovered.run_id, '3');
        assert.equal(acquisitions, 3);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

// 验证工具刷新严格限定默认分支与指定事件。
test('only weekly or explicit default-branch runs may discover new tool versions', () => {
    for (const event of ['pull_request', 'push', 'workflow_dispatch', 'schedule']) {
        for (const ref of ['refs/pull/8/merge', 'refs/heads/feature']) {
            assert.equal(canRefresh(event, ref, 'develop', 'true'), false);
        }
    }
    assert.equal(canRefresh('push', 'refs/heads/develop', 'develop', 'true'), false);
    assert.equal(canRefresh('workflow_dispatch', 'refs/heads/develop', 'develop', 'false'), false);
    assert.equal(canRefresh('workflow_dispatch', 'refs/heads/develop', 'develop', 'true'), true);
    assert.equal(canRefresh('schedule', 'refs/heads/develop', 'develop', 'false'), true);
});

// 验证取消、其他分支及非刷新事件的工件不受信。
test('unfinished, PR, foreign workflow and non-default-branch records cannot supply Windows tools', () => {
    const good = { workflow_id: 12, head_branch: 'develop', conclusion: 'success', event: 'schedule' };
    assert.equal(trustedRun(good, 12, 'develop'), true);
    for (const change of [{ conclusion: null }, { conclusion: 'cancelled' },
        { event: 'pull_request' }, { event: 'push' }, { workflow_id: 13 }, { head_branch: 'feature' }]) {
        assert.equal(trustedRun({ ...good, ...change }, 12, 'develop'), false);
    }
});

// 验证 Windows 全部通过才可独立批准，不能用 Linux 失败掩盖 Windows 缺项或失败。
test('Linux failure cannot strand successfully validated Windows tools; failed Windows jobs remain blocked', () => {
    const run = { workflow_id: 12, head_branch: 'develop', conclusion: 'failure', event: 'workflow_dispatch' };
    assert.equal(trustedRun(run, 12, 'develop'), true);
    assert.equal(windowsPassed(successfulWindowsJobs), true);
    for (let index = 0; index < successfulWindowsJobs.length; index++) {
        for (const conclusion of ['failure', 'cancelled', 'skipped', null]) {
            const jobs = structuredClone(successfulWindowsJobs);
            jobs[index].conclusion = conclusion;
            assert.equal(windowsPassed(jobs), false);
        }
        // 删除一个必需作业验证缺项失败。
        assert.equal(windowsPassed(successfulWindowsJobs.filter((_, current) => current !== index)), false);
    }
    assert.equal(windowsPassed([...successfulWindowsJobs, successfulWindowsJobs[0]]), false);
});

// 验证归档摘要可校验且不将归档元数据变化误算为编译器变更。
test('snapshot transport identity does not invalidate unchanged dependency ABI inputs', () => {
    const snapshot = { ...bootstrap, nativeSnapshotSha256: 'a'.repeat(64) };
    validate(snapshot);
    assert.equal(identity(snapshot), identity(bootstrap));
    // 拒绝非 SHA256 的快照摘要。
    assert.throws(() => validate({ ...snapshot, nativeSnapshotSha256: 'invalid' }), /snapshot digest/);
});

// 验证工具、编译器或 SDK 版本变化都会改变锁身份。
test('PowerShell patch upgrades and compiler identity changes isolate native cache namespaces', () => {
    const original = identity(bootstrap);
    // 修改工具版本以触发身份变化。
    for (const change of [lock => { lock.tools[2].version = '7.6.6'; },
        // 修改编译器版本以触发身份变化。
        lock => { lock.msvc.compilerVersion = '19.44.35229.0'; },
        // 修改 SDK 版本以触发身份变化。
        lock => { lock.msvc.sdk = '10.0.22621.0'; }]) {
        const candidate = structuredClone(bootstrap);
        change(candidate);
        assert.notEqual(identity(candidate), original);
    }
    assert.equal(identity(structuredClone(bootstrap)), original);
});

// 验证浮动版本、重复工具、非法来源、缺摘要及越界可执行路径被拒绝。
test('malformed versions, duplicate tools, untrusted downloads and missing digests fail closed', () => {
    validate(bootstrap);
    // 注入未固定的工具集版本。
    for (const mutate of [lock => { lock.msvc.toolset = 'latest'; },
        // 注入重复工具身份。
        lock => { lock.tools[2] = lock.tools[0]; },
            // 注入非批准工具来源。
            lock => { lock.tools[0].url = 'https://example.com/tool.zip'; },
        // 移除工具校验和。
        lock => { lock.tools[0].sha512 = ''; },
            // 注入越界可执行文件路径。
            lock => { lock.tools[0].executable = '../tool.exe'; }]) {
        const candidate = structuredClone(bootstrap);
        mutate(candidate);
        // 校验当前变异锁文件，供拒绝断言。
        assert.throws(() => validate(candidate));
    }
});

// 验证 Windows 批准与 Linux 隔离，但完整发布仍需双平台门禁。
test('tool selection precedes restore; cold refresh skips caches and Windows approval preserves full release gates', () => {
    const ci = read('.github/workflows/ci.yml');
    const windows = read('.github/workflows/windows-ci.yml');
    const linux = read('.github/workflows/linux-ci.yml');
    assert.doesNotMatch(ci, /promote-toolchain|ci-toolchain-approved|toolchain_lock|toolchain_run/);
    assert.match(ci, /needs: \[plan, windows, linux\]/);
    assert.match(ci, /needs: full/);
    const server = windows.split('  servers-release:')[1].split('  client-release:')[0];
    assert.ok(server.indexOf('actions/setup-node@') < server.indexOf('node scripts/ci/toolchain.js select'));
    assert.ok(server.indexOf('node scripts/ci/toolchain.js select') < server.indexOf('install-windows-toolchain.ps1'));
    assert.match(server, /CHAT_TOOLCHAIN_LOCK: \$\{\{ steps\.toolchain\.outputs\.lock \}\}/);
    assert.match(server, /CHAT_TOOLCHAIN_RUN: \$\{\{ steps\.toolchain\.outputs\.run_id \}\}/);
    assert.equal((windows.match(/node scripts\/ci\/toolchain.js select/g) || []).length, 1);
    for (const [id, name] of [['static-check', 'Static configuration checks'], ['servers-release', 'Server Release build'],
        ['client-release', 'Qt client Release'], ['varify-release', 'VarifyServer dependency and package check']]) {
        assert.ok(windows.includes(`  ${id}:\n    name: ${name}\n`));
    }
    assert.match(ci, /  windows:\s+needs: plan\s+if: needs\.plan\.outputs\.mode != 'performance'\s+uses: \.\/\.github\/workflows\/windows-ci.yml/);
    assert.ok(windows.indexOf('install-windows-toolchain.ps1') < windows.indexOf('- name: Prepare layered binary cache keys'));
    assert.match(windows, /id: vcpkg-binary-cache\s+if: \$\{\{ !inputs\.refresh_tools \}\}/);
    assert.match(linux, /id: vcpkg-binary-cache\s+if: \$\{\{ !inputs\.cold_build \}\}/);
    const install = read('scripts/ci/install-windows-toolchain.ps1');
    assert.match(install, /VCPKG_FORCE_DOWNLOADED_BINARIES = '1'/);
    assert.match(install, /fetch \$tool\.name --x-stderr-status/);
    assert.match(install, /\$actualVersion -ne \$tool\.version/);
    assert.match(install, /compilerSha256 -ne \$compilerHash/);
    assert.match(install, /FileVersionInfo\]::GetVersionInfo\(\$compiler\)/);
    assert.doesNotMatch(install, /cmd\.exe/);
    assert.match(install, /VCPKG_PLATFORM_TOOLSET_VERSION/);
    assert.ok(install.indexOf('Restore-NativeToolchain') < install.indexOf('$compiler ='));
    assert.match(windows, /name: windows-native-toolchain/);
    assert.match(windows, /run: .\/scripts\/ci\/test-native-toolchain.ps1/);
    const runner = read('scripts/windows-local.ps1');
    assert.equal((runner.match(/\+= @\(Get-CiToolchainArguments\)/g) || []).length, 3);
    assert.match(runner, /\/p:VCToolsVersion=/);
    assert.match(runner, /\/p:WindowsTargetPlatformVersion=/);
});
