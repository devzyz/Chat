'use strict';
const fs = require('node:fs');
const { execFileSync } = require('node:child_process');
const { performance } = require('node:perf_hooks');
const { setTimeout: delay } = require('node:timers/promises');
const { requiredChecks, selectCheck } = require('./phase3dGate');

// Cover Windows static checks, the cold-build budget and cross-workflow scheduling.
async function waitForChecks({ candidateSha, read, timeoutMs = 260 * 60 * 1000,
    intervalMs = 30000, now = () => performance.now(), delay: sleep = delay }) {
    const deadline = now() + timeoutMs;
    while (true) {
        const pages = await read();
        const checks = pages.flatMap(page => page.check_runs);
        const selected = requiredChecks.map(name => selectCheck(checks, name, candidateSha));
        if (selected.some(check => check?.status === 'completed' && check.conclusion !== 'success')) {
            throw new Error('required-check-failed');
        }
        if (selected.every(check => check?.status === 'completed' && check.conclusion === 'success')) return pages;
        const remaining = deadline - now();
        if (remaining <= 0) throw new Error('required-check-timeout');
        await sleep(Math.min(intervalMs, remaining));
    }
}

module.exports = { waitForChecks };
if (require.main === module) {
    const [repository, candidateSha, output] = process.argv.slice(2);
    (async () => {
        if (!/^[\w.-]+\/[\w.-]+$/.test(repository) || !/^[a-f0-9]{40}$/.test(candidateSha)) {
            throw new Error('invalid-check-identity');
        }
        // Keep the last snapshot on failure for the admission gate's fail-closed evidence.
        await waitForChecks({ candidateSha, read: () => {
            const bytes = execFileSync('gh', ['api', '--paginate', '--slurp',
                `repos/${repository}/commits/${candidateSha}/check-runs?per_page=100`],
            { timeout: 30000, maxBuffer: 8 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'] });
            const pages = JSON.parse(bytes);
            fs.writeFileSync(output, bytes);
            return pages;
        } });
    })().catch(() => {
        process.stderr.write('Required candidate checks failed, timed out, or could not be read\n');
        process.exitCode = 1;
    });
}
