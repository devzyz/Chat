'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { validate, widgetCases, storeCases } = require('./widgetEvidence');

test('widget gate requires exact executed cases, same SHA and a complete capture', /** 验证控件证据门禁的正例与必需失败路径。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-widget-evidence-'));
    const sha = 'b'.repeat(40);
    const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a2ioAAAAASUVORK5CYII=', 'base64');
    const report = /** 生成校验器的合成 XML 输入，不代表真实控件执行。 */ (names, gtest = false) =>
        '<testsuite tests="' + names.length + '" failures="0">' + names.map(/** 编码单条用例及执行状态。 */ name =>
            `<testcase name="${name}"${gtest ? ' status="run" result="completed"' : ''}/>`).join('') + '</testsuite>';
    const restore = /** 恢复完整的校验器测试输入。 */ () => {
        fs.writeFileSync(path.join(root, 'source-sha.txt'), sha);
        fs.writeFileSync(path.join(root, 'widgets.xml'), report(widgetCases));
        fs.writeFileSync(path.join(root, 'resource-store.xml'), report(storeCases, true));
        fs.writeFileSync(path.join(root, 'group-panel.png'), png);
    };
    const check = /** 检验当前测试目录。 */ () => validate(root, sha);
    try {
        restore(); check();
        assert.throws(/** 拒绝其他源码提交的报告。 */ () => validate(root, 'c'.repeat(40)));
        for (const file of ['widgets.xml', 'resource-store.xml', 'group-panel.png', 'source-sha.txt']) {
            restore(); fs.unlinkSync(path.join(root, file)); assert.throws(check);
        }
        for (const marker of ['<skipped/>', '<failure/>', '<error/>']) {
            restore(); fs.appendFileSync(path.join(root, 'widgets.xml'), marker); assert.throws(check);
        }
        restore(); fs.writeFileSync(path.join(root, 'widgets.xml'), report(widgetCases.slice(1))); assert.throws(check);
        restore(); fs.writeFileSync(path.join(root, 'widgets.xml'), report([...widgetCases.slice(1), widgetCases[1]]));
        assert.throws(check);
        restore(); fs.writeFileSync(path.join(root, 'resource-store.xml'), report(storeCases, true).replace('status="run"', 'status="notrun"'));
        assert.throws(check);
        restore(); fs.writeFileSync(path.join(root, 'widgets.xml'), report(widgetCases).replace('failures="0"', 'skipped="1"'));
        assert.throws(check);
        restore(); fs.writeFileSync(path.join(root, 'group-panel.png'), png.subarray(0, 24)); assert.throws(check);
    } finally { fs.rmSync(root, { recursive: true }); }
});
