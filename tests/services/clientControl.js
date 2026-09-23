'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

/** 管理单一 Qt 测试客户端的本地控制通道，限制消息、并发请求与等待时长。 */
class ClientControl {
    /** 生成所属管道或套接字端点，初始化请求关联并注册连接故障处理。 */
    constructor(root, name) {
        assert.match(name, /^[a-z]+$/);
        this.endpoint = process.platform === 'win32' ? `\\\\.\\pipe\\chat3d-${randomUUID()}` : path.join(root, `${name}.sock`);
        this.sequence = 0;
        this.pending = new Map();
        this.buffer = '';
        this.server = net.createServer(/** 仅接纳首个控制客户端，绑定接收与断连处理。 */ socket => {
            if (this.socket) { socket.destroy(); return; }
            this.socket = socket;
            socket.setEncoding('utf8');
            socket.on('data', /** 把 UTF-8 数据交给控制消息解析器。 */ chunk => this.receive(chunk));
            socket.on('error', /** 连接错误时失败所有未完成请求。 */ () => this.fail());
            socket.on('close', /** 连接关闭时失败所有未完成请求。 */ () => this.fail());
        });
        this.server.on('error', /** 监听器错误时终止控制通道。 */ () => this.fail());
    }
    /** 启动本地控制端点，POSIX 下限制为当前用户访问。 */
    async listen() {
        await new Promise(/** 等待监听成功或传播启动错误。 */ (resolve, reject) => {
            this.server.once('error', reject);
            this.server.listen(this.endpoint, resolve);
        });
        if (process.platform !== 'win32') fs.chmodSync(this.endpoint, 0o600);
    }
    /** 登记唯一关联键的有界等待；通道失败或重复键时拒绝。 */
    wait(key, timeout = 10000) {
        if (this.failed || this.pending.has(key)) return Promise.reject(new Error('client-control-unavailable'));
        return new Promise(/** 保存等待者及截止计时器，供匹配响应完成。 */ (resolve, reject) => {
            const timer = setTimeout(/** 等待超时后移除请求并关闭整个控制通道。 */ () => {
                this.pending.delete(key);
                reject(new Error('client-control-deadline'));
                this.fail();
            }, timeout);
            this.pending.set(key, { resolve, reject, timer });
        });
    }
    /** 返回已收到的就绪状态，或等待首条就绪事件。 */
    ready() {
        return this.readyState ? Promise.resolve(this.readyState) : this.wait('ready');
    }
    /** 有界解析换行 JSON 并匹配就绪或请求响应，畸形及未知响应导致失败。 */
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
    /** 生成请求编号并发送有界命令，返回对应响应的 Promise。 */
    command(command, fields = {}) {
        if (!this.socket || this.failed || this.pending.size >= 16) return Promise.reject(new Error('client-control-unavailable'));
        const id = ++this.sequence;
        const bytes = JSON.stringify({ ...fields, id, command }) + '\n';
        if (Buffer.byteLength(bytes) > 8192) return Promise.reject(new Error('client-control-input-limit'));
        const result = this.wait(id);
        this.socket.write(bytes);
        return result;
    }
    /** 标记通道失败、拒绝所有等待者并销毁套接字。 */
    fail() {
        this.failed = true;
        for (const waiter of this.pending.values()) {
            clearTimeout(waiter.timer);
            waiter.reject(new Error('client-control-unavailable'));
        }
        this.pending.clear();
        this.socket?.destroy();
    }
    /** 失败未完成请求并等待所属监听器关闭。 */
    async close() {
        this.fail();
        if (this.server.listening) await new Promise(/** 把监听器关闭转换为可等待的成功或失败。 */ (resolve, reject) => this.server.close(/** 传播监听器关闭错误，否则完成关闭等待。 */ error => error ? reject(error) : resolve()));
    }
}

module.exports = { ClientControl };
