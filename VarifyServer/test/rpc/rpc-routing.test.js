'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const grpc = require('@grpc/grpc-js');

const messageProto = require('../../proto');
const { createServer } = require('../../server');

/** 在随机回环端口启动注入处理器的真实 gRPC 服务并返回实例及端口。 */ async function startLoopbackServer(handler) {
    const server = createServer(handler);
    const port = await new Promise(/** 把异步服务绑定转换为可等待的 Promise。 */ (resolve, reject) => {
        server.bindAsync(
            '127.0.0.1:0',
            grpc.ServerCredentials.createInsecure(),
            /** 将绑定错误传播或返回实际监听端口。 */ (error, boundPort) => error ? reject(error) : resolve(boundPort)
        );
    });
    return { server, port };
}

/** 在给定期限内调用公开验证码 RPC 并返回响应 Promise。 */ function callGetVarifyCode(client, email, deadline) {
    return new Promise(/** 发起带期限的 RPC 并接收最终回调。 */ (resolve, reject) => {
        client.GetVarifyCode({ email }, { deadline }, /** 将传输错误拒绝或把响应交给等待者。 */ (error, response) => {
            if (error) {
                reject(error);
                return;
            }
            resolve(response);
        });
    });
}

// T05-GRPC-01
test('loopback service routes GetVarifyCode to the registered handler', { timeout: 5000 }, /** 验证真实回环 RPC 只委派一次且响应邮箱及验证码保持一致。 */ async (t) => {
    const calls = [];
    const { server, port } = await startLoopbackServer(/** 记录接收邮箱并返回固定验证码。 */ (call, callback) => {
        calls.push(call.request.email);
        callback(null, { error: 0, email: call.request.email, code: 'TEST' });
    });
    const client = new messageProto.VarifyService(
        `127.0.0.1:${port}`,
        grpc.credentials.createInsecure()
    );
    t.after(/** 关闭本用例拥有的 gRPC 客户端和服务。 */ () => {
        client.close();
        server.forceShutdown();
    });

    const response = await callGetVarifyCode(
        client,
        'loopback@example.test',
        Date.now() + 2000
    );

    assert.deepEqual(calls, ['loopback@example.test']);
    assert.equal(response.error, 0);
    assert.equal(response.email, 'loopback@example.test');
    assert.equal(response.code, 'TEST');
});
