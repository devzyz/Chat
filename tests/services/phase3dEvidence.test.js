'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { groups, reportGroups, validate } = require('./phase3dEvidence');
const { writeReports } = require('./serviceReports');
const { createTopology } = require('./twoServerTopology');

test('foundation evidence binds exact cases, bytes, SHA, distinct clients and cleanup', /** 验证证据绑定精确 Test ID、文件字节、源码 SHA、独立客户端及完整清理。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-3d-evidence-'));
    const previous = process.env.CHAT_CANDIDATE_SHA;
    const sha = 'a'.repeat(40);
    const write = /** 将合成 JSON 写入本测试拥有的证据文件。 */ (name, value) => fs.writeFileSync(path.join(root, name), JSON.stringify(value));
    const cases = Array.from({ length: 7 }, /** 生成完整基础合同的合成通过记录。 */ (_, index) => ({ id: `E03-CONTRACT-${String(index + 6).padStart(2, '0')}`,
        name: 'synthetic validator input', pass: true }));
    const topology = createTopology('b'.repeat(32), {
        gate: 30001, status: 30002, varify: 30003, chatA: 30004, rpcA: 30005, chatB: 30006, rpcB: 30007 });
    topology.clients = topology.users.map(/** 为每个逻辑用户生成独立客户端身份和匹配实例端点。 */ (user, index) => ({ logical: user.logical, pid: index + 100,
        uid: index + 1, active: true, host: topology.host, port: topology.servers[index].port }));
    const restore = /** 恢复完整且自洽的基础报告、拓扑与清理证据。 */ () => {
        writeReports(root, '3D-00', cases, { groups, manifest: 'phase3d-reports.json', level: 'E2E' });
        write('topology.json', topology);
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) write(name, { complete: true });
    };
    try {
        process.env.CHAT_CANDIDATE_SHA = sha;
        restore(); validate(root, sha);
        const finalize = /** 有界运行真实最终证据入口并取得退出状态。 */ () => spawnSync(process.execPath, [path.join(__dirname, 'finalizeEvidence.js'), root], {
            env: { ...process.env, CHAT_SERVICE_SELECTOR: '3D-00', GITHUB_ACTIONS: 'false' }, timeout: 3000 });
        write('teardown.json', { complete: true });
        assert.equal(finalize().status, 0);
        fs.unlinkSync(path.join(root, 'phase3d-reports.json'));
        assert.equal(finalize().status, 1);
        assert.match(fs.readFileSync(path.join(root, groups[0].file), 'utf8'), /<failure/);
        restore();
        assert.throws(/** 用错误源码 SHA 校验证据，预期拒绝串用。 */ () => validate(root, 'c'.repeat(40)));
        const reportPath = path.join(root, groups[0].file);
        fs.appendFileSync(reportPath, 'changed bytes');
        assert.throws(/** 校验被篡改字节的报告，预期摘要不匹配。 */ () => validate(root, sha));
        restore();
        writeReports(root, '3D-00', cases.slice(1), { groups, manifest: 'phase3d-reports.json' });
        assert.throws(/** 校验缺少基础用例的报告，预期失败。 */ () => validate(root, sha));
        restore();
        const wrongIds = cases.map(/** 复制基础记录以独立修改 Test ID。 */ value => ({ ...value })); wrongIds[0].id = 'E03-CONTRACT-99';
        writeReports(root, '3D-00', wrongIds, { groups, manifest: 'phase3d-reports.json' });
        assert.throws(/** 校验数量相同但 Test ID 错误的报告。 */ () => validate(root, sha));
        restore(); write('topology.json', { ...topology, clients: [topology.clients[0], topology.clients[0]] });
        assert.throws(/** 校验重复客户端身份的拓扑，预期拒绝。 */ () => validate(root, sha));
        for (const name of ['application-teardown.json', 'process-teardown.json', 'redaction.json']) {
            restore(); write(name, { complete: false }); assert.throws(/** 校验未完成的应用清理、进程清理或脱敏证据。 */ () => validate(root, sha));
        }
        restore();
        assert.throws(/** 用更大范围选择器校验只有基础用例的证据，预期失败。 */ () => validate(root, sha, '3D-01'));
        const journey = Array.from({ length: 7 }, /** 生成完整好友旅程用例的合成通过记录。 */ (_, index) => ({
            id: `E03-JOURNEY-${String(index + 1).padStart(2, '0')}`,
            name: 'synthetic journey validator input', pass: true }));
        const writeJourney = /** 写入基础合同与指定旅程用例形成的报告。 */ values => writeReports(root, '3D-01', [...cases, ...values], {
            groups: reportGroups('3D-01'), manifest: 'phase3d-reports.json', level: 'E2E' });
        writeJourney(journey);
        validate(root, sha, '3D-01');
        writeJourney(journey.slice(1));
        assert.throws(/** 校验缺少旅程用例的证据，预期失败。 */ () => validate(root, sha, '3D-01'));
        writeJourney(journey.map(/** 把第四条旅程用例改为失败。 */ (value, index) => ({ ...value, pass: index !== 3 })));
        assert.throws(/** 校验含失败旅程用例的证据，预期拒绝。 */ () => validate(root, sha, '3D-01'));
        writeJourney(journey);
        fs.appendFileSync(path.join(root, 'linux_phase3d_journey.xml'), 'modified');
        assert.throws(/** 校验字节被篡改的旅程报告，预期失败。 */ () => validate(root, sha, '3D-01'));
        assert.throws(/** 验证未知 Phase 3D 选择器被拒绝。 */ () => reportGroups('unknown'));
        restore();
        const messaging = Array.from({ length: 8 }, /** 生成完整跨实例消息用例的合成通过记录。 */ (_, index) => ({
            id: `E03-XMSG-${String(index + 1).padStart(2, '0')}`,
            name: 'synthetic messaging validator input', pass: true }));
        const writeMessaging = /** 写入基础、旅程及指定消息用例报告。 */ values => writeReports(root, '3D-02', [...cases, ...journey, ...values], {
            groups: reportGroups('3D-02'), manifest: 'phase3d-reports.json', level: 'E2E' });
        writeMessaging(messaging);
        validate(root, sha, '3D-02');
        writeMessaging(messaging.slice(1));
        assert.throws(/** 校验缺少消息用例的证据，预期失败。 */ () => validate(root, sha, '3D-02'));
        writeMessaging(messaging.map(/** 把第六条消息用例改为失败。 */ (value, index) => ({ ...value, pass: index !== 5 })));
        assert.throws(/** 校验含失败消息用例的证据，预期拒绝。 */ () => validate(root, sha, '3D-02'));
        writeMessaging(messaging);
        fs.appendFileSync(path.join(root, 'linux_phase3d_messaging.xml'), 'modified');
        assert.throws(/** 校验字节被篡改的消息报告，预期失败。 */ () => validate(root, sha, '3D-02'));
        restore();
        write('topology.json', { ...topology, recoveredClients: [{ ...topology.clients[0],
            previousPid: topology.clients[0].pid, pid: 9999 }] });
        const recovery = Array.from({ length: 4 }, /** 生成完整历史恢复用例的合成通过记录。 */ (_, index) => ({
            id: `E03-RECOVER-${String(index + 1).padStart(2, '0')}`,
            name: 'synthetic recovery validator input', pass: true }));
        const writeRecovery = /** 写入前置报告及指定恢复用例形成的证据。 */ values => writeReports(root, '3D-03-history', [...cases, ...journey, ...messaging, ...values], {
            groups: reportGroups('3D-03-history'), manifest: 'phase3d-reports.json', level: 'E2E' });
        writeRecovery(recovery);
        validate(root, sha, '3D-03-history');
        write('topology.json', topology);
        assert.throws(/** 校验缺少恢复客户端身份的拓扑，预期拒绝。 */ () => validate(root, sha, '3D-03-history'));
        write('topology.json', { ...topology, recoveredClients: [{ ...topology.clients[0],
            previousPid: topology.clients[0].pid, pid: 9999 }] });
        writeRecovery(recovery.slice(1));
        assert.throws(/** 校验缺少恢复用例的证据，预期失败。 */ () => validate(root, sha, '3D-03-history'));
        writeRecovery(recovery.map(/** 把第三条恢复用例改为失败。 */ (value, index) => ({ ...value, pass: index !== 2 })));
        assert.throws(/** 校验含失败恢复用例的证据，预期拒绝。 */ () => validate(root, sha, '3D-03-history'));
        writeRecovery(recovery);
        fs.appendFileSync(path.join(root, 'linux_phase3d_recovery.xml'), 'modified');
        assert.throws(/** 校验字节被篡改的恢复报告，预期失败。 */ () => validate(root, sha, '3D-03-history'));
        assert.equal(reportGroups('3D-03').at(-1).expected, 11);
    } finally {
        if (previous === undefined) delete process.env.CHAT_CANDIDATE_SHA;
        else process.env.CHAT_CANDIDATE_SHA = previous;
        fs.rmSync(root, { recursive: true });
    }
});
