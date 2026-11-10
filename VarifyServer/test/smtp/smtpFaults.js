'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const { performance } = require('node:perf_hooks');
const { createSmtpAdapter } = require('../../email');

// 为指定故障模式启动真实回环 SMTP 对端，动作结束后清理计时器和连接。
async function withPeer(mode, action) {
    const sockets = new Set();
    const timers = new Set();
    // 跟踪连接并按故障模式发送、拒绝或省略 SMTP 问候。
    const server = net.createServer((socket) => {
        sockets.add(socket);
        // 消费预期的测试连接错误，避免未处理事件终止进程。
        socket.on('error', () => {});
        // 关闭后移除套接字跟踪。
        socket.once('close', () => sockets.delete(socket));
        if (mode === 'silent') return;
        socket.write(mode === 'rejectGreeting' ? '554 synthetic rejection\r\n' : '220 fixture ESMTP\r\n');
        if (mode === 'rejectGreeting') return;
        let buffer = '';
        let data = false;
        /**
         * 按行解析 SMTP 对话并注入拒绝、断连或拖延；
         * 限制输入缓冲长度。
         */
        socket.on('data', (chunk) => {
            buffer += chunk.toString();
            if (buffer.length > 32768) { socket.destroy(); return; }
            while (buffer.includes('\r\n')) {
                const boundary = buffer.indexOf('\r\n');
                const line = buffer.slice(0, boundary);
                buffer = buffer.slice(boundary + 2);
                if (data) {
                    if (line === '.') {
                        data = false;
                        socket.write('250 queued\r\n');
                    }
                } else if (/^(EHLO|HELO) /i.test(line)) socket.write('250 fixture\r\n');
                else if (/^MAIL FROM:/i.test(line)) {
                    if (mode === 'disconnect') socket.destroy();
                    else socket.write('250 sender accepted\r\n');
                } else if (/^RCPT TO:/i.test(line)) {
                    socket.write(mode === 'rejectRecipient' ? '550 recipient rejected\r\n' : '250 recipient accepted\r\n');
                } else if (line === 'DATA') {
                    if (mode === 'slowData') {
                        // Keep incoming bytes flowing beyond the total deadline. An idle
                        // socket timeout alone cannot bound this server response.
                        // 持续发送不完整响应以验证总期限不被数据活动延长。
                        const timer = setInterval(() => socket.write('354-waiting\r\n'), 20);
                        timers.add(timer);
                        // 连接关闭后清除拖延响应计时器。
                        socket.once('close', () => { clearInterval(timer); timers.delete(timer); });
                    } else { data = true; socket.write('354 send data\r\n'); }
                } else if (line === 'QUIT') socket.end('221 closing\r\n');
            }
        });
    });
    // 监听随机回环端口并传播绑定失败。
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    // 销毁本夹具仍跟踪的连接。
    const hardStop = setTimeout(() => { for (const socket of sockets) socket.destroy(); }, 4000);
    try { await action(server.address().port, sockets); }
    finally {
        clearTimeout(hardStop);
        for (const timer of timers) clearInterval(timer);
        for (const socket of sockets) socket.destroy();
        await new Promise(
            // 等待监听器关闭并传播错误。
            (resolve, reject) => server.close(
            // 根据监听器关闭结果完成或拒绝等待。
            (error) => error ? reject(error) : resolve()));
    }
}

// 有界等待所有真实 SMTP 连接关闭并断言跟踪集合为空。
async function awaitClosed(sockets) {
    await Promise.all([...sockets].map(
        // 为一个套接字构造带期限的关闭等待。
        (socket) => new Promise(
        // 安装关闭事件监听及一秒清理期限。
        (resolve, reject) => {
        // 连接未按时关闭则拒绝清理等待。
        const timer = setTimeout(() => reject(new Error('SMTP socket cleanup deadline')), 1000);
        // 连接关闭后清除期限并完成等待。
        socket.once('close', () => { clearTimeout(timer); resolve(); });
    })));
    assert.equal(sockets.size, 0, 'SMTP deadline must close the real socket');
}

const faultCases = [
    ['V09-SMTP-06', 'SMTP greeting rejected', 'rejectGreeting', 'Rejected'],
    ['V09-SMTP-07', 'SMTP recipient rejected', 'rejectRecipient', 'Rejected'],
    ['V09-SMTP-08', 'SMTP disconnect during send', 'disconnect', 'Unavailable'],
    ['V09-SMTP-09', 'SMTP silent greeting deadline', 'silent', 'DeadlineExceeded'],
    ['V09-SMTP-10', 'SMTP total deadline despite live response', 'slowData', 'DeadlineExceeded'],
    ['V09-SMTP-11', 'SMTP active cancellation closes socket', 'silent', 'Unavailable']
].map(
    // 将故障数据转为独立可执行用例。
    ([id, name, mode, expected]) => ({ id, name,
    // 验证指定 SMTP 故障状态、完成次数和连接清理。
    async action() {
    // 在真实故障对端上发信并验证取消与期限。
    await withPeer(mode, async (port, sockets) => {
        const adapter = createSmtpAdapter({ host: '127.0.0.1', port, secure: false,
            auth: 'none', deadlineMs: 250 });
        let completions = 0;
        const start = performance.now();
        try {
            const pending = adapter.sendMail({ from: 'sender@example.invalid', to: 'fault@example.invalid',
                // 累计发送结算次数以检测重复完成。
                subject: id, text: 'synthetic' }).then((result) => { completions += 1; return result; });
            if (id === 'V09-SMTP-11') {
                // Await a real accepted socket, not a fixed sleep or mocked transport.
                // 在一秒内等待夹具收到连接，超时拒绝。
                await new Promise((resolve, reject) => {
                    const deadline = performance.now() + 1000;
                    /**
                     * 检查连接集合；
                     * 未到期限时交还事件循环继续等待。
                     */
                    const check = () => {
                        if (sockets.size) resolve();
                        else if (performance.now() >= deadline) reject(new Error('connection deadline'));
                        else setImmediate(check);
                    };
                    check();
                });
                adapter.close();
            }
            assert.deepEqual(await pending, { status: expected });
            await awaitClosed(sockets);
            assert.equal(completions, 1);
            assert.ok(performance.now() - start < 1800, 'SMTP operation must be bounded');
        } finally { adapter.close(); }
    });
} }));

module.exports = { faultCases };
