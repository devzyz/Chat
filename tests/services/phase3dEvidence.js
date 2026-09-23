'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { assertConnectedClients } = require('./twoServerTopology');

const groups = [{ file: 'linux_phase3d_contract.xml', prefix: 'E03-CONTRACT-', expected: 7 }];
/** 按受支持的 Phase 3D 选择器返回精确报告组与预期数量。 */
function reportGroups(selector = '3D-00') {
    assert.ok(['3D-00', '3D-01', '3D-02', '3D-03-history', '3D-03', '3D'].includes(selector), 'unimplemented Phase 3D selector');
    const selected = [...groups];
    if (selector !== '3D-00') selected.push({ file: 'linux_phase3d_journey.xml', prefix: 'E03-JOURNEY-', expected: 7 });
    if (['3D-02', '3D-03-history', '3D-03', '3D'].includes(selector)) selected.push({ file: 'linux_phase3d_messaging.xml', prefix: 'E03-XMSG-', expected: 8 });
    if (['3D-03-history', '3D-03', '3D'].includes(selector)) selected.push({ file: 'linux_phase3d_recovery.xml', prefix: 'E03-RECOVER-', expected: selector === '3D-03-history' ? 4 : 11 });
    return selected;
}
/** 核对证据格式、源码 SHA、报告 Test ID 及通过状态，拒绝不完整或串用证据。 */
function validate(root, sourceSha, selector = '3D-00') {
    const read = /** 从所属证据根目录读取指定 JSON，缺失或损坏时抛异常。 */ name => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
    assert.match(sourceSha || '', /^[a-f0-9]{40}$/);
    const manifest = read('phase3d-reports.json');
    assert.equal(manifest.format, 1);
    assert.equal(manifest.sourceSha, sourceSha);
    assert.equal(manifest.selector, selector);
    const expectedGroups = reportGroups(selector);
    assert.equal(manifest.reports.length, expectedGroups.length);
    for (const [index, group] of expectedGroups.entries()) {
        const ids = Array.from({ length: group.expected }, /** 根据组编号生成连续且固定宽度的预期 Test ID。 */ (_, offset) =>
            `${group.prefix}${String(offset + (index === 0 ? 6 : 1)).padStart(2, '0')}`);
        const report = manifest.reports[index];
        assert.equal(report.file, group.file);
        assert.equal(report.expected, group.expected);
        assert.deepEqual(report.cases.map(/** 提取实际用例标识供精确集合核对。 */ value => value.id).sort(), ids);
        assert.ok(report.cases.every(/** 检查记录明确标为成功，不把缺失结果视为通过。 */ value => value.pass === true));
        const xml = fs.readFileSync(path.join(root, report.file), 'utf8');
        assert.equal(createHash('sha256').update(xml).digest('hex'), report.sha256);
        assert.equal((xml.match(/<testcase\b/g) || []).length, group.expected);
        assert.ok(!/<(?:failure|error|skipped)\b/.test(xml));
        for (const id of ids) assert.equal(xml.split(`name="${id} `).length, 2);
    }
    const topology = read('topology.json');
    assertConnectedClients(topology, topology.clients);
    if (['3D-03-history', '3D-03', '3D'].includes(selector)) {
        assert.equal(topology.recoveredClients?.length, 1);
        const recovered = topology.recoveredClients[0];
        assert.equal(recovered.previousPid, topology.clients[0].pid);
        assert.notEqual(recovered.pid, recovered.previousPid);
        assert.equal(recovered.uid, topology.clients[0].uid);
        assertConnectedClients(topology, [recovered, topology.clients[1]]);
    }
    if (['3D-03', '3D'].includes(selector)) {
        const relay = read('fault-relay.json');
        assert.equal(relay.complete, true);
        assert.equal(relay.droppedAck, true);
        assert.equal(relay.replayedNotification, true);
        assert.match(relay.committedId, /^[1-9][0-9]*$/);
        assert.deepEqual(topology.serverRestarts?.map(/** 提取清理记录的逻辑资源名供归属核对。 */ value => value.logical), ['ChatA', 'ChatB']);
        for (const [index, restart] of topology.serverRestarts.entries()) {
            for (const identity of [restart.previous, restart.current]) {
                assert.ok(Number.isSafeInteger(identity.pid) && identity.pid > 0);
                assert.match(identity.creationTime, /^[0-9]+$/);
            }
            assert.ok(restart.previous.pid !== restart.current.pid || restart.previous.creationTime !== restart.current.creationTime);
            assert.equal(restart.advertisedPort, topology.servers[index].port);
        }
    }
    for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) {
        assert.equal(read(name).complete, true);
    }
}
module.exports = { groups, reportGroups, validate };
