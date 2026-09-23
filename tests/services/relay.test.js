'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { randomUUID } = require('node:crypto');
const { poll } = require('./dependencyCoordinator');

test('compiled production-codec relay drops one ACK and replays one framed notification', { timeout: 15000 }, /** 以真实驱动和 loopback 后端验证中继占用端口拒绝、ACK 丢弃与通知重放。 */ async () => {
    const driver = process.env.CHAT_FOUR_DRIVER;
    assert.ok(driver && path.isAbsolute(driver));
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-relay-'));
    const report = path.join(root, 'relay.json');
    const dropUuid = randomUUID(), replayUuid = randomUUID();
    const sockets = new Set();
    const frame = /** 将 JSON 载荷编码为 Chat 协议帧。 */ (id, body) => {
        const bytes = Buffer.from(JSON.stringify(body));
        const header = Buffer.alloc(4);
        header.writeUInt16BE(id); header.writeUInt16BE(bytes.length, 2);
        return Buffer.concat([header, bytes]);
    };
    const backend = net.createServer(/** 登记后端测试连接并按收到的帧类型回复合成消息。 */ socket => {
        sockets.add(socket); socket.on('error', /** 吸收预期连接错误，最终合同由协议与清理断言决定。 */ () => {});
        let input = Buffer.alloc(0);
        socket.on('data', /** 累计并拆出完整请求帧，回复 ACK 或通知。 */ bytes => {
            input = Buffer.concat([input, bytes]);
            while (input.length >= 4 && input.length >= 4 + input.readUInt16BE(2)) {
                const id = input.readUInt16BE(0), length = input.readUInt16BE(2);
                input = input.subarray(4 + length);
                socket.write(id === 1016 ? frame(1017, { error: 0, uuid_msgId: [{ msg_uuid: dropUuid, message_id: 81 }] })
                    : frame(1018, { error: 0, msgs: [{ msg_uuid: replayUuid, message_id: 82 }] }));
            }
        });
    });
    let child;
    const lease = net.createServer();
    try {
        await new Promise(/** 为合成后端分配临时 loopback 端口。 */ resolve => backend.listen(0, '127.0.0.1', resolve));
        await new Promise(/** 占用中继候选端口以验证重复绑定拒绝。 */ resolve => lease.listen(0, '127.0.0.1', resolve));
        const port = lease.address().port;
        const occupiedReport = path.join(root, 'occupied.json');
        child = spawn(driver, ['relay', occupiedReport], { windowsHide: true, stdio: 'ignore', env: { ...process.env,
            CHAT_RELAY_CONFIG: JSON.stringify({ port, backend: backend.address().port, dropUuid, replayUuid }) } });
        child.done = new Promise(/** 观察占用端口场景的驱动退出，启动失败返回负值。 */ resolve => { child.once('exit', resolve); child.once('error', /** 把驱动启动错误转换为可断言的失败码。 */ () => resolve(-1)); });
        await poll(/** 等待占用端口的中继进程退出。 */ async () => child.exitCode !== null, 3000);
        assert.equal(await child.done, 1);
        const occupied = JSON.parse(fs.readFileSync(occupiedReport));
        assert.equal(occupied.ready, false);
        assert.equal(occupied.startupStage, 'bind');
        assert.ok(Number.isInteger(occupied.errorCode) && occupied.errorCode !== 0);
        // Match replacement of a server that actively closed an established session.
        const accepted = new Promise(/** 观察旧监听器接受的客户端连接。 */ resolve => lease.once('connection', resolve));
        const previousClient = net.connect(port, '127.0.0.1');
        sockets.add(previousClient);
        previousClient.on('error', /** 吸收旧服务端连接的预期关闭错误。 */ () => {});
        const previousServer = await accepted;
        sockets.add(previousServer);
        previousServer.on('error', /** 吸收旧客户端连接的预期关闭错误。 */ () => {});
        const previousClosed = new Promise(/** 等待旧客户端关闭以完成旧代会话清理。 */ resolve => previousClient.once('close', resolve));
        previousServer.end();
        await previousClosed;
        await new Promise(/** 释放先前占用的监听端口。 */ resolve => lease.close(resolve));
        child = spawn(driver, ['relay', report], { windowsHide: true, stdio: 'ignore', env: { ...process.env,
            CHAT_RELAY_CONFIG: JSON.stringify({ port, backend: backend.address().port, dropUuid, replayUuid }) } });
        const exited = new Promise(/** 观察正式中继驱动的退出或启动错误。 */ resolve => { child.once('exit', resolve); child.once('error', resolve); });
        child.done = exited;
        await poll(/** 读取报告并等待中继声明就绪。 */ async () => JSON.parse(fs.readFileSync(report)).ready, 4000);
        const connect = /** 连接中继并登记所属套接字，连接失败传播给调用者。 */ async () => {
            const socket = net.connect(port, '127.0.0.1'); sockets.add(socket); socket.on('error', /** 吸收已由连接等待或后续断言处理的套接字错误。 */ () => {});
            await new Promise(/** 等待中继 TCP 连接建立或报告失败。 */ (resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject); });
            return socket;
        };
        const first = await connect();
        let received = Buffer.alloc(0);
        first.on('data', /** 累计首条连接收到的字节供 ACK 丢弃断言。 */ bytes => { received = Buffer.concat([received, bytes]); });
        first.write(frame(1016, { uuid: dropUuid }));
        await poll(/** 等待 ACK 丢弃策略关闭首条连接。 */ async () => first.destroyed, 3000);
        assert.equal(received.length, 0);
        const evidence = JSON.parse(fs.readFileSync(report));
        assert.equal(evidence.droppedAck, true); assert.equal(evidence.committedId, '81');
        const second = await connect();
        received = Buffer.alloc(0);
        second.on('data', /** 累计恢复连接收到的协议字节。 */ bytes => { received = Buffer.concat([received, bytes]); });
        second.write(frame(1016, { uuid: dropUuid }));
        const ack = frame(1017, { error: 0, uuid_msgId: [{ msg_uuid: dropUuid, message_id: 81 }] });
        await poll(/** 等待完整 ACK 字节到达后核对内容。 */ async () => received.length >= ack.length, 3000);
        assert.deepEqual(received, ack);
        received = Buffer.alloc(0);
        second.write(frame(1018, { uuid: replayUuid }));
        const notification = frame(1018, { error: 0, msgs: [{ msg_uuid: replayUuid, message_id: 82 }] });
        await poll(/** 等待重放产生的两份通知字节到达。 */ async () => received.length >= notification.length * 2, 3000);
        assert.deepEqual(received, Buffer.concat([notification, notification]));
        assert.equal(JSON.parse(fs.readFileSync(report)).replayedNotification, true);
    } finally {
        for (const socket of sockets) socket.destroy();
        if (child) { child.kill(); await child.done; }
        if (backend.listening) await new Promise(/** 关闭所属合成后端并等待完成。 */ resolve => backend.close(resolve));
        if (lease.listening) await new Promise(/** 异常清理路径释放原端口保留监听器。 */ resolve => lease.close(resolve));
        fs.rmSync(root, { recursive: true });
    }
});
