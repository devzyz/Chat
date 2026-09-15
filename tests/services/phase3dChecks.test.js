'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { waitForChecks } = require('./phase3dChecks');
const { requiredChecks } = require('./phase3dGate');
const candidateSha = 'b'.repeat(40);
const pages = () => [{ check_runs: requiredChecks.map((name, id) => ({
    name, id, head_sha: candidateSha, status: 'completed', conclusion: 'success' })) }];

test('waits for the same candidate without rerunning pending builds', async () => {
    let clock = 0, calls = 0;
    const complete = pages();
    const pending = pages();
    pending[0].check_runs[1].status = 'in_progress';
    pending[0].check_runs[1].conclusion = null;
    const result = await waitForChecks({ candidateSha, timeoutMs: 100, intervalMs: 10,
        now: () => clock, delay: async ms => { clock += ms; },
        read: () => ++calls === 1 ? pending : complete });
    assert.deepEqual(result, complete);
    assert.equal(calls, 2);
});

test('fails closed for terminal failures, missing checks and a newer pending check', async () => {
    for (const conclusion of ['failure', 'cancelled', 'skipped', 'timed_out']) {
        const failed = pages(); failed[0].check_runs[1].conclusion = conclusion;
        await assert.rejects(waitForChecks({ candidateSha, read: () => failed }), /required-check-failed/);
    }
    for (const mutate of [
        input => input[0].check_runs.pop(),
        input => { input[0].check_runs[0].head_sha = 'a'.repeat(40); },
        input => input.push({ check_runs: [{ ...input[0].check_runs[1], id: 100,
            status: 'queued', conclusion: null }] })
    ]) {
        let clock = 0;
        const input = pages(); mutate(input);
        await assert.rejects(waitForChecks({ candidateSha, read: () => input,
            timeoutMs: 20, intervalMs: 10, now: () => clock,
            delay: async ms => { clock += ms; } }), /required-check-timeout/);
        assert.equal(clock, 20);
    }
});

test('API errors propagate without accepting an older successful snapshot', async () => {
    await assert.rejects(waitForChecks({ candidateSha, read: () => { throw new Error('api-unavailable'); } }),
        /api-unavailable/);
});
