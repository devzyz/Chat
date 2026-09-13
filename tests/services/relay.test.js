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

test('compiled production-codec relay drops one ACK and replays one framed notification', { timeout: 15000 }, async () => {
    const driver = process.env.CHAT_FOUR_DRIVER;
    assert.ok(driver && path.isAbsolute(driver));
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-relay-'));
    const report = path.join(root, 'relay.json');
    const dropUuid = randomUUID(), replayUuid = randomUUID();
    const sockets = new Set();
    const frame = (id, body) => {
        const bytes = Buffer.from(JSON.stringify(body));
        const header = Buffer.alloc(4);
        header.writeUInt16BE(id); header.writeUInt16BE(bytes.length, 2);
        return Buffer.concat([header, bytes]);
    };
    const backend = net.createServer(socket => {
        sockets.add(socket); socket.on('error', () => {});
        let input = Buffer.alloc(0);
        socket.on('data', bytes => {
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
        await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
        await new Promise(resolve => lease.listen(0, '127.0.0.1', resolve));
        const port = lease.address().port;
        await new Promise(resolve => lease.close(resolve));
        child = spawn(driver, ['relay', report], { windowsHide: true, stdio: 'ignore', env: { ...process.env,
            CHAT_RELAY_CONFIG: JSON.stringify({ port, backend: backend.address().port, dropUuid, replayUuid }) } });
        const exited = new Promise(resolve => { child.once('exit', resolve); child.once('error', resolve); });
        child.done = exited;
        await poll(async () => JSON.parse(fs.readFileSync(report)).ready, 4000);
        const connect = async () => {
            const socket = net.connect(port, '127.0.0.1'); sockets.add(socket); socket.on('error', () => {});
            await new Promise((resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject); });
            return socket;
        };
        const first = await connect();
        let received = Buffer.alloc(0);
        first.on('data', bytes => { received = Buffer.concat([received, bytes]); });
        first.write(frame(1016, { uuid: dropUuid }));
        await poll(async () => first.destroyed, 3000);
        assert.equal(received.length, 0);
        const evidence = JSON.parse(fs.readFileSync(report));
        assert.equal(evidence.droppedAck, true); assert.equal(evidence.committedId, '81');
        const second = await connect();
        received = Buffer.alloc(0);
        second.on('data', bytes => { received = Buffer.concat([received, bytes]); });
        second.write(frame(1016, { uuid: dropUuid }));
        const ack = frame(1017, { error: 0, uuid_msgId: [{ msg_uuid: dropUuid, message_id: 81 }] });
        await poll(async () => received.length >= ack.length, 3000);
        assert.deepEqual(received, ack);
        received = Buffer.alloc(0);
        second.write(frame(1018, { uuid: replayUuid }));
        const notification = frame(1018, { error: 0, msgs: [{ msg_uuid: replayUuid, message_id: 82 }] });
        await poll(async () => received.length >= notification.length * 2, 3000);
        assert.deepEqual(received, Buffer.concat([notification, notification]));
        assert.equal(JSON.parse(fs.readFileSync(report)).replayedNotification, true);
    } finally {
        for (const socket of sockets) socket.destroy();
        if (child) { child.kill(); await child.done; }
        if (backend.listening) await new Promise(resolve => backend.close(resolve));
        if (lease.listening) await new Promise(resolve => lease.close(resolve));
        fs.rmSync(root, { recursive: true });
    }
});
