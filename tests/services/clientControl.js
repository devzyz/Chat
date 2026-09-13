'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

class ClientControl {
    constructor(root, name) {
        assert.match(name, /^[a-z]+$/);
        this.endpoint = process.platform === 'win32' ? `\\\\.\\pipe\\chat3d-${randomUUID()}` : path.join(root, `${name}.sock`);
        this.sequence = 0;
        this.pending = new Map();
        this.buffer = '';
        this.server = net.createServer(socket => {
            if (this.socket) { socket.destroy(); return; }
            this.socket = socket;
            socket.setEncoding('utf8');
            socket.on('data', chunk => this.receive(chunk));
            socket.on('error', () => this.fail());
            socket.on('close', () => this.fail());
        });
        this.server.on('error', () => this.fail());
    }
    async listen() {
        await new Promise((resolve, reject) => {
            this.server.once('error', reject);
            this.server.listen(this.endpoint, resolve);
        });
        if (process.platform !== 'win32') fs.chmodSync(this.endpoint, 0o600);
    }
    wait(key, timeout = 10000) {
        if (this.failed || this.pending.has(key)) return Promise.reject(new Error('client-control-unavailable'));
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(key);
                reject(new Error('client-control-deadline'));
                this.fail();
            }, timeout);
            this.pending.set(key, { resolve, reject, timer });
        });
    }
    ready() {
        return this.readyState ? Promise.resolve(this.readyState) : this.wait('ready');
    }
    receive(chunk) {
        try {
            this.buffer += chunk;
            assert.ok(Buffer.byteLength(this.buffer) <= 65536);
            let end;
            while ((end = this.buffer.indexOf('\n')) >= 0) {
                const value = JSON.parse(this.buffer.slice(0, end));
                this.buffer = this.buffer.slice(end + 1);
                assert.ok(value && typeof value === 'object' && !Array.isArray(value));
                const key = value.event === 'ready' ? 'ready' : value.id;
                if (key === 'ready') {
                    assert.ok(!this.readyState && value.format === 1 && Number.isSafeInteger(value.pid) && value.pid > 0);
                    this.readyState = value;
                } else assert.ok(Number.isSafeInteger(key) && this.pending.has(key));
                const waiter = this.pending.get(key);
                if (waiter) {
                    clearTimeout(waiter.timer);
                    this.pending.delete(key);
                    waiter.resolve(value);
                }
            }
        } catch { this.fail(); }
    }
    command(command, fields = {}) {
        if (!this.socket || this.failed || this.pending.size >= 16) return Promise.reject(new Error('client-control-unavailable'));
        const id = ++this.sequence;
        const bytes = JSON.stringify({ ...fields, id, command }) + '\n';
        if (Buffer.byteLength(bytes) > 8192) return Promise.reject(new Error('client-control-input-limit'));
        const result = this.wait(id);
        this.socket.write(bytes);
        return result;
    }
    fail() {
        this.failed = true;
        for (const waiter of this.pending.values()) {
            clearTimeout(waiter.timer);
            waiter.reject(new Error('client-control-unavailable'));
        }
        this.pending.clear();
        this.socket?.destroy();
    }
    async close() {
        this.fail();
        if (this.server.listening) await new Promise((resolve, reject) => this.server.close(error => error ? reject(error) : resolve()));
    }
}

module.exports = { ClientControl };
