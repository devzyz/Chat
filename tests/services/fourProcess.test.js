'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { reportGroups, writeReports } = require('./serviceReports');
const { reserve } = require('./fourProcessCases');

test('four-process reports cannot be replaced by passed adapter cases', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-four-report-'));
    try {
        const groups = reportGroups('3C-07');
        assert.equal(groups.find(value => value.prefix === 'T10-4PROC-').expected, 18);
        const cases = groups.filter(value => value.prefix !== 'T10-4PROC-').flatMap(group =>
            Array.from({ length: group.expected }, (_, index) => ({ id: group.prefix + String(index + 1).padStart(2, '0'), pass: true })));
        writeReports(root, '3C-07', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_four_process.xml'), 'utf8'), /<failure/);
        for (let index = 1; index <= 18; index++) cases.push({ id: `T10-4PROC-${String(index).padStart(2, '0')}`, pass: index !== 18 });
        writeReports(root, '3C-07', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_four_process.xml'), 'utf8'), /failures="1"/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('application port leases are unique until explicitly released', async () => {
    const first = await reserve();
    const second = await reserve();
    try { assert.notEqual(first.port, second.port); }
    finally { await first.release(); await second.release(); }
});


test('process shutdown reports retain safe failure identity and reject incomplete cleanup', async () => {
    const { stop } = require('./fourProcessCases');
    const { caseDiagnostic } = require('./dependencyCoordinator');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-stop-report-'));
    try {
        const report = path.join(root, 'report.json');
        const owned = () => ({ name: 'ChatServer', report, child: { kill() {} }, exited: Promise.resolve(0) });
        fs.writeFileSync(report, JSON.stringify({ complete: true, escalated: false, exitCode: 139 }));
        await assert.rejects(stop(owned()), error => {
            assert.deepEqual(caseDiagnostic(error), { stage: 'stop-ChatServer', category: 'exit-139' });
            return true;
        });
        fs.writeFileSync(report, JSON.stringify({ complete: false, escalated: true, exitCode: 137 }));
        await assert.rejects(stop(owned()), /stop-escalated/);
        fs.writeFileSync(report, JSON.stringify({ complete: true, escalated: false, exitCode: 0 }));
        const successful = owned();
        await stop(successful);
        assert.equal(successful.stopped, true);
        await stop(successful);
        assert.equal(caseDiagnostic(new Error('FourProcess:ChatServer:secret-token')), undefined);
        assert.equal(caseDiagnostic(new Error('FourProcess:private-account:exit-1')), undefined);
    } finally { fs.rmSync(root, { recursive: true }); }
});
