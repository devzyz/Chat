'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');

/** 按服务选择器返回应生成的报告组及预期用例数量。 */
function reportGroups(selector = '3C-02') {
    const fourProcess = selector === '3C-07';
    if (fourProcess) selector = '3C-data-adapters';
    const groups = [{ file: 'linux_services.xml', prefix: 'T10-SVC-', expected: 12 }];
    if (['3C-03', '3C-05', '3C-data-adapters'].includes(selector)) {
        groups.push({ file: 'linux_migration.xml', prefix: 'T10-MIG-', expected: 12 });
    }
    if (['3C-05', '3C-data-adapters'].includes(selector)) {
        groups.push({ file: 'linux_message.xml', prefix: 'T10-MSG-', expected: 20 });
    }
    if (selector === '3C-04' || selector === '3C-adapters' || selector === '3C-data-adapters') {
        groups.push({ file: 'linux_redis.xml', prefix: 'T10-RDS-', expected: 9 });
        groups.push({ file: 'varify_redis.xml', prefix: 'V08-REDIS-', expected: 6 });
    }
    if (selector === '3C-06' || selector === '3C-adapters' || selector === '3C-data-adapters') {
        groups.push({ file: 'varify_smtp.xml', prefix: 'V09-SMTP-', expected: 12 });
    }
    if (!['3C-02', '3C-03', '3C-04', '3C-05', '3C-06', '3C-adapters', '3C-data-adapters'].includes(selector)) {
        throw new Error('unknown service selector');
    }
    if (fourProcess) groups.push({ file: 'linux_four_process.xml', prefix: 'T10-4PROC-', expected: 18 });
    return groups;
}

/** 转义 XML 特殊字符，防止报告属性和文本破坏结构。 */
function xml(value) {
    return String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&apos;');
}

/** 只保留白名单内的进程停止或业务子步骤诊断，拒绝任意敏感内容。 */
function processDiagnostic(value) {
    if (value && /^(gate-(verify|mail|register|login|selection)|client-(create|chat-id|send|message-sync|message-data))$/.test(value.stage)
        && /^(deadline|assertion|operation-failed|response--?\d{1,10})$/.test(value.category)) {
        return { stage: value.stage, category: value.category };
    }
    if (!value || !/^stop-(GateServer|StatusServer|ChatServer|VarifyServer)$/.test(value.stage)
        || !/^(stop-timeout|report-unavailable|stop-escalated|exit-[0-9]{1,10}|harness-incomplete|exit-unavailable|expected-failure)$/.test(value.category)) return undefined;
    return { stage: value.stage, category: value.category };
}

/** 生成 JUnit 与精确用例清单，缺项、重复或失败均体现在报告中。 */
function writeReports(root, selector, cases, options = {}) {
    fs.mkdirSync(root, { recursive: true });
    const manifest = { format: 1, sourceSha: process.env.CHAT_CANDIDATE_SHA || null,
        selector, reports: [] };
    for (const group of options.groups || reportGroups(selector)) {
        const selected = cases.filter(/** 筛选属于当前报告组前缀的用例。 */ (entry) => entry.id.startsWith(group.prefix));
        const incomplete = selected.length === 0 || (group.expected !== undefined && selected.length !== group.expected);
        const duplicate = new Set(selected.map(/** 提取 Test ID 以检测重复记录。 */ (entry) => entry.id)).size !== selected.length;
        const failures = selected.filter(/** 识别未通过用例以累计失败数量。 */ (entry) => !entry.pass).length + Number(incomplete || duplicate);
        const rows = selected.map(/** 编码单个 JUnit 用例，仅输出白名单内的失败诊断。 */ (entry) => {
            const diagnostic = processDiagnostic(entry.diagnostic);
            const message = diagnostic ? `${diagnostic.stage}:${diagnostic.category}` : 'bounded contract failed';
            return `<testcase classname="${xml(group.prefix)}" name="${xml(entry.id)} ${xml(entry.name)}" time="${Number(entry.seconds || 0).toFixed(3)}">${entry.pass ? '' : `<failure message="${xml(message)}"/>`}</testcase>`;
        });
        if (incomplete || duplicate) rows.push('<testcase name="registration"><failure message="required cases missing or duplicated"/></testcase>');
        fs.writeFileSync(path.join(root, group.file),
            `<testsuite name="${xml(group.file)}" tests="${rows.length}" failures="${failures}">\n${rows.join('\n')}\n</testsuite>\n`);
        manifest.reports.push({ file: group.file, owner: 'tests/services', level: options.level || 'Integration',
            deadlineSeconds: 690, prefix: group.prefix, expected: group.expected,
            cases: selected.map(/** 生成用例清单记录，诊断字段通过白名单后才保留。 */ entry => ({ id: entry.id, name: entry.name, pass: entry.pass,
                ...(processDiagnostic(entry.diagnostic) ? { diagnostic: processDiagnostic(entry.diagnostic) } : {}) })),
            sha256: createHash('sha256').update(fs.readFileSync(path.join(root, group.file))).digest('hex') });
    }
    fs.writeFileSync(path.join(root, options.manifest || 'phase3c-reports.json'), JSON.stringify(manifest, null, 2));
}

module.exports = { reportGroups, writeReports };
