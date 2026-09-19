const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { test } = require('node:test');
const { validate, identity, canRefresh, trustedRun, select } = require('../../scripts/ci/toolchain');
const bootstrap = require('../../scripts/ci/windows-toolchain.json');
const root = path.resolve(__dirname, '../..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');

test('selection retains previous approved tools after a failed refresh and rejects expired or unavailable records', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-toolchain-'));
    const env = { GITHUB_REPOSITORY: 'owner/repo', GITHUB_EVENT_NAME: 'pull_request', GITHUB_REF: 'refs/pull/8/merge' };
    let expired = false;
    let acquisitions = 0;
    const request = endpoint => {
        if (endpoint === 'repos/owner/repo') return { default_branch: 'develop' };
        if (endpoint.endsWith('/workflows/ci.yml')) return { id: 12 };
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
    const acquire = (repo, runId, directory) => {
        acquisitions++;
        assert.equal(repo, 'owner/repo');
        assert.equal(runId, 2);
        fs.writeFileSync(path.join(directory, 'windows-toolchain.json'), JSON.stringify(bootstrap));
    };
    const options = { env, request, acquire, publish() {} };
    try {
        assert.equal(select(dir, options).source, 'approved run 2');
        assert.equal(acquisitions, 1);
        expired = true;
        assert.throws(() => select(dir, options), /expired/);
        assert.throws(() => select(dir, { ...options, request() { throw new Error('API unavailable'); } }), /API unavailable/);
        // A refresh can recover from expired artifacts, but still cannot publish before full success.
        const refresh = select(dir, { ...options, env: { ...env, GITHUB_EVENT_NAME: 'schedule', GITHUB_REF: 'refs/heads/develop' } });
        assert.equal(refresh.refresh, true);
        assert.equal(acquisitions, 1);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

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

test('failed, unfinished, PR, foreign workflow and non-default-branch records cannot be promoted', () => {
    const good = { workflow_id: 12, head_branch: 'develop', conclusion: 'success', event: 'schedule' };
    assert.equal(trustedRun(good, 12, 'develop'), true);
    for (const change of [{ conclusion: 'failure' }, { conclusion: null }, { conclusion: 'cancelled' },
        { event: 'pull_request' }, { event: 'push' }, { workflow_id: 13 }, { head_branch: 'feature' }]) {
        assert.equal(trustedRun({ ...good, ...change }, 12, 'develop'), false);
    }
});

test('PowerShell patch upgrades and compiler identity changes isolate native cache namespaces', () => {
    const original = identity(bootstrap);
    for (const change of [lock => { lock.tools[2].version = '7.6.6'; },
        lock => { lock.msvc.compilerVersion = '19.44.35229.0'; },
        lock => { lock.msvc.sdk = '10.0.22621.0'; }]) {
        const candidate = structuredClone(bootstrap);
        change(candidate);
        assert.notEqual(identity(candidate), original);
    }
    assert.equal(identity(structuredClone(bootstrap)), original);
});

test('malformed versions, duplicate tools, untrusted downloads and missing digests fail closed', () => {
    validate(bootstrap);
    for (const mutate of [lock => { lock.msvc.toolset = 'latest'; },
        lock => { lock.tools[2] = lock.tools[0]; }, lock => { lock.tools[0].url = 'https://example.com/tool.zip'; },
        lock => { lock.tools[0].sha512 = ''; }, lock => { lock.tools[0].executable = '../tool.exe'; }]) {
        const candidate = structuredClone(bootstrap);
        mutate(candidate);
        assert.throws(() => validate(candidate));
    }
});

test('tool selection precedes restore; cold refresh skips caches and promotion requires full regression', () => {
    const ci = read('.github/workflows/ci.yml');
    const windows = read('.github/workflows/windows-ci.yml');
    const linux = read('.github/workflows/linux-ci.yml');
    assert.match(ci, /needs: \[toolchain, full\]/);
    assert.match(ci, /needs\.toolchain\.outputs\.refresh == 'true' && needs\.full\.result == 'success'/);
    assert.match(ci, /name: ci-toolchain-approved/);
    assert.ok(windows.indexOf('install-windows-toolchain.ps1') < windows.indexOf('- name: Prepare layered binary cache keys'));
    assert.match(windows, /id: vcpkg-binary-cache\s+if: \$\{\{ !inputs\.refresh_tools \}\}/);
    assert.match(linux, /id: vcpkg-binary-cache\s+if: \$\{\{ !inputs\.cold_build \}\}/);
    const install = read('scripts/ci/install-windows-toolchain.ps1');
    assert.match(install, /VCPKG_FORCE_DOWNLOADED_BINARIES = '1'/);
    assert.match(install, /fetch \$tool\.name --x-stderr-status/);
    assert.match(install, /\$actualVersion -ne \$tool\.version/);
    assert.match(install, /compilerSha256 -ne \$compilerHash/);
    assert.match(install, /VCPKG_PLATFORM_TOOLSET_VERSION/);
    const runner = read('scripts/windows-local.ps1');
    assert.equal((runner.match(/\+= @\(Get-CiToolchainArguments\)/g) || []).length, 3);
    assert.match(runner, /\/p:VCToolsVersion=/);
    assert.match(runner, /\/p:WindowsTargetPlatformVersion=/);
});
