'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { preserveLogs } = require('./evidence');

test('evidence includes bounded service logs and excludes links configurations and secrets', /** 验证正式日志目录、截断边界、脱敏和导出范围。 */ t => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'performance-evidence-'));
    t.after(/** 只删除本用例创建的临时目录。 */ () => fs.rmSync(temporary, { recursive: true, force: true }));
    const root = path.join(temporary, 'logs'), output = path.join(temporary, 'evidence');
    fs.mkdirSync(path.join(root, 'ChatA', 'nested'), { recursive: true });
    fs.writeFileSync(path.join(root, 'ResourceServer.log'), 'resource ready\n');
    fs.writeFileSync(path.join(root, 'ChatA', 'ChatA.txt'), 'token secret\nSELECT private\nwarning fixture-secret\n');
    fs.writeFileSync(path.join(root, 'ChatA', 'ChatA.1.txt'), 'password=' + 'x'.repeat(70000) + '\nsafe tail\n');
    fs.writeFileSync(path.join(root, 'ChatA', 'config.ini'), 'private configuration');
    fs.writeFileSync(path.join(root, 'ChatA', 'nested', 'hidden.log'), 'private nested log');
    fs.symlinkSync(path.join(root, 'ChatA'), path.join(root, 'linked-service'), 'dir');
    fs.symlinkSync(path.join(root, 'ChatA', 'config.ini'), path.join(root, 'linked.log'));
    preserveLogs(root, output, ['fixture-secret']);
    assert.deepEqual(fs.readdirSync(output).sort(), ['ChatA', 'ResourceServer.log']);
    assert.deepEqual(fs.readdirSync(path.join(output, 'ChatA')).sort(), ['ChatA.1.txt', 'ChatA.txt']);
    assert.equal(fs.readFileSync(path.join(output, 'ChatA', 'ChatA.txt'), 'utf8'), 'warning <redacted>\n');
    assert.equal(fs.readFileSync(path.join(output, 'ChatA', 'ChatA.1.txt'), 'utf8'), 'safe tail\n');
    for (let index = 0; index < 30; index++) fs.writeFileSync(path.join(root, `${index}.log`), 'bounded');
    const limited = path.join(temporary, 'limited'); preserveLogs(root, limited, []);
    assert.equal(fs.readdirSync(limited).length, 20);
});
