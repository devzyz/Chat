'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { groups, validate } = require('./phase3dEvidence');
const { writeReports } = require('./serviceReports');
const { createTopology } = require('./twoServerTopology');

test('foundation evidence binds exact cases, bytes, SHA, distinct clients and cleanup', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-3d-evidence-'));
    const previous = process.env.CHAT_CANDIDATE_SHA;
    const sha = 'a'.repeat(40);
    const write = (name, value) => fs.writeFileSync(path.join(root, name), JSON.stringify(value));
    const cases = Array.from({ length: 7 }, (_, index) => ({ id: `E03-CONTRACT-${String(index + 6).padStart(2, '0')}`,
        name: 'synthetic validator input', pass: true }));
    const topology = createTopology('b'.repeat(32), {
        gate: 30001, status: 30002, varify: 30003, chatA: 30004, rpcA: 30005, chatB: 30006, rpcB: 30007 });
    topology.clients = topology.users.map((user, index) => ({ logical: user.logical, pid: index + 100,
        uid: index + 1, active: true, host: topology.host, port: topology.servers[index].port }));
    const restore = () => {
        writeReports(root, '3D-00', cases, { groups, manifest: 'phase3d-reports.json', level: 'E2E' });
        write('topology.json', topology);
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) write(name, { complete: true });
    };
    try {
        process.env.CHAT_CANDIDATE_SHA = sha;
        restore(); validate(root, sha);
        const finalize = () => spawnSync(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, CHAT_SERVICE_SELECTOR: '3D-00', GITHUB_ACTIONS: 'false' }, timeout: 3000 });
        write('teardown.json', { complete: true });
        assert.equal(finalize().status, 0);
        fs.unlinkSync(path.join(root, 'phase3d-reports.json'));
        assert.equal(finalize().status, 1);
        assert.match(fs.readFileSync(path.join(root, groups[0].file), 'utf8'), /<failure/);
        restore();
        assert.throws(() => validate(root, 'c'.repeat(40)));
        const reportPath = path.join(root, groups[0].file);
        fs.appendFileSync(reportPath, 'changed bytes');
        assert.throws(() => validate(root, sha));
        restore();
        writeReports(root, '3D-00', cases.slice(1), { groups, manifest: 'phase3d-reports.json' });
        assert.throws(() => validate(root, sha));
        restore();
        const wrongIds = cases.map(value => ({ ...value })); wrongIds[0].id = 'E03-CONTRACT-99';
        writeReports(root, '3D-00', wrongIds, { groups, manifest: 'phase3d-reports.json' });
        assert.throws(() => validate(root, sha));
        restore(); write('topology.json', { ...topology, clients: [topology.clients[0], topology.clients[0]] });
        assert.throws(() => validate(root, sha));
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) {
            restore(); write(name, { complete: false }); assert.throws(() => validate(root, sha));
        }
    } finally {
        if (previous === undefined) delete process.env.CHAT_CANDIDATE_SHA;
        else process.env.CHAT_CANDIDATE_SHA = previous;
        fs.rmSync(root, { recursive: true });
    }
});
