'use strict';

const path = require('node:path');
const { execFileSync } = require('node:child_process');

function verifyDependencies(output, bundle, requireHiredis = false) {
    const root = path.posix.resolve(bundle);
    let hasHiredis = false;
    for (const line of output.trim().split('\n')) {
        if (/^\s*linux-vdso\.so\.1\s+\(0x[\da-f]+\)\s*$/.test(line)) continue;
        const match = line.match(/^\s*(?:(\S+) => )?(\/\S+)\s+\(0x[\da-f]+\)\s*$/);
        if (!match) throw new Error('Unresolved or unsupported runtime dependency');
        const [, name, resolved] = match;
        if (requireHiredis && name === 'libhiredis.so.1' && resolved === `${root}/libhiredis.so.1`) {
            hasHiredis = true;
            continue;
        }
        if (!/^\/(?:usr\/)?lib(?:64|\/x86_64-linux-gnu)\/(?:lib(?:stdc\+\+|gcc_s|c|m|pthread|dl|rt)\.so\.[0-9]+|ld-linux-x86-64\.so\.2)$/.test(resolved)) {
            throw new Error('Runtime dependency escapes the bundle/system allowlist');
        }
    }
    if (requireHiredis && !hasHiredis) throw new Error('Bundled hiredis runtime is missing');
}

if (require.main === module) {
    const bundle = path.resolve(process.argv[2]);
    for (const [binary, hiredis] of [['service_run', false], ['redis_adapter_integration', true]]) {
        const output = execFileSync('ldd', [path.join(bundle, binary)], {
            encoding: 'utf8', timeout: 10000, env: { ...process.env, LD_LIBRARY_PATH: '', LD_PRELOAD: '' }
        });
        verifyDependencies(output, bundle, hiredis);
    }
}

module.exports = { verifyDependencies };
