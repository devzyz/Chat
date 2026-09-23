'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const grpc = require('@grpc/grpc-js');

const { startServer, getBindAddress, main } = require('../../server');

// V07-START-01
test('bind failure rejects startup and never starts the server', /** 验证默认及显式绑定地址，并在非法地址时拒绝启动。 */ async (t) => {
    assert.equal(getBindAddress({}), '0.0.0.0:50051');
    assert.equal(getBindAddress({ CHAT_VARIFY_BIND_ADDRESS: '127.0.0.1:32123' }), '127.0.0.1:32123');
    for (const address of ['', 'localhost:1234', '999.0.0.1:1234', '127.0.0.1:0', '127.0.0.1:65536']) {
        assert.throws(/** 以当前非法地址调用配置校验。 */ () => getBindAddress({ CHAT_VARIFY_BIND_ADDRESS: address }), /Invalid CHAT_VARIFY_BIND_ADDRESS/);
    }
    const previous = process.env.CHAT_VARIFY_BIND_ADDRESS;
    t.after(/** 恢复测试之前的绑定地址环境变量。 */ () => {
        if (previous === undefined) delete process.env.CHAT_VARIFY_BIND_ADDRESS;
        else process.env.CHAT_VARIFY_BIND_ADDRESS = previous;
    });
    process.env.CHAT_VARIFY_BIND_ADDRESS = '127.0.0.1:65536';
    let binds = 0;
    await assert.rejects(main({ server: { /** 记录绑定尝试次数，验证非法配置未触及服务。 */ bindAsync() { binds += 1; } } }), /Invalid CHAT_VARIFY_BIND_ADDRESS/);
    assert.equal(binds, 0);
    let starts = 0;
    const server = {
        /** 通过绑定回调模拟端口占用错误。 */ bindAsync(address, credentials, callback) {
            callback(new Error('address already in use'), 0);
        },
        /** 记录服务启动次数以检测错误后的误启动。 */ start() {
            starts += 1;
        }
    };

    await assert.rejects(
        startServer({
            server,
            address: '127.0.0.1:50051',
            credentials: grpc.ServerCredentials.createInsecure(),
            logger: { /** 丢弃夹具普通日志。 */ log() {}, /** 丢弃夹具错误日志。 */ error() {} }
        }),
        /address already in use/
    );
    assert.equal(starts, 0);
});

// V07-START-02
test('zero bound port rejects startup and never starts the server', /** 验证绑定返回零端口时启动失败且不调用 start。 */ async () => {
    let starts = 0;
    const server = {
        /** 模拟绑定未报错但未取得有效端口。 */ bindAsync(address, credentials, callback) {
            callback(null, 0);
        },
        /** 记录 start 调用供未启动断言。 */ start() {
            starts += 1;
        }
    };

    await assert.rejects(
        startServer({
            server,
            address: '127.0.0.1:0',
            credentials: grpc.ServerCredentials.createInsecure(),
            logger: { /** 丢弃夹具普通日志。 */ log() {}, /** 丢弃夹具错误日志。 */ error() {} }
        }),
        /did not bind a port/
    );
    assert.equal(starts, 0);
});

// V07-START-03
test('successful bind starts once and exposes the actual bound port', /** 验证有效绑定端口被返回且服务只启动一次。 */ async () => {
    let starts = 0;
    const server = {
        /** 模拟成功绑定并返回固定有效端口。 */ bindAsync(address, credentials, callback) {
            callback(null, 43210);
        },
        /** 累计服务启动次数。 */ start() {
            starts += 1;
        }
    };

    const port = await startServer({
        server,
        address: '127.0.0.1:0',
        credentials: grpc.ServerCredentials.createInsecure(),
        logger: { /** 丢弃夹具普通日志。 */ log() {}, /** 丢弃夹具错误日志。 */ error() {} }
    });

    assert.equal(port, 43210);
    assert.equal(starts, 1);
});
