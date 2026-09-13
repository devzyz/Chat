'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { aggregate, requiredChecks } = require('./phase3dGate');
const { reportGroups } = require('./phase3dEvidence');
const { writeReports } = require('./serviceReports');
const { createTopology } = require('./twoServerTopology');
const { assess, write } = require('../compatibility/bootstrap');

test('admission binds current-N checks and recovery while keeping absent N-1 non-PASS', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-3d-gate-'));
    const previous = process.env.CHAT_CANDIDATE_SHA;
    const sourceSha = 'a'.repeat(40), candidateSha = 'b'.repeat(40);
    const services = path.join(root, 'services'), phase3c = path.join(root, 'phase3c'), output = path.join(root, 'gate');
    const json = (directory, name, value) => fs.writeFileSync(path.join(directory, name), JSON.stringify(value));
    const groups = reportGroups('3D');
    const cases = groups.flatMap((group, index) => Array.from({ length: group.expected }, (_, offset) => ({
        id: `${group.prefix}${String(offset + (index === 0 ? 6 : 1)).padStart(2, '0')}`,
        name: 'synthetic gate input', pass: true })));
    const topology = createTopology('c'.repeat(32), {
        gate: 30001, status: 30002, varify: 30003, chatA: 30004, rpcA: 30005, chatB: 30006, rpcB: 30007 });
    topology.clients = topology.users.map((user, index) => ({ logical: user.logical, pid: index + 100,
        uid: index + 1, active: true, host: topology.host, port: topology.servers[index].port }));
    topology.recoveredClients = [{ ...topology.clients[0], previousPid: 100, pid: 103 }];
    topology.serverRestarts = ['ChatA', 'ChatB'].map((logical, index) => ({ logical,
        previous: { pid: index + 200, creationTime: '1' }, current: { pid: index + 300, creationTime: '2' },
        advertisedPort: topology.servers[index].port, listenPort: topology.servers[index].port }));
    const restore = () => {
        fs.mkdirSync(services, { recursive: true });
        writeReports(services, '3D', cases, { groups, manifest: 'phase3d-reports.json', level: 'E2E' });
        json(services, 'topology.json', topology);
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) json(services, name, { complete: true });
        json(services, 'teardown.json', { complete: true, processComplete: true, primaryFailure: null });
        json(services, 'fault-relay.json', { complete: true, droppedAck: true, replayedNotification: true, committedId: '81' });
        write(phase3c, assess([[]], sourceSha));
        json(phase3c, 'gate.json', { sourceSha, currentNPass: true, compatibility: 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1' });
        json(phase3c, 'phase3c-reports.json', { sourceSha });
    };
    const inputs = () => ({ services, phase3c, output, sourceSha, candidateSha,
        commit: { sha: sourceSha, parents: [{ sha: candidateSha }] },
        checkPages: [{ check_runs: requiredChecks.map((name, index) => ({ name, head_sha: candidateSha,
            id: index + 1, status: 'completed', conclusion: 'success', html_url: `https://github.com/example/check/${index}` })) }] });
    try {
        process.env.CHAT_CANDIDATE_SHA = sourceSha;
        restore();
        const accepted = aggregate(inputs());
        assert.equal(accepted.currentNPass, true);
        assert.equal(accepted.caseCount, 33);
        assert.equal(accepted.releaseEligible, false);
        assert.equal(accepted.next_route, 'R-00');
        assert.deepEqual(accepted.openRequirements, ['G-017']);
        const matrix = JSON.parse(fs.readFileSync(path.join(output, 'compatibility.json')));
        assert.equal(matrix.entries.length, 5);
        assert.ok(matrix.entries.every(entry => !entry.executed && entry.status === 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1'));
        const wrongParent = inputs(); wrongParent.commit.parents = [];
        assert.equal(aggregate(wrongParent).currentNPass, false);
        const failedCheck = inputs(); failedCheck.checkPages[0].check_runs[0].conclusion = 'failure';
        assert.equal(aggregate(failedCheck).currentNPass, false);
        const staleCheck = inputs(); staleCheck.checkPages[0].check_runs[0].head_sha = sourceSha;
        assert.equal(aggregate(staleCheck).currentNPass, false);
        json(services, 'fault-relay.json', { complete: false });
        assert.equal(aggregate(inputs()).currentNPass, false);
        restore(); topology.serverRestarts[1].current = topology.serverRestarts[1].previous;
        json(services, 'topology.json', topology);
        assert.equal(aggregate(inputs()).currentNPass, false);
        topology.serverRestarts[1].current = { pid: 301, creationTime: '2' };
        restore(); fs.appendFileSync(path.join(services, 'linux_phase3d_recovery.xml'), 'tampered');
        assert.equal(aggregate(inputs()).currentNPass, false);
        restore(); write(phase3c, assess([[{ id: 1, draft: false, prerelease: false }]], sourceSha));
        assert.equal(aggregate(inputs()).currentNPass, false);
        restore(); json(services, 'teardown.json', { complete: true, processComplete: false });
        assert.equal(aggregate(inputs()).currentNPass, false);
    } finally {
        if (previous === undefined) delete process.env.CHAT_CANDIDATE_SHA; else process.env.CHAT_CANDIDATE_SHA = previous;
        fs.rmSync(root, { recursive: true });
    }
});
