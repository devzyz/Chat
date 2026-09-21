'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const { performance } = require('node:perf_hooks');
const { createSmtpAdapter } = require('../../email');

async function withPeer(mode, action) {
    const sockets = new Set();
    const timers = new Set();
    const server = net.createServer((socket) => {
        sockets.add(socket);
        socket.on('error', () => {});
        socket.once('close', () => sockets.delete(socket));
        if (mode === 'silent') return;
        socket.write(mode === 'rejectGreeting' ? '554 synthetic rejection\r\n' : '220 fixture ESMTP\r\n');
        if (mode === 'rejectGreeting') return;
        let buffer = '';
        let data = false;
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
                        const timer = setInterval(() => socket.write('354-waiting\r\n'), 20);
                        timers.add(timer);
                        socket.once('close', () => { clearInterval(timer); timers.delete(timer); });
                    } else { data = true; socket.write('354 send data\r\n'); }
                } else if (line === 'QUIT') socket.end('221 closing\r\n');
            }
        });
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    const hardStop = setTimeout(() => { for (const socket of sockets) socket.destroy(); }, 4000);
    try { await action(server.address().port, sockets); }
    finally {
        clearTimeout(hardStop);
        for (const timer of timers) clearInterval(timer);
        for (const socket of sockets) socket.destroy();
        await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    }
}

async function awaitClosed(sockets) {
    await Promise.all([...sockets].map((socket) => new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('SMTP socket cleanup deadline')), 1000);
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
].map(([id, name, mode, expected]) => ({ id, name, async action() {
    await withPeer(mode, async (port, sockets) => {
        const adapter = createSmtpAdapter({ host: '127.0.0.1', port, secure: false,
            auth: 'none', deadlineMs: 250 });
        let completions = 0;
        const start = performance.now();
        try {
            const pending = adapter.SendMail({ from: 'sender@example.invalid', to: 'fault@example.invalid',
                subject: id, text: 'synthetic' }).then((result) => { completions += 1; return result; });
            if (id === 'V09-SMTP-11') {
                // Await a real accepted socket, not a fixed sleep or mocked transport.
                await new Promise((resolve, reject) => {
                    const deadline = performance.now() + 1000;
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
