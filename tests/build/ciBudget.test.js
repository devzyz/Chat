'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const { waitForChecks } = require('../services/phase3dChecks');
const { requiredChecks } = require('../services/phase3dGate');

function jobMinutes(file, job) {
    const text = fs.readFileSync(path.resolve(__dirname, '../../.github/workflows', file), 'utf8');
    const body = text.split(`  ${job}:`)[1]?.split(/\r?\n  [\w-]+:/)[0];
    assert.ok(body, `Missing job ${job}`);
    return Number(body.match(/^    timeout-minutes: (\d+)/m)?.[1]);
}

test('Windows budget covers the observed cold restore plus build, tests and packaging', () => {
    // Run 35098718644: restore took 174 minutes; the 180-minute job killed test compilation.
    assert.ok(jobMinutes('windows-ci.yml', 'servers-release') >= 174 + 30);
});

test('admission waits through the Windows budget plus scheduling margin', async () => {
    const candidateSha = 'b'.repeat(40);
    const completeAt = (jobMinutes('windows-ci.yml', 'static-check')
        + jobMinutes('windows-ci.yml', 'servers-release') + 5) * 60000;
    let clock = 0;
    const result = await waitForChecks({ candidateSha,
        now: () => clock, delay: async ms => { clock += ms; },
        read: () => [{ check_runs: requiredChecks.map((name, id) => ({
            name, id, head_sha: candidateSha,
            status: clock >= completeAt ? 'completed' : 'in_progress',
            conclusion: clock >= completeAt ? 'success' : null
        })) }]
    });
    assert.equal(result[0].check_runs[0].conclusion, 'success');
});

test('admission job retains time to publish evidence after its polling deadline', async () => {
    let clock = 0;
    await assert.rejects(waitForChecks({ candidateSha: 'b'.repeat(40), read: () => [],
        now: () => clock, delay: async ms => { clock += ms; }
    }), /required-check-timeout/);
    assert.ok(jobMinutes('linux-ci.yml', 'phase3d-release-admission') * 60000 >= clock + 10 * 60000);
});
