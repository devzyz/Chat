'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { assertConnectedClients } = require('./twoServerTopology');

const groups = [{ file: 'linux_phase3d_contract.xml', prefix: 'E03-CONTRACT-', expected: 7 }];
const ids = Array.from({ length: 7 }, (_, index) => `E03-CONTRACT-${String(index + 6).padStart(2, '0')}`);
function validate(root, sourceSha) {
    const read = name => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
    assert.match(sourceSha || '', /^[a-f0-9]{40}$/);
    const manifest = read('phase3d-reports.json');
    assert.equal(manifest.format, 1);
    assert.equal(manifest.sourceSha, sourceSha);
    assert.equal(manifest.selector, '3D-00');
    assert.equal(manifest.reports.length, 1);
    const report = manifest.reports[0];
    assert.equal(report.file, groups[0].file);
    assert.equal(report.expected, 7);
    assert.deepEqual(report.cases.map(value => value.id).sort(), ids);
    assert.ok(report.cases.every(value => value.pass === true));
    const xml = fs.readFileSync(path.join(root, report.file), 'utf8');
    assert.equal(createHash('sha256').update(xml).digest('hex'), report.sha256);
    assert.equal((xml.match(/<testcase\b/g) || []).length, 7);
    assert.ok(!/<(?:failure|error|skipped)\b/.test(xml));
    for (const id of ids) assert.equal(xml.split(`name="${id} `).length, 2);
    const topology = read('topology.json');
    assertConnectedClients(topology, topology.clients);
    for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) {
        assert.equal(read(name).complete, true);
    }
}
module.exports = { groups, validate };
