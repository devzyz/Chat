'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const { spawnSync } = require('node:child_process');
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

test('Windows builds production servers once and validates registration before all lanes', () => {
    const windows = workflow('windows-ci.yml');
    assert.doesNotMatch(job(windows, 'servers-release'), /-Task BuildServers/);
    assert.equal((windows.match(/-Task CheckTestStructure/g) || []).length, 1);
    for (const [name, task] of [['static-check', 'RunScriptTests'], ['servers-release', 'RunServerTests'],
        ['client-release', 'RunClientTests'], ['varify-release', 'RunVarifyTests']]) {
        const body = job(windows, name);
        assert.match(body, new RegExp(`-Task ${task} -SkipTestStructureCheck`));
        if (name !== 'static-check') assert.match(body, /needs: static-check/);
    }
    const runner = fs.readFileSync(path.join(__dirname, '../../scripts/windows-local.ps1'), 'utf8');
    const server = runner.split('function Run-ServerTests {')[1].split('function ')[0];
    assert.match(server, /\/t:GateServer;StatusServer;ChatServer;ServerUnitTests/);
    assert.match(server, /Assert-RegressionReport/);
});

test('structure-check bypass is rejected outside the prerequisite CI lanes', { skip: process.platform !== 'win32' }, () => {
    for (const [task, githubActions] of [['RunVarifyTests', 'false'], ['CheckTestStructure', 'true']]) {
        const result = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            path.join(__dirname, '../../scripts/windows-local.ps1'), '-Task', task, '-SkipTestStructureCheck'],
        { encoding: 'utf8', env: { ...process.env, GITHUB_ACTIONS: githubActions }, timeout: 30000 });
        assert.notEqual(result.status, 0);
        assert.match(result.stdout + result.stderr, /requires a CI test lane after CheckTestStructure/);
    }
});

test('quick regression omits packages while every full lane retains all release inputs', () => {
    const windows = workflow('windows-ci.yml');
    const expression = windows.match(/BUILD_PACKAGES: \$\{\{ (.+) \}\}/)[1];
    const packageEnabled = new Function('github', `return ${expression};`);
    for (const [event_name, ref, base_ref, expected] of [
        ['pull_request', 'refs/pull/7/merge', 'develop', false],
        ['push', 'refs/heads/develop', '', false],
        ['pull_request', 'refs/pull/7/merge', 'master', true],
        ['push', 'refs/heads/master', '', true],
        ['schedule', 'refs/heads/develop', '', true],
        ['workflow_dispatch', 'refs/heads/develop', '', true]
    ]) assert.equal(packageEnabled({ event_name, ref, base_ref }), expected);
    for (const name of ['Create independent server ZIP packages', 'Upload server release directories',
        'Deploy and package Qt client', 'Upload Qt client ZIP', 'Package VarifyServer', 'Upload VarifyServer ZIP']) {
        const step = windows.split(`- name: ${name}`)[1]?.split(/\r?\n      - /)[0];
        assert.ok(step, name);
        assert.match(step, /if: env\.BUILD_PACKAGES == 'true'/);
    }
    assert.equal((workflow('linux-ci.yml').match(/tests\/services\/phase3dEvidence\.test\.js/g) || []).length, 1);
});

test('Linux retains PASS validation without the retired compatibility selector or unused output', () => {
    const linux = workflow('linux-ci.yml');
    const runner = fs.readFileSync(path.join(__dirname, '../../scripts/linux-ci.sh'), 'utf8');
    assert.doesNotMatch(runner, /3C-08|CHAT_RELEASE_INVENTORY|compatibility\/bootstrap/);
    assert.doesNotMatch(linux, /preflight-status|outputs\.status|echo 'status=PASS'/);
    assert.ok(linux.includes("jq -e '.status == \"PASS\"' out/phase3c/preflight/linux-preflight.json"));
    assert.match(job(linux, 'downstream-contract'), /verify-service-reports\.js/);
    assert.match(job(ci, 'release'), /needs: full/);
});
