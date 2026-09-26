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

test('supervision distinguishes an expected application failure from incomplete cleanup', /** 重复观察自行失败退出的真实子进程，退出状态不得被误报为监督器清理失败。 */ async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-driver-exit-'));
    try {
        for (let index = 0; index < 40; ++index) {
            const report = path.join(root, `${index}.json`);
            const result = await run(['supervise', process.execPath, root, report, '-e', 'process.exit(1)']);
            assert.equal(result.code, 0, `supervisor iteration ${index}`);
            assert.deepEqual(JSON.parse(fs.readFileSync(report)), { complete: true, escalated: false, exitCode: 1 });
        }
    } finally { fs.rmSync(root, { recursive: true }); }
});

for (const phase of ['request', 'login', 'partial', 'success', 'timeout']) {
    test(`wire disconnect evidence rejects ${phase} ambiguity`, /** 核对仅登录成功后的完整请求被明确断开才构成拒绝证据，半帧及业务成功不算拒绝。 */ async () => {
        const sockets = new Set();
        const server = net.createServer(/** 逐帧读取登录及消息请求，模拟正式协议的拒绝关闭边界。 */ socket => {
            sockets.add(socket);
            socket.on('error', /** 预期断连由驱动结果断言，不把传输错误作为测试进程异常。 */ () => {});
            socket.on('close', /** 移除已结束的所属连接。 */ () => sockets.delete(socket));
            let buffered = Buffer.alloc(0), count = 0;
            socket.on('data', /** 累计完整帧后按阶段发送成功响应或关闭连接。 */ bytes => {
                buffered = Buffer.concat([buffered, bytes]);
                while (buffered.length >= 4 && buffered.length >= 4 + buffered.readUInt16BE(2)) {
                    const id = buffered.readUInt16BE(0);
                    buffered = buffered.subarray(4 + buffered.readUInt16BE(2));
                    ++count;
                    if (count === 2 && phase === 'timeout') return;
                    if ((count === 1 && phase === 'login') || (count === 2 && phase !== 'success')) {
                        socket.end(phase === 'partial' ? Buffer.from([0]) : undefined);
                        return;
                    }
                    const body = Buffer.from('{"error":0}');
                    const header = Buffer.alloc(4); header.writeUInt16BE(id + 1); header.writeUInt16BE(body.length, 2);
                    socket.write(Buffer.concat([header, body]));
                }
            });
        });
        await new Promise(/** 等待本次 loopback 服务成功监听。 */ resolve => server.listen(0, '127.0.0.1', resolve));
        try {
            const result = await run(['chat'], { CHAT_FOUR_WIRE: JSON.stringify({ port: server.address().port,
                login: { uid: 1, token: '<synthetic-token>' }, requests: [{ id: 1016, expect_disconnect: true,
                    body: { from_uid: 2 } }] }) });
            assert.equal(result.code, phase === 'request' ? 0 : 1);
            if (phase === 'request') assert.deepEqual(JSON.parse(result.output),
                { error: 0, responses: [{ disconnected: true }] });
        } finally {
            for (const socket of sockets) socket.destroy();
            await new Promise(/** 等待本次监听与全部连接完成清理。 */ resolve => server.close(resolve));
        }
    });
}
