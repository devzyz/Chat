'use strict';
const net = require('node:net');
const http = require('node:http');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const { randomUUID } = require('node:crypto');

/** 编码生产四字节大端帧，拒绝超过客户端请求上限的数据。 */
function encode(id, value) {
    const body = Buffer.from(JSON.stringify(value));
    assert.ok(body.length > 0 && body.length <= 2048, 'request-size');
    const header = Buffer.alloc(4);
    header.writeUInt16BE(id); header.writeUInt16BE(body.length, 2);
    return Buffer.concat([header, body]);
}

/** 增量解析分片及合包，按生产响应上限限制缓冲区。 */
class Decoder {
    /** 初始化未完成帧缓冲。 */
    constructor() { this.buffer = Buffer.alloc(0); }
    /** 交付完整 JSON 帧；畸形数据立即失败，不保留无限输入。 */
    feed(chunk, receive) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (this.buffer.length >= 4) {
            const id = this.buffer.readUInt16BE(0);
            const size = this.buffer.readUInt16BE(2);
            const limit = id === 1028 ? 65535 : [1006, 1017, 1047].includes(id) ? 8192 : 2048;
            assert.ok(size > 0 && size <= limit, 'response-size');
            if (this.buffer.length < size + 4) break;
            const value = JSON.parse(this.buffer.subarray(4, size + 4).toString());
            this.buffer = this.buffer.subarray(size + 4);
            receive(id, value);
        }
    }
}

/** 拥有一个真实 TCP 会话、有限在途请求及心跳；错误传播至所有等待者。 */
class Client extends EventEmitter {
    /** 保存连接身份和默认期限，不自动连接。 */
    constructor(port, uid, token, timeoutMs = 10000) {
        super(); this.port = port; this.uid = uid; this.token = token; this.timeoutMs = timeoutMs;
        this.pending = new Map(); this.decoder = new Decoder(); this.serial = 0; this.closed = false;
    }
    /** 通过生产登录协议认证并启动心跳。 */
    async connect() {
        this.socket = net.createConnection({ host: '127.0.0.1', port: this.port });
        this.socket.setNoDelay(true);
        this.socket.on('error', /** 将连接错误转换为不含凭据的固定诊断。 */ () => this.fail(new Error('tcp-error')));
        this.socket.on('close', /** 关闭连接时使全部未完成请求失败。 */ () => this.fail(new Error('tcp-closed')));
        this.socket.on('data', /** 解析完整帧并分发，解析失败关闭连接。 */ data => {
            try { this.decoder.feed(data, /** 按协议标识分发完整帧。 */ (id, value) => this.receive(id, value)); }
            catch { this.fail(new Error('invalid-frame')); }
        });
        await this.request(1005, 1006, { uid: this.uid, token: this.token,
            capabilities: ['sync_v1', 'group_membership_v1', 'basic_social_v1'] });
        this.heartbeat = setInterval(/** 维持生产会话存活并传播心跳失败。 */ () => {
            this.request(1020, 1021, { uid: this.uid }).catch(/** 心跳失败终止会话。 */ error => this.fail(error));
        }, 5000);
        return this;
    }
    /** 优先匹配请求，其余业务通知通过事件交付。 */
    receive(id, value) {
        for (const [key, item] of this.pending) {
            if (item.id === id && item.match(value)) {
                clearTimeout(item.timer); this.pending.delete(key);
                if (value.error !== 0) item.reject(new Error(`business-${id}-${Number(value.error)}`));
                else item.resolve(value);
                return;
            }
        }
        if (id === 1019) this.fail(new Error('session-replaced'));
        else this.emit('notification', id, value);
    }
    /** 有界写入并关联响应，拒绝重复无关联请求和过量积压。 */
    request(id, responseId, value, match = null) {
        if (this.closed) return Promise.reject(new Error('client-closed'));
        if (this.pending.size >= 64 || this.socket.writableLength > 65536) return Promise.reject(new Error('client-backpressure'));
        if (!match && [...this.pending.values()].some(/** 查找没有关联键的同类在途请求。 */ item => item.id === responseId)) {
            return Promise.reject(new Error('ambiguous-request'));
        }
        const frame = encode(id, value);
        return new Promise(/** 登记响应等待及截止计时器后写入帧。 */ (resolve, reject) => {
            const key = ++this.serial;
            const timer = setTimeout(/** 请求到期时撤销等待并拒绝本次请求。 */ () => {
                this.pending.delete(key); reject(new Error(`request-timeout-${responseId}`));
            }, this.timeoutMs);
            this.pending.set(key, { id: responseId, match: match || (/** 无歧义响应直接匹配。 */ () => true), resolve, reject, timer });
            this.socket.write(frame, /** 写入失败终止连接；成功仍等待业务响应。 */ error => { if (error) this.fail(new Error('write-failed')); });
        });
    }
    /** 发送带请求身份的同步或群业务请求。 */
    correlated(id, value) {
        const requestId = randomUUID();
        return this.request(id, id + 1, { ...value, request_id: requestId },
            /** 只接受本次请求的响应。 */ response => response.request_id === requestId);
    }
    /** 关闭并拒绝所有等待，保留安全诊断用于报告。 */
    fail(error) {
        if (this.closed) return;
        this.closed = true; this.failure = error.message; clearInterval(this.heartbeat);
        for (const item of this.pending.values()) { clearTimeout(item.timer); item.reject(error); }
        this.pending.clear(); this.socket?.destroy(); this.emit('stopped');
    }
    /** 主动结束所属连接。 */
    close() { this.fail(new Error('client-shutdown')); }
}

/** 执行 loopback JSON HTTP 请求并验证真实业务结果。 */
async function post(url, value, timeoutMs = 10000) {
    assert.equal(new URL(url).hostname, '127.0.0.1');
    const body = Buffer.from(JSON.stringify(value));
    // Gate closes every response. A one-shot connection avoids idle speculative pool sockets; never retry a login.
    const result = await new Promise(/** 每次请求拥有单独连接和完整响应期限。 */ (resolve, reject) => {
        const request = http.request(url, { method: 'POST', agent: false,
            headers: { 'Content-Type': 'application/json', 'Content-Length': body.length, Connection: 'close' } },
        /** 累积有界 JSON 响应，拒绝重定向和不完整正文。 */ response => {
            const chunks = []; let size = 0;
            response.on('data', /** 限制响应内存占用。 */ chunk => {
                size += chunk.length;
                if (size > 1048576) request.destroy(new Error('http-response-size'));
                else chunks.push(chunk);
            });
            response.on('error', /** 提前断开不可计为成功。 */ () => reject(new Error('http-response-incomplete')));
            response.on('end', /** HTTP 与 JSON 合同全部满足后才完成请求。 */ () => {
                clearTimeout(timer);
                if (response.statusCode !== 200) { reject(new Error(`http-${response.statusCode}`)); return; }
                try { resolve(JSON.parse(Buffer.concat(chunks).toString())); }
                catch { reject(new Error('http-invalid-json')); }
            });
        });
        const timer = setTimeout(/** 总期限包含连接及响应读取。 */ () => request.destroy(new Error('http-timeout')), timeoutMs);
        request.on('error', /** 只输出安全网络错误类别，不包含账号正文。 */ error => {
            clearTimeout(timer); reject(new Error(error.message === 'http-timeout' ? 'http-timeout' : 'http-transport'));
        });
        request.end(body);
    });
    if (result.error !== 0) throw new Error(`gate-${Number(result.error)}`);
    return result;
}
module.exports = { encode, Decoder, Client, post };
