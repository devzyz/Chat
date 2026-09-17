'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const workflow = name => fs.readFileSync(path.join(__dirname, '../../.github/workflows', name), 'utf8');
const ci = workflow('ci.yml');
const job = (text, name) => text.split(`  ${name}:`)[1]?.split(/\r?\n  [\w-]+:/)[0];

test('develop uses quick regression; master, weekly and manual runs use full regression', () => {
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

test('publication requires master push and all full checks; failed smoke cannot publish', () => {
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

test('cold Windows restore and Linux business steps retain setup and cleanup time', () => {
    const windows = job(workflow('windows-ci.yml'), 'servers-release');
    assert.ok(Number(windows.match(/timeout-minutes: (\d+)/)[1]) >= 174 + 30);
    for (const name of ['disposable-services', 'two-server-contract']) {
        const body = job(workflow('linux-ci.yml'), name);
        assert.ok(Number(body.match(/timeout-minutes: (\d+)/)[1]) >= 20);
    }
    assert.doesNotMatch(workflow('linux-ci.yml'), /phase3dChecks|checks: read/);
});
