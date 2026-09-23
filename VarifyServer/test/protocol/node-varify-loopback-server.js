'use strict';

const grpc = require('@grpc/grpc-js');
const { createServer } = require('../../server');

const server = createServer(/** 回显请求邮箱并返回空验证码，供跨语言线路合同验证。 */ (call, callback) => {
    callback(null, { error: 0, email: call.request.email, code: '' });
});

let stopped = false;
/** 幂等关闭回环 gRPC 服务并设置退出状态。 */ function stop(exitCode = 0) {
    if (stopped) {
        return;
    }
    stopped = true;
    server.forceShutdown();
    process.exitCode = exitCode;
}

const hardTimeout = setTimeout(/** 达到进程总期限后以失败状态停服。 */ () => stop(2), 10000);
process.stdin.setEncoding('utf8');
process.stdin.on('data', /** 接收父进程停止命令并清除总期限计时器。 */ (chunk) => {
    if (chunk.includes('STOP')) {
        clearTimeout(hardTimeout);
        stop(0);
    }
});
process.on('SIGTERM', /** 标准输入关闭时停止测试服务。 */ () => stop(0));

server.bindAsync(
    '127.0.0.1:0',
    grpc.ServerCredentials.createInsecure(),
    /** 绑定成功后发布真实端口；失败仅输出安全诊断并停服。 */ (error, port) => {
        if (error || !Number.isInteger(port) || port <= 0) {
            process.stderr.write('loopback bind failed\n');
            stop(1);
            return;
        }
        process.stdout.write(`READY ${port}\n`);
    }
);
