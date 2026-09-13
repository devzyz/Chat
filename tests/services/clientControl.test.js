'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { ClientControl } = require('./clientControl');

test('Node controller owns two real Qt processes and preserves independent state', { timeout: 15000 }, async () => {
    const binary = process.env.CHAT_E2E_CLIENT;
    assert.ok(binary && path.isAbsolute(binary), 'same-source CHAT_E2E_CLIENT is required');
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-control-'));
    const owned = [];
    try {
        for (const name of ['alice', 'bob']) {
            const control = new ClientControl(root, name);
            await control.listen();
            const child = spawn(binary, ['--control', control.endpoint], { windowsHide: true, stdio: 'ignore' });
            const exited = new Promise((resolve, reject) => { child.once('error', reject); child.once('exit', resolve); });
            owned.push({ control, child, exited });
            const ready = await control.ready();
            assert.equal(ready.pid, child.pid);
            assert.equal((await control.command('snapshot')).active, false);
        }
        assert.notEqual(owned[0].child.pid, owned[1].child.pid);
        assert.equal((await owned[0].control.command('stop')).status, 'stopped');
        assert.equal(await owned[0].exited, 0);
        assert.equal((await owned[1].control.command('snapshot', { chatId: 7 })).messages.length, 0);
        assert.equal((await owned[1].control.command('stop')).status, 'stopped');
        assert.equal(await owned[1].exited, 0);
    } finally {
        for (const { control, child, exited } of owned.reverse()) {
            await control.close();
            if (child.exitCode === null) child.kill();
            await exited;
        }
        fs.rmSync(root, { recursive: true });
    }
});

test('controller rejects malformed, uncorrelated, oversized and stalled responses', { timeout: 5000 }, async () => {
    for (const payload of ['not-json\n', '{"id":99}\n', 'x'.repeat(65537), null]) {
        const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-control-'));
        const control = new ClientControl(root, 'fixture');
        let socket;
        try {
            await control.listen();
            socket = net.connect(control.endpoint);
            socket.on('error', () => {});
            await new Promise(resolve => socket.once('connect', resolve));
            const pending = control.wait('ready', 50);
            if (payload !== null) socket.write(payload);
            await assert.rejects(pending, /client-control-/);
            assert.equal(control.failed, true);
        } finally { socket?.destroy(); await control.close(); fs.rmSync(root, { recursive: true }); }
    }
});
