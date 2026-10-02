'use strict';
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const profiles = require('./profiles.json');
const { validate } = require('./metrics');
const scenarios = ['login', 'same-server', 'cross-server', 'group', 'resources', 'mixed', 'offline'];

/** 把动态文本编码为 XML 属性或正文。 */
function escapeXml(value) { return String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('"', '&quot;'); }
/** 格式化毫秒指标，缺失值明确展示为未获得。 */
function number(value) { return Number.isFinite(value) ? value.toFixed(2) : '未获得'; }
/** 验证完整执行矩阵、源码身份和清理结果，防止部分结果被当作通过。 */
function verify(report, sha) {
    assert.match(sha, /^[a-f0-9]{40}$/); assert.equal(report.sha, sha);
    assert.ok(Object.hasOwn(profiles, report.profile));
    assert.equal(report.status, 'passed'); assert.equal(report.cleanup?.complete, true);
    assert.equal(report.failures.length, 0);
    const expected = Array.from({ length: profiles[report.profile].repeats },
        /** 为每轮展开唯一场景集合。 */ (_, index) => scenarios.map(/** 生成场景结果键。 */ name => `${index + 1}:${name}`)).flat().sort();
    assert.deepEqual(report.results.map(/** 提取实际场景结果键。 */ row => `${row.round}:${row.name}`).sort(), expected);
    assert.deepEqual(report.warmups.map(/** 检查独立预热证据完整性。 */ row => `${row.round}:${row.name}`).sort(), expected);
    for (const row of report.warmups) {
        validate(row.result.metrics || row.result);
        if (row.name === 'mixed') validate(row.result.resources);
    }
    for (const row of report.results) {
        validate(row.metrics);
        if (row.name === 'mixed') { assert.ok(row.resources, 'missing-mixed-resource-evidence'); validate(row.resources); }
    }
}
/** 写入机器报告、JUnit 和简明中文报告；失败与未执行状态均保留。 */
function write(root, report) {
    fs.mkdirSync(root, { recursive: true });
    fs.writeFileSync(path.join(root, 'metrics.json'), JSON.stringify(report, null, 2));
    const rows = report.results.map(/** 将每个场景指标呈现为简洁表格。 */ row => {
        const m = row.metrics;
        return `| ${row.round} / ${row.name} | ${m.completed}/${m.planned} | ${number(m.throughput)} | ${number(m.latencyMs.p95)} | ${number(m.latencyMs.p99)} | ${number(m.deliveryMs?.p95)} | ${m.pass ? '通过' : '失败'} |`;
    });
    const resourceRows = report.results.filter(/** 筛选含资源吞吐的场景。 */ row => row.metrics.bytes !== undefined || row.resources)
        .map(/** 呈现资源吞吐并保留混合场景独立结果。 */ row => {
            const value = row.resources || row.metrics;
            return `- 第 ${row.round} 轮 ${row.name}：${number(value.mibPerSecond)} MiB/s，${value.completed}/${value.planned} 次，${value.pass ? '通过' : '失败'}。`;
        });
    const markdown = `# 性能测试报告\n\n状态：${report.status}；档位：${report.profile}。\n\n` +
        `提交：\`${report.sha}\`。运行：[GitHub Actions](${report.url})。\n\n` +
        `环境：GitHub Ubuntu 24.04，同机正式服务与临时依赖；${report.environment?.cpus || '?'} vCPU，` +
        `${number((report.environment?.memoryBytes || 0) / 1073741824)} GiB RAM，Node ${report.environment?.node || '?' }。\n\n` +
        '| 轮次 / 场景 | 完成/计划 | 业务完成/秒 | p95 ms | p99 ms | 接收 p95 ms | 结果 |\n' +
        '|---|---:|---:|---:|---:|---:|---|\n' + rows.join('\n') + '\n\n' +
        resourceRows.join('\n') + '\n\n' +
        `清理：${report.cleanup?.complete ? '通过' : '未完成'}。失败分类：${report.failures.map(/** 只列安全阶段和类别。 */ item => `${item.stage}: ${item.category}`).join('；') || '无'}。\n\n` +
        'JSON 工件包含提交确认/接收百分位、未发送数量、事件循环延迟、资源字节吞吐、各进程 CPU/RSS 与各阶段计时。\n\n' +
        '此结果为 Linux 共享运行器同机基线，不代表公网体验、Windows 性能或生产容量。群聊包含两秒补拉等待。' +
        '速度指标暂不作为回归门禁；业务错误、超时、缺样本、消息或内容错误、清理失败会失败。\n\n' +
        '调用示例：`$chat-performance 对 develop 运行 baseline 并下载报告`。\n';
    fs.writeFileSync(path.join(root, '测试.md'), markdown);
    const cases = report.results.map(/** 生成场景业务正确性用例。 */ row =>
        `<testcase name="${row.round}-${escapeXml(row.name)}">${row.metrics.pass && (!row.resources || row.resources.pass) ? '' : '<failure message="scenario-failed"/>'}</testcase>`);
    for (const failure of report.failures) cases.push(`<testcase name="${escapeXml(failure.stage)}"><failure message="${escapeXml(failure.category)}"/></testcase>`);
    if (report.status !== 'passed' && !report.failures.length) cases.push('<testcase name="completion"><failure message="incomplete"/></testcase>');
    fs.writeFileSync(path.join(root, 'performance.xml'), `<testsuite name="performance" tests="${cases.length}">${cases.join('')}</testsuite>\n`);
    return markdown;
}
if (require.main === module) {
    const [root, sha] = process.argv.slice(2);
    verify(JSON.parse(fs.readFileSync(path.join(root, 'metrics.json'))), sha);
}
module.exports = { scenarios, verify, write };
