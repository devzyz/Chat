'use strict';

const grpc = require('@grpc/grpc-js');
const { createServer } = require('../../server');

const server = createServer((call, callback) => {
    callback(null, { error: 0, email: call.request.email, code: '' });
});

let stopped = false;
function stop(exitCode = 0) {
    if (stopped) {
        return;
    }
    stopped = true;
    server.forceShutdown();
    process.exitCode = exitCode;
}

const hardTimeout = setTimeout(() => stop(2), 10000);
process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => {
    if (chunk.includes('STOP')) {
        clearTimeout(hardTimeout);
        stop(0);
    }
});
process.on('SIGTERM', () => stop(0));

server.bindAsync(
    '127.0.0.1:0',
    grpc.ServerCredentials.createInsecure(),
    (error, port) => {
        if (error || !Number.isInteger(port) || port <= 0) {
            process.stderr.write('loopback bind failed\n');
            stop(1);
            return;
        }
        process.stdout.write(`READY ${port}\n`);
    }
);
