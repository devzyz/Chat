'use strict';
const assert = require('node:assert/strict');
const { test } = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const tls = require('node:tls');
const { createCertificate } = require('./certificate');
const { createGateway, validate } = require('../../tlsGateway');

/** 监听独立 loopback 端口，并在错误时拒绝。 */
function listen(server, port = 0) {
    return new Promise(/** 将端口绑定结果交给测试。 */ (resolve, reject) => {
        server.once('error', reject);
        server.listen(port, '127.0.0.1', /** 只在监听成功后返回实际端口。 */ () => { server.removeListener('error', reject); resolve(server.address().port); });
    });
}

/** 与入口交换真实 TLS 字节；证书错误、超时或提前关闭都拒绝。 */
function exchange(port, ca, servername = 'localhost') {
    return new Promise(/** 将回显或握手失败转换为测试结果。 */ (resolve, reject) => {
        const socket = tls.connect({ host: '127.0.0.1', port, ca, servername }, /** 握手成功后才发送夹具字节。 */ () => socket.write('tls-fixture'));
        socket.setTimeout(3000, /** 有界退出没有响应的连接。 */ () => { socket.destroy(); reject(new Error('timeout')); });
        socket.once('error', reject);
        socket.once('data', /** 收到字节后关闭自有连接并返回内容。 */ bytes => { socket.destroy(); resolve(bytes.toString()); });
        socket.once('close', /** 提前关闭不能被判定为交换成功。 */ () => reject(new Error('closed before echo')));
    });
}

test('TLS gateway protects all client channels and rejects invalid certificates and upstreams', { timeout: 20000 }, /** 验证 TLS 认证、三通道覆盖与失败回滚。 */ async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-tls-contract-'));
    const peers = new Set();
    const echo = net.createServer(/** 登记测试上游并回显所有字节。 */ socket => {
        peers.add(socket); socket.once('close', /** 释放已关闭测试连接的跟踪引用。 */ () => peers.delete(socket));
        socket.on('error', /** 预期测试断开无需额外报告。 */ () => {});
        socket.once('data', /** 模拟 HTTP 上游写完大响应后立即正常关闭。 */ data => {
            if (data.toString() === 'large') socket.end(Buffer.alloc(4 * 1024 * 1024, 37));
            else { socket.write(data); socket.pipe(socket); }
        });
    });
    let gateway;
    try {
        createCertificate(root);
        const targetPort = await listen(echo);
        const reservations = [net.createServer(), net.createServer(), net.createServer()];
        const ports = await Promise.all(reservations.map(/** 获取随机端口预留。 */ server => listen(server)));
        await Promise.all(reservations.map(/** 释放随机端口供被测入口绑定。 */ server => new Promise(/** 等待监听关闭。 */ resolve => server.close(resolve))));
        const config = { cert: 'cert.pem', key: 'key.pem', listeners: ['gate', 'resource', 'chat-01'].map(/** 为三类通道创建独立入口配置。 */ (name, i) => ({
            name, host: '127.0.0.1', port: ports[i], targetHost: '127.0.0.1', targetPort
        })) };
        const invalid = structuredClone(config); invalid.listeners[0].targetHost = '192.0.2.1';
        assert.throws(/** 远程明文上游必须拒绝。 */ () => validate(invalid), /listener/);
        assert.throws(/** 不完整 TLS 覆盖必须拒绝。 */ () => validate({ ...config, listeners: config.listeners.slice(0, 2) }), /cover/);
        gateway = createGateway(config, root, { /** 测试预期的拒绝不写日志。 */ warn() {}, /** 绑定失败由断言检查。 */ error() {} });
        await gateway.start();
        const ca = fs.readFileSync(path.join(root, 'cert.pem'));
        for (const port of ports) assert.equal(await exchange(port, ca), 'tls-fixture');
        await assert.rejects(exchange(ports[0]), /certificate/i);
        await assert.rejects(exchange(ports[1], ca, 'wrong.invalid'), /altnames|hostname/i);
        await new Promise(/** 慢速读取完整响应，正常上游关闭不能截断 TLS 写缓冲。 */ (resolve, reject) => {
            let count = 0;
            const socket = tls.connect({ host: '127.0.0.1', port: ports[1], ca, servername: 'localhost' },
                /** 建连后请求大响应，再暂停读取以制造背压。 */ () => { socket.write('large'); socket.pause(); });
            const resume = setTimeout(/** 释放慢消费者屏障。 */ () => socket.resume(), 150);
            socket.setTimeout(5000, /** 读取超时必须失败。 */ () => socket.destroy(new Error('large-response-timeout')));
            socket.on('data', /** 检查分块内容并累计总长度。 */ bytes => {
                if (bytes.some(/** 任一变更字节都意味着转发损坏。 */ byte => byte !== 37)) socket.destroy(new Error('corrupt-response'));
                count += bytes.length;
            });
            socket.once('error', reject);
            socket.once('close', /** 只有完整读取且结束才能通过。 */ () => {
                clearTimeout(resume);
                if (count !== 4 * 1024 * 1024) reject(new Error('truncated-response'));
                else resolve();
            });
        });
        await gateway.close();
        await gateway.close();
        // 全部监听必须可立即重新占用，证明关闭释放端口。
        for (const port of ports) {
            const probe = net.createServer(); await listen(probe, port);
            await new Promise(/** 释放测试探针监听。 */ resolve => probe.close(resolve));
        }
        const occupied = net.createServer(); await listen(occupied, ports[1]);
        try {
            gateway = createGateway(config, root, { /** 丢弃预期拒绝日志。 */ warn() {}, /** 绑定失败由断言检查。 */ error() {} });
            await assert.rejects(gateway.start(), /EADDRINUSE/);
            const probe = net.createServer(); await listen(probe, ports[0]);
            await new Promise(/** 释放回滚检查监听。 */ resolve => probe.close(resolve));
        } finally { await new Promise(/** 释放测试主动占用的冲突端口。 */ resolve => occupied.close(resolve)); }
    } finally {
        if (gateway) await gateway.close();
        for (const peer of peers) peer.destroy();
        if (echo.listening) await new Promise(/** 释放测试上游监听。 */ resolve => echo.close(resolve));
        const owned = path.resolve(root);
        assert.equal(path.dirname(owned), path.resolve(os.tmpdir()));
        assert.ok(path.basename(owned).startsWith('chat-tls-contract-'));
        fs.rmSync(owned, { recursive: true, force: true });
    }
});
