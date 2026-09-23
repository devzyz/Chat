'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { reportGroups, writeReports } = require('./serviceReports');
const { reserve } = require('./fourProcessCases');

test('four-process reports cannot be replaced by passed adapter cases', /** 验证四进程报告要求完整用例集，缺失或失败记录不能生成成功证据。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-four-report-'));
    try {
        const groups = reportGroups('3C-07');
        assert.equal(groups.find(/** 定位四进程报告组。 */ value => value.prefix === 'T10-4PROC-').expected, 18);
        const cases = groups.filter(/** 选取其他依赖报告组作为完整性对照。 */ value => value.prefix !== 'T10-4PROC-').flatMap(/** 为单个报告组建立完整的合成通过记录。 */ group =>
            Array.from({ length: group.expected }, /** 生成连续编号的单条合成 Test ID。 */ (_, index) => ({ id: group.prefix + String(index + 1).padStart(2, '0'), pass: true })));
        writeReports(root, '3C-07', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_four_process.xml'), 'utf8'), /<failure/);
        for (let index = 1; index <= 18; index++) cases.push({ id: `T10-4PROC-${String(index).padStart(2, '0')}`, pass: index !== 18 });
        writeReports(root, '3C-07', cases);
        assert.match(fs.readFileSync(path.join(root, 'linux_four_process.xml'), 'utf8'), /failures="1"/);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('application port leases are unique until explicitly released', /** 验证同时持有的两个临时端口不同，并分别释放。 */ async () => {
    const first = await reserve();
    const second = await reserve();
    try { assert.notEqual(first.port, second.port); }
    finally { await first.release(); await second.release(); }
});


test('process shutdown reports retain safe failure identity and reject incomplete cleanup', /** 验证非零进程退出保存白名单诊断，并传播到四进程报告。 */ async () => {
    const { stop } = require('./fourProcessCases');
    const { caseDiagnostic } = require('./dependencyCoordinator');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-stop-report-'));
    try {
        const report = path.join(root, 'report.json');
        const owned = /** 建立已退出的所属进程替身，供停止报告校验。 */ () => ({ name: 'ChatServer', report, child: { /** 替身进程已退出，无需实际发送终止信号。 */ kill() {} }, exited: Promise.resolve(0) });
        fs.writeFileSync(report, JSON.stringify({ complete: true, escalated: false, exitCode: 139 }));
        await assert.rejects(stop(owned()), /** 核对停止失败保留正式服务名称与真实退出分类。 */ error => {
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
        const cases = [
            { id: 'T10-4PROC-10', name: 'occupied port', pass: false,
              diagnostic: caseDiagnostic(new Error('FourProcess:GateServer:harness-incomplete')) },
            { id: 'T10-4PROC-18', name: 'cleanup', pass: false,
              diagnostic: { stage: 'private-account', category: 'secret-token' } }
        ];
        writeReports(root, '3C-07', cases);
        const manifest = JSON.parse(fs.readFileSync(path.join(root, 'phase3c-reports.json'), 'utf8'));
        const recorded = manifest.reports.find(/** 从清单中定位四进程报告结果。 */ group => group.prefix === 'T10-4PROC-').cases;
        assert.deepEqual(recorded[0].diagnostic, { stage: 'stop-GateServer', category: 'harness-incomplete' });
        assert.equal(recorded[1].diagnostic, undefined);
        assert.match(fs.readFileSync(path.join(root, 'linux_four_process.xml'), 'utf8'), /stop-GateServer:harness-incomplete/);
        assert.doesNotMatch(JSON.stringify(manifest), /private-account|secret-token/);
    } finally { fs.rmSync(root, { recursive: true }); }
});
