'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const grpc = require('@grpc/grpc-js');

const { startServer } = require('../../server');

// V07-START-01
test('bind failure rejects startup and never starts the server', async () => {
    let starts = 0;
    const server = {
        bindAsync(address, credentials, callback) {
            callback(new Error('address already in use'), 0);
        },
        start() {
            starts += 1;
        }
    };

    await assert.rejects(
        startServer({
            server,
            address: '127.0.0.1:50051',
            credentials: grpc.ServerCredentials.createInsecure(),
            logger: { log() {}, error() {} }
        }),
        /address already in use/
    );
    assert.equal(starts, 0);
});

// V07-START-02
test('zero bound port rejects startup and never starts the server', async () => {
    let starts = 0;
    const server = {
        bindAsync(address, credentials, callback) {
            callback(null, 0);
        },
        start() {
            starts += 1;
        }
    };

    await assert.rejects(
        startServer({
            server,
            address: '127.0.0.1:0',
            credentials: grpc.ServerCredentials.createInsecure(),
            logger: { log() {}, error() {} }
        }),
        /did not bind a port/
    );
    assert.equal(starts, 0);
});

// V07-START-03
test('successful bind starts once and exposes the actual bound port', async () => {
    let starts = 0;
    const server = {
        bindAsync(address, credentials, callback) {
            callback(null, 43210);
        },
        start() {
            starts += 1;
        }
    };

    const port = await startServer({
        server,
        address: '127.0.0.1:0',
        credentials: grpc.ServerCredentials.createInsecure(),
        logger: { log() {}, error() {} }
    });

    assert.equal(port, 43210);
    assert.equal(starts, 1);
});
