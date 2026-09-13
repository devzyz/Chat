'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const systemLibrary = /^\/(?:usr\/)?lib(?:64|\/x86_64-linux-gnu)\/(?:lib(?:stdc\+\+|gcc_s|c|m|pthread|dl|rt)\.so\.[0-9]+|ld-linux-x86-64\.so\.2)$/;
const libraryName = /^lib[A-Za-z0-9_+.-]+\.so(?:\.[0-9]+)*$/;

function dependencies(output) {
    const libraries = new Map();
    for (const line of output.trim().split('\n')) {
        if (/^\s*linux-vdso\.so\.1\s+\(0x[\da-f]+\)\s*$/.test(line)) continue;
        const match = line.match(/^\s*(?:(\S+) => )?(\/\S+)\s+\(0x[\da-f]+\)\s*$/);
        assert.ok(match, 'unresolved message runtime dependency');
        const [, name, resolved] = match;
        if (systemLibrary.test(resolved)) continue;
        assert.ok(name && libraryName.test(name), 'invalid message runtime library');
        assert.ok(!libraries.has(name) || libraries.get(name) === resolved, 'conflicting runtime library');
        libraries.set(name, resolved);
    }
    return libraries;
}

function validateRelocated(output, bundle, names) {
    assert.ok(Array.isArray(names) && names.every((name) => libraryName.test(name)),
        'invalid runtime manifest');
    assert.equal(new Set(names).size, names.length, 'duplicate runtime manifest entry');
    const actual = dependencies(output);
    assert.deepEqual([...actual.keys()].sort(), [...names].sort(), 'runtime manifest mismatch');
    for (const [name, resolved] of actual) {
        assert.equal(resolved, `${path.posix.resolve(bundle)}/${name}`, 'runtime escaped owned bundle');
    }
}

function inspect(binary, bundle) {
    // The explicit app-local search directory applies to indirect dependencies
    // too; never inherit the build runner's or user's library search path.
    return execFileSync('ldd', [binary], { encoding: 'utf8', timeout: 10000,
        env: { ...process.env, LD_LIBRARY_PATH: bundle || '', LD_PRELOAD: '' } });
}

function pack(binary, bundle, installedRoot, name = 'message_commit_integration', probe = ['validation']) {
    assert.match(name, /^[A-Za-z][A-Za-z0-9_]*$/);
    binary = fs.realpathSync(binary);
    installedRoot = fs.realpathSync(installedRoot);
    bundle = path.resolve(bundle);
    assert.ok(!fs.existsSync(bundle), 'message runtime destination must be new');
    const libraries = dependencies(inspect(binary));
    fs.mkdirSync(bundle, { recursive: true });
    fs.copyFileSync(binary, path.join(bundle, name));
    fs.chmodSync(path.join(bundle, name), 0o755);
    for (const [name, source] of libraries) {
        const real = fs.realpathSync(source);
        assert.ok(real.startsWith(installedRoot + path.sep) ||
            /^\/(?:usr\/)?lib\/x86_64-linux-gnu\//.test(real), 'library source outside locked/system roots');
        fs.copyFileSync(real, path.join(bundle, name));
    }
    fs.writeFileSync(path.join(bundle, 'libraries.json'), JSON.stringify([...libraries.keys()].sort()) + '\n');
    verify(bundle, name, probe);
}

function verify(bundle, name = 'message_commit_integration', probe = ['validation']) {
    assert.match(name, /^[A-Za-z][A-Za-z0-9_]*$/);
    bundle = path.resolve(bundle);
    const binary = path.join(bundle, name);
    const names = JSON.parse(fs.readFileSync(path.join(bundle, 'libraries.json'), 'utf8'));
    validateRelocated(inspect(binary, bundle), bundle, names);
    if (probe) execFileSync(binary, probe, { timeout: 10000,
        env: { ...process.env, LD_LIBRARY_PATH: bundle, LD_PRELOAD: '' }, stdio: 'pipe' });
}

if (require.main === module) {
    const [action, ...args] = process.argv.slice(2);
    if (action === 'pack' && args.length === 3) pack(...args);
    else if (action === 'verify' && args.length === 1) verify(...args);
    else throw new Error('messageRuntime requires pack <binary> <new-bundle> <installed-root> or verify <bundle>');
}

module.exports = { dependencies, validateRelocated, pack, verify };
