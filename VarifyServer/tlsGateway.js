'use strict';

const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const tls = require('node:tls');

/** 校验固定 TLS 入口和同机上游；不允许把解密后流量转发到远端主机。 */
function validate(config) {
    if (!config || !Array.isArray(config.listeners) || !config.listeners.length
        || config.listeners.length > 32 || typeof config.cert !== 'string' || typeof config.key !== 'string') {
        throw new Error('invalid TLS gateway configuration');
    }
    const names = new Set();
    for (const listener of config.listeners) {
        if (!listener || !/^[a-z][a-z0-9-]{0,31}$/.test(listener.name) || names.has(listener.name)
            || !net.isIP(listener.host) || !Number.isInteger(listener.port) || listener.port < 1 || listener.port > 65535
            || !['127.0.0.1', '::1'].includes(listener.targetHost)
            || !Number.isInteger(listener.targetPort) || listener.targetPort < 1 || listener.targetPort > 65535) {
            throw new Error('invalid TLS listener configuration');
        }
        names.add(listener.name);
    }
    if (!names.has('gate') || !names.has('resource') || ![...names].some(/** 查找至少一个聊天入口。 */ name => /^chat-/.test(name))) {
        throw new Error('TLS gateway must cover Gate, Resource and Chat');
    }
}

/** 创建有界 TLS 转发器；调用 start 后监听，close 先停止接入再销毁全部自有连接。 */
function createGateway(config, directory, logger = console) {
    validate(config);
    const options = {
        cert: fs.readFileSync(path.resolve(directory, config.cert)),
        key: fs.readFileSync(path.resolve(directory, config.key)),
        minVersion: 'TLSv1.2', handshakeTimeout: 10000,
        // 内部 TCP/HTTP 服务只接收来自本机入口的流量，客户端认证仍由应用协议负责。
        requestCert: false
    };
    tls.createSecureContext(options);
    const servers = [];
    const sockets = new Set();
    let closed = false;
    let started = false;

    /** 跟踪本实例拥有的连接，断开后释放跟踪记录。 */
    function own(socket) {
        sockets.add(socket);
        socket.once('close', /** 连接关闭后释放本实例的跟踪引用。 */ () => sockets.delete(socket));
        return socket;
    }

    /** 停止全部监听及握手/转发中的连接；重复调用安全。 */
    async function close() {
        closed = true;
        const pending = servers.map(/** 等待每个自有监听器停止接入。 */ server => new Promise(/** 将监听关闭回调转换为完成结果。 */ resolve => {
            if (!server.listening) { resolve(); return; }
            server.close(/** 监听已释放。 */ () => resolve());
        }));
        for (const socket of sockets) socket.destroy();
        await Promise.all(pending);
    }

    /** 顺序绑定所有入口；任一绑定失败时回滚本次已经建立的监听器。 */
    async function start() {
        if (closed || started) throw new Error('TLS gateway already started or closed');
        started = true;
        try {
            for (const listener of config.listeners) {
                const server = tls.createServer(options, /** TLS 认证通过后创建唯一上游并成对管理生命周期。 */ client => {
                    if (closed) { client.destroy(); return; }
                    const upstream = own(net.createConnection({ host: listener.targetHost, port: listener.targetPort }));
                    const deadline = setTimeout(/** 上游建连超过期限时关闭双端。 */ () => { client.destroy(); upstream.destroy(); }, 5000);
                    deadline.unref();
                    const stop = /** 任一端失败或空闲超时均结束整条通道。 */ () => { clearTimeout(deadline); client.destroy(); upstream.destroy(); };
                    client.once('error', stop);
                    upstream.once('error', stop);
                    client.once('close', stop);
                    upstream.once('close', /** 正常 EOF 由 pipe 结束 TLS 写流，保留尚未刷出的响应字节。 */ () => {
                        if (!upstream.readableEnded) stop();
                    });
                    client.setTimeout(120000, stop);
                    upstream.setTimeout(120000, stop);
                    upstream.once('connect', /** 建连成功后以流背压转发并清除建连期限。 */ () => {
                        clearTimeout(deadline);
                        if (closed || client.destroyed) { stop(); return; }
                        client.pipe(upstream).pipe(client);
                    });
                });
                server.maxConnections = 1024;
                server.on('connection', /** 握手前就登记原始连接，以便关闭可覆盖未认证连接。 */ socket => {
                    own(socket); socket.on('error', /** 握手失败由 TLS 事件记录固定诊断。 */ () => {});
                });
                let lastRejection = 0;
                server.on('tlsClientError', /** 每个入口每分钟最多一条固定诊断，避免握手洪泛写满日志。 */ () => {
                    if (Date.now() - lastRejection >= 60000) {
                        lastRejection = Date.now(); logger.warn('TLS handshake rejected: ' + listener.name);
                    }
                });
                server.on('error', /** 只记录已校验的入口名称。 */ () => logger.error('TLS listener error: ' + listener.name));
                servers.push(server);
                await new Promise(/** 就绪或绑定失败仅完成一次启动操作。 */ (resolve, reject) => {
                    server.once('error', reject);
                    server.listen(listener.port, listener.host, /** 监听就绪后移除本次启动错误回调。 */ () => {
                        server.removeListener('error', reject);
                        resolve();
                    });
                });
            }
        } catch (error) {
            await close();
            throw error;
        }
    }
    return { start, close };
}

/** 加载显式配置并运行 TLS 入口；启动失败只输出固定诊断，不回显证书或配置内容。 */
async function main(args = process.argv.slice(2)) {
    let gateway;
    try {
        if (args.length !== 2 || args[0] !== '--config') throw new Error('usage');
        const filename = path.resolve(args[1]);
        const config = JSON.parse(fs.readFileSync(filename, 'utf8'));
        gateway = createGateway(config, path.dirname(filename));
        await gateway.start();
        console.log('TLS gateway ready');
        const stop = /** 收到退出信号后关闭自有连接并传播清理失败。 */ () => gateway.close().catch(
            /** 清理失败通过退出码反映。 */ () => { process.exitCode = 1; });
        process.once('SIGINT', stop);
        process.once('SIGTERM', stop);
    } catch {
        if (gateway) await gateway.close();
        console.error('TLS gateway startup failed; check configuration, certificate, key and listener ports');
        process.exitCode = 1;
    }
}

module.exports = { createGateway, validate, main };
if (require.main === module) void main();
