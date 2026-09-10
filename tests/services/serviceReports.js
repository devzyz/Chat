'use strict';

const fs = require('node:fs');
const path = require('node:path');

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

function xml(value) {
    return String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&apos;');
}

function writeReports(root, selector, cases) {
    fs.mkdirSync(root, { recursive: true });
    for (const group of reportGroups(selector)) {
        const selected = cases.filter((entry) => entry.id.startsWith(group.prefix));
        const incomplete = selected.length === 0 || (group.expected !== undefined && selected.length !== group.expected);
        const duplicate = new Set(selected.map((entry) => entry.id)).size !== selected.length;
        const failures = selected.filter((entry) => !entry.pass).length + Number(incomplete || duplicate);
        const rows = selected.map((entry) => `<testcase classname="${xml(group.prefix)}" name="${xml(entry.id)} ${xml(entry.name)}" time="${Number(entry.seconds || 0).toFixed(3)}">${entry.pass ? '' : '<failure message="bounded contract failed"/>'}</testcase>`);
        if (incomplete || duplicate) rows.push('<testcase name="registration"><failure message="required cases missing or duplicated"/></testcase>');
        fs.writeFileSync(path.join(root, group.file),
            `<testsuite name="${xml(group.file)}" tests="${rows.length}" failures="${failures}">\n${rows.join('\n')}\n</testsuite>\n`);
    }
}

module.exports = { reportGroups, writeReports };
