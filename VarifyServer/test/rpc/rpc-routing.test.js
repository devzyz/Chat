'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const grpc = require('@grpc/grpc-js');

const messageProto = require('../../proto');
const { createServer } = require('../../server');

async function startLoopbackServer(handler) {
    const server = createServer(handler);
    const port = await new Promise((resolve, reject) => {
        server.bindAsync(
            '127.0.0.1:0',
            grpc.ServerCredentials.createInsecure(),
            (error, boundPort) => error ? reject(error) : resolve(boundPort)
        );
    });
    return { server, port };
}

function callGetVarifyCode(client, email, deadline) {
    return new Promise((resolve, reject) => {
        client.GetVarifyCode({ email }, { deadline }, (error, response) => {
            if (error) {
                reject(error);
                return;
            }
            resolve(response);
        });
    });
}

// T05-GRPC-01
test('loopback service routes GetVarifyCode to the registered handler', { timeout: 5000 }, async (t) => {
    const calls = [];
    const { server, port } = await startLoopbackServer((call, callback) => {
        calls.push(call.request.email);
        callback(null, { error: 0, email: call.request.email, code: 'TEST' });
    });
    const client = new messageProto.VarifyService(
        `127.0.0.1:${port}`,
        grpc.credentials.createInsecure()
    );
    t.after(() => {
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
