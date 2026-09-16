const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { test } = require('node:test');

const script = path.resolve(__dirname, '../../scripts/ci/vcpkgBinaryCache.js');

test('Linux binary cache path is bound on the runner before dependency restoration', () => {
    const workflow = fs.readFileSync(path.resolve(__dirname, '../../.github/workflows/linux-ci.yml'), 'utf8');
    const preflight = workflow.slice(workflow.indexOf('  linux-preflight:'));
    const jobEnv = preflight.slice(preflight.indexOf('    env:'), preflight.indexOf('    steps:'));
    // GitHub cannot resolve runner context in jobs.<id>.env; YAML parsing alone cannot catch this.
    assert.doesNotMatch(jobEnv, /\$\{\{\s*runner\./);
    const binding = preflight.indexOf('VCPKG_BINARY_SOURCES=clear;files,$RUNNER_TEMP/vcpkg-binary-cache,readwrite');
    assert.ok(binding > 0 && binding < preflight.indexOf('- name: Run fail-closed Linux preflight'));
    assert.match(preflight.slice(binding).split('\n')[0], /GITHUB_ENV/);
});

test('stale restored cache is refreshed once; a warm run does not upload identical archives', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-cache-test-'));
    try {
        const cache = path.join(root, 'archives');
        const output = path.join(root, 'output');
        const summary = path.join(root, 'summary');
        const log = path.join(root, 'restore.log');
        const env = { ...process.env, RUNNER_TEMP: root, RUNNER_OS: 'Windows', RUNNER_ARCH: 'X64',
            ImageOS: 'win22', ImageVersion: 'fixture-1', GITHUB_RUN_ID: '100', GITHUB_RUN_ATTEMPT: '1',
            GITHUB_OUTPUT: output, GITHUB_STEP_SUMMARY: summary };
        function run(command, overrides = {}) {
            fs.writeFileSync(output, '');
            const result = spawnSync(process.execPath, [script, command, cache,
                command === 'prepare' ? 'x64-windows-chat-release' : log],
            { env: { ...env, ...overrides }, encoding: 'utf8' });
            assert.equal(result.status, 0, result.stderr);
            return Object.fromEntries(fs.readFileSync(output, 'utf8').trim().split('\n')
                .filter(Boolean).map(line => line.split('=')));
        }
        const first = run('prepare');
        assert.ok(first.key.startsWith(first.prefix));
        const second = run('prepare', { GITHUB_RUN_ID: '101' });
        assert.equal(second.prefix, first.prefix);
        assert.notEqual(second.key, first.key, 'a refresh must not overwrite an immutable key');
        assert.notEqual(run('prepare', { ImageVersion: 'fixture-2' }).prefix, first.prefix);
        assert.notEqual(run('prepare', { RUNNER_OS: 'Linux' }).prefix, first.prefix);
        assert.notEqual(run('prepare', { GITHUB_RUN_ATTEMPT: '2' }).key, first.key);

        fs.mkdirSync(path.join(cache, 'aa'));
        fs.writeFileSync(path.join(cache, 'aa', 'old-abi.zip'), 'old compiled package');
        run('snapshot'); // Simulate a successful GitHub restore containing unusable old ABIs.
        fs.writeFileSync(path.join(cache, 'aa', 'new-abi.zip'), 'new compiled package');
        fs.writeFileSync(log, 'Restored 0 package(s) from archives\nBuilding grpc:x64-windows-chat-release@1...\n');
        const cold = run('report');
        assert.equal(cold.changed, 'true');
        assert.equal(cold.archives, '2');
        assert.match(fs.readFileSync(summary, 'utf8'), /Restored packages: 0/);

        run('snapshot'); // Next run has downloaded the updated archive set.
        fs.writeFileSync(log, 'Restored 2 package(s) from archives\n');
        const warm = run('report');
        assert.equal(warm.changed, 'false');
        assert.match(fs.readFileSync(summary, 'utf8'), /Restored packages: 2/);
        fs.rmSync(path.join(cache, 'aa', 'new-abi.zip'));
        assert.equal(run('report').changed, 'true');
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test('empty archives do not request a save and a missing snapshot fails', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-cache-empty-'));
    try {
        const cache = path.join(root, 'archives');
        const output = path.join(root, 'output');
        fs.mkdirSync(cache);
        const env = { ...process.env, GITHUB_OUTPUT: output };
        const run = command => spawnSync(process.execPath, [script, command, cache], { env, encoding: 'utf8' });
        assert.notEqual(run('report').status, 0);
        assert.equal(run('snapshot').status, 0);
        assert.equal(run('report').status, 0);
        assert.match(fs.readFileSync(output, 'utf8'), /changed=false/);
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});
