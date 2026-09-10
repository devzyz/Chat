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
function run(args, env = {}) {
    return new Promise(resolve => {
        const child = spawn(driver, args, { windowsHide: true,
            env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = '';
        const timer = setTimeout(() => child.kill(), 10000);
        child.stdout.on('data', bytes => { output += bytes; }); child.stderr.resume();
        child.on('error', () => { clearTimeout(timer); resolve({ code: -1, output }); });
        child.on('close', code => { clearTimeout(timer); resolve({ code, output }); });
    });
}
test('production harness reaps a real child and records safe completion', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-driver-'));
    try {
        const report = path.join(root, 'report.json');
        const result = await run(['supervise', process.execPath, root, report, '-e', 'process.exit(0)']);
        assert.equal(result.code, 0);
        assert.deepEqual(JSON.parse(fs.readFileSync(report)), { complete: true, escalated: false, exitCode: 0 });
    } finally { fs.rmSync(root, { recursive: true }); }
});
for (const id of [1006, 1008]) test(`wire response ${id} validates production frame identity`, async () => {
    const sockets = new Set();
    const server = net.createServer(socket => {
        sockets.add(socket); socket.on('error', () => {}); socket.once('close', () => sockets.delete(socket));
        socket.once('data', () => {
            const body = Buffer.from('{"error":1010,"token":"not-for-evidence"}');
            const header = Buffer.alloc(4); header.writeUInt16BE(id); header.writeUInt16BE(body.length, 2);
            socket.end(Buffer.concat([header, body]));
        });
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    try {
        const result = await run(['chat'], { CHAT_FOUR_WIRE: JSON.stringify({ port: server.address().port,
            login: { uid: 1, token: 'not-for-evidence' }, requests: [] }) });
        assert.equal(result.code, id === 1006 ? 0 : 1);
        assert.ok(!result.output.includes('not-for-evidence'));
    } finally { for (const socket of sockets) socket.destroy(); await new Promise(resolve => server.close(resolve)); }
});
