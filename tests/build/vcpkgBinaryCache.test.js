const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { test } = require('node:test');

const script = path.resolve(__dirname, '../../scripts/ci/vcpkgBinaryCache.js');

test('dependency edits change preference but retain a platform-specific fallback', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-cache-inputs-'));
    try {
        const fixture = path.join(root, 'scripts/ci/vcpkgBinaryCache.js');
        fs.mkdirSync(path.dirname(fixture), { recursive: true });
        fs.copyFileSync(script, fixture);
        fs.mkdirSync(path.join(root, 'triplets'));
        fs.writeFileSync(path.join(root, 'triplets/x64-linux-chat-release.cmake'), 'release');
        fs.writeFileSync(path.join(root, 'scripts/ci/linux-tool-assets.json'), '{}');
        const manifest = path.join(root, 'vcpkg.json');
        fs.writeFileSync(manifest, '{"dependencies":["grpc"]}');
        const run = () => {
            const result = spawnSync(process.execPath,
                [fixture, 'prepare', path.join(root, 'archives'), 'x64-linux-chat-release'],
                { encoding: 'utf8', env: { ...process.env, RUNNER_OS: 'Linux', RUNNER_ARCH: 'X64',
                    ImageOS: 'ubuntu24', GITHUB_RUN_ID: '100', GITHUB_RUN_ATTEMPT: '1', GITHUB_OUTPUT: '' } });
            assert.equal(result.status, 0, result.stderr);
            return Object.fromEntries(result.stdout.trim().split('\n').map(line => line.split('=')));
        };
        const original = run();
        fs.writeFileSync(manifest, '{"dependencies":["grpc","jsoncpp"]}');
        const updated = run();
        assert.notEqual(updated.prefix, original.prefix);
        assert.equal(updated.fallback, original.fallback);
        assert.match(updated.fallback, /Linux-X64-ubuntu24-x64-linux-chat-release-x64-linux-chat-release-/);
        assert.match(updated.legacy, /^vcpkg-binary-v2-Linux-X64-/);
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

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
        assert.equal(run('prepare', { ImageVersion: 'fixture-2' }).prefix, first.prefix);
        assert.notEqual(run('prepare', { ImageOS: 'win25' }).fallback, first.fallback);
        assert.ok(first.prefix.startsWith(first.fallback));
        assert.match(first.legacy, /^vcpkg-binary-v2-Windows-X64-/);
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
        fs.writeFileSync(log, 'Restored 2 package(s) from archives\nAll requested installations completed successfully in: 1.5 s\n');
        const warm = run('report');
        assert.equal(warm.changed, 'false');
        assert.match(fs.readFileSync(summary, 'utf8'), /Dependency installation time: 1.5 s/);
        assert.match(fs.readFileSync(summary, 'utf8'), /Save reason: unchanged/);
        assert.match(fs.readFileSync(summary, 'utf8'), /Restored packages: 2/);
        const migration = { CACHE_RESTORED_KEY: `${first.legacy}100-1`, CACHE_PRIMARY_PREFIX: first.prefix };
        assert.equal(run('report', migration).changed, 'true', 'legacy archives must seed the new namespace');
        assert.equal(run('report', { ...migration, CACHE_RESTORED_KEY: first.key }).changed, 'false');
        assert.equal(run('report', { ...migration,
            CACHE_RESTORED_KEY: `${first.fallback}older-manifest-100-1` }).changed, 'true');
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
test('both workflows restore layered keys and save before business builds', () => {
    for (const platform of ['windows', 'linux']) {
        const workflow = fs.readFileSync(path.resolve(__dirname, `../../.github/workflows/${platform}-ci.yml`), 'utf8');
        assert.match(workflow, /restore-keys: \|\s+\$\{\{ steps.binary-cache-key.outputs.prefix \}\}\s+\$\{\{ steps.binary-cache-key.outputs.fallback \}\}\s+\$\{\{ steps.binary-cache-key.outputs.legacy \}\}/);
        assert.match(workflow, /CACHE_RESTORED_KEY: \$\{\{ steps.vcpkg-binary-cache.outputs.cache-matched-key \}\}/);
        assert.match(workflow, /CACHE_PRIMARY_PREFIX: \$\{\{ steps.binary-cache-key.outputs.prefix \}\}/);
        const save = workflow.indexOf('- name: Save new binary packages');
        const build = workflow.indexOf(platform === 'windows' ? '- name: Build GateServer' : '- name: Run fail-closed Linux preflight');
        assert.ok(save > workflow.indexOf('id: dependency-restore') && save < build);
        assert.match(workflow.slice(save, build), /if: success\(\) && steps.binary-cache-after.outputs.changed == 'true'/);
        assert.match(workflow, /share\/\*\/vcpkg_abi_info.txt/);
        assert.match(workflow, /include-hidden-files: true/);
    }
});
