'use strict';
const fs = require('node:fs');
const path = require('node:path');
const tls = require('node:tls');
const { createCertificate } = require('./certificate');

// Qt 测试拥有进程和临时目录；只返回端口，TLS 内容按原字节回显。
createCertificate(process.argv[2]);
const options = {
    key: fs.readFileSync(path.join(process.argv[2], 'key.pem')),
    cert: fs.readFileSync(path.join(process.argv[2], 'cert.pem')),
    minVersion: 'TLSv1.2'
};
const peers = new Set();
const echo = /** 回显通过 TLS 握手的应用帧并消化预期断线错误。 */ socket => {
    peers.add(socket); socket.once('close', /** 移除已断开的夹具连接。 */ () => peers.delete(socket));
    socket.on('error', /** 证书拒绝和测试关闭不产生进程异常。 */ () => {});
    socket.pipe(socket);
};
const server = tls.createServer(options, echo);
let ipv6;
server.listen(0, '127.0.0.1', /** 同时监听两个 loopback 地址，避免 localhost 的 IPv6 回退延迟影响 TLS 断言。 */ () => {
    ipv6 = tls.createServer(options, echo);
    ipv6.listen(server.address().port, '::1', /** 两个 loopback 入口均就绪后才报告端口。 */ () => console.log(server.address().port));
});
process.stdin.once('data', /** 拥有者写入停止命令后释放全部监听和已认证连接。 */ () => {
    server.close(); if (ipv6) ipv6.close();
    for (const peer of peers) peer.destroy();
    process.stdin.destroy();
});
