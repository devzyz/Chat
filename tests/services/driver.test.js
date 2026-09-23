'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const driver = process.env.CHAT_FOUR_DRIVER;
assert.ok(driver && path.isAbsolute(driver), 'compiled driver required');
/** 运行原生驱动并收集有界时间内的退出结果和标准输出。 */
function run(args, env = {}) {
    return new Promise(/** 拥有驱动子进程及截止计时器，退出或启动失败时完成观察。 */ resolve => {
        const child = spawn(driver, args, { windowsHide: true,
            env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = '';
        const timer = setTimeout(/** 到达驱动测试时限后发送终止信号。 */ () => child.kill(), 10000);
        child.stdout.on('data', /** 累计驱动输出供脱敏和协议断言。 */ bytes => { output += bytes; }); child.stderr.resume();
        child.on('error', /** 启动失败时取消计时器并返回失败状态。 */ () => { clearTimeout(timer); resolve({ code: -1, output }); });
        child.on('close', /** 退出时取消计时器并返回退出码及输出。 */ code => { clearTimeout(timer); resolve({ code, output }); });
    });
}
test('production harness reaps a real child and records safe completion', /** 验证监督器正常回收成功子进程，并生成完整无升级终止的报告。 */ async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-driver-'));
    try {
        const report = path.join(root, 'report.json');
        const result = await run(['supervise', process.execPath, root, report, '-e', 'process.exit(0)']);
        assert.equal(result.code, 0);
        assert.deepEqual(JSON.parse(fs.readFileSync(report)), { complete: true, escalated: false, exitCode: 0 });
    } finally { fs.rmSync(root, { recursive: true }); }
});
for (const id of [1006, 1008]) test(`wire response ${id} validates production frame identity`, /** 用真实 loopback 响应验证驱动核对消息类型且不泄漏响应中的 Token。 */ async () => {
    const sockets = new Set();
    const server = net.createServer(/** 登记测试连接，在首个请求后发送指定类型的合成响应。 */ socket => {
        sockets.add(socket); socket.on('error', /** 吸收预期断连错误，结果由驱动退出和输出断言判定。 */ () => {}); socket.once('close', /** 连接关闭后移除跟踪记录。 */ () => sockets.delete(socket));
        socket.once('data', /** 编码带测试敏感字段的响应帧并关闭连接。 */ () => {
            const body = Buffer.from('{"error":1010,"token":"not-for-evidence"}');
            const header = Buffer.alloc(4); header.writeUInt16BE(id); header.writeUInt16BE(body.length, 2);
            socket.end(Buffer.concat([header, body]));
        });
    });
    await new Promise(/** 在临时 loopback 端口启动响应服务。 */ resolve => server.listen(0, '127.0.0.1', resolve));
    try {
        const result = await run(['chat'], { CHAT_FOUR_WIRE: JSON.stringify({ port: server.address().port,
            login: { uid: 1, token: 'not-for-evidence' }, requests: [] }) });
        assert.equal(result.code, id === 1006 ? 0 : 1);
        assert.ok(!result.output.includes('not-for-evidence'));
    } finally { for (const socket of sockets) socket.destroy(); await new Promise(/** 等待本用例响应服务关闭。 */ resolve => server.close(resolve)); }
});
