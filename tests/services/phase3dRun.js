'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');
const { DependencyCoordinator, loadConfiguration, caseDiagnostic } = require('./dependencyCoordinator');
const { runFiveProcessCases } = require('./fiveProcessCases');
const { writeReports } = require('./serviceReports');

const { reportGroups } = require('./phase3dEvidence');
/** 按所选 Phase 3D 范围运行真实服务合同，聚合用例与清理证据并传播失败。 */
async function run(root) {
    const selector = process.env.CHAT_SERVICE_SELECTOR || '3D-00';
    const groups = reportGroups(selector);
    fs.mkdirSync(root, { recursive: true });
    const cases = [];
    let coordinator;
    let primaryFailure;
    let cleanup = { complete: false };
    const record = /** 记录用例耗时与真实结果，失败时以稳定 Test ID 中断。 */ async (id, name, action) => {
        const started = performance.now();
        try {
            await action();
            cases.push({ id, name, pass: true, seconds: (performance.now() - started) / 1000 });
        } catch (error) {
            cases.push({ id, name, pass: false, diagnostic: caseDiagnostic(error), seconds: (performance.now() - started) / 1000 });
            throw new Error(id);
        }
    };
    try {
        assert.match(process.env.CHAT_CANDIDATE_SHA || '', /^[a-f0-9]{40}$/);
        coordinator = new DependencyCoordinator(loadConfiguration(process.env));
        for (const service of ['redis', 'mysql', 'mailpit']) await coordinator.inspect(service);
        await coordinator.bootstrap();
        await runFiveProcessCases(coordinator, record, root, selector);
    } catch (error) {
        primaryFailure = /^E03-(?:CONTRACT|JOURNEY|XMSG|RECOVER)-\d\d$/.test(error.message) ? error.message : 'setup';
    } finally {
        if (coordinator) {
            try { cleanup = await coordinator.teardown(); } catch { cleanup = { complete: false }; }
        }
        fs.writeFileSync(path.join(root, 'teardown.json'), JSON.stringify({ ...cleanup, primaryFailure }));
        writeReports(root, selector, cases, { groups, manifest: 'phase3d-reports.json', level: 'E2E' });
    }
    if (primaryFailure || !cleanup.complete || cases.length !== groups.reduce(/** 累计所选报告组预期用例总数。 */ (sum, group) => sum + group.expected, 0) || cases.some(/** 识别未通过的用例以判定本次运行失败。 */ value => !value.pass)) {
        throw new Error('phase3d-contract-failed');
    }
}

module.exports = { run };
if (require.main === module) run(process.env.CHAT_SERVICE_EVIDENCE_ROOT).catch(/** 主流程异常时输出证据定位提示并以非零退出。 */ () => {
    process.stderr.write('Phase 3D contract failed; inspect bounded evidence\n');
    process.exitCode = 1;
});
