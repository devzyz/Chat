const grpc = require('@grpc/grpc-js');
const net = require('node:net');
const messageProto = require('./proto');
const constModule = require('./const');
const { v4: uuidv4 } = require('uuid');

/** 创建注入 Redis、SMTP、UUID 和日志依赖的验证码处理器，不启动监听。 */
function createGetVarifyCodeHandler({ redisModule, emailModule, senderEmail, generateUuid = uuidv4, logger = console }) {
    /** 处理外部 GetVarifyCode RPC；复用或缓存验证码后发送邮件，通过 callback 返回业务码，依赖回调不得抛异常。 */
    return async function GetVarifyCode(call, callback) {
        logger.log('verification request received');

        try {
            const key = constModule.code_prefix + call.request.email;
            const queryResult = await redisModule.getRedis(key);
            let uniqueId = queryResult;
            if (queryResult == null) {
                uniqueId = generateUuid();
                if (uniqueId.length > 4) {
                    uniqueId = uniqueId.substring(0, 4);
                }
                const stored = await redisModule.setRedisExpire(key, uniqueId, 600);

                if (!stored) {
                    callback(null, {
                        email: call.request.email,
                        error: constModule.Errors.RedisErr
                    });
                    return;
                }
            }
            const text = '您的验证码为' + uniqueId + '请十分钟内完成注册';
            const mailOptions = {
                from: senderEmail,
                to: call.request.email,
                subject: '验证码',
                text
            };

            const sendResult = await emailModule.sendMail(mailOptions);
            if (!sendResult || (typeof sendResult === 'object' && sendResult.status !== 'Delivered')) {
                callback(null, {
                    email: call.request.email,
                    error: constModule.Errors.Exception
                });
            } else {
                callback(null, {
                    email: call.request.email,
                    error: constModule.Errors.Success
                });
            }
        } catch (error) {
            const logError = typeof logger.error === 'function' ? logger.error.bind(logger) : logger.log.bind(logger);
            logError('verification request failed');
            callback(null, {
                email: call.request.email,
                error: constModule.Errors.Exception
            });
        }
    };
}

/** 按配置创建默认适配器并为处理器附加关闭入口。 */
function createDefaultHandler() {
    const { email_user, smtp, redis } = require('./config');
    const emailModule = require('./email').createSmtpAdapter(smtp);
    const redisModule = require('./redis').createRedisAdapter(redis);
    const handler = createGetVarifyCodeHandler({ redisModule, emailModule, senderEmail: email_user });
    handler.close =
        /** 关闭邮件发送并返回 Redis 适配器关闭 Promise。 */
        () => { emailModule.close(); return redisModule.close(); };
    return handler;
}

/** 创建 gRPC 服务并注册外部协议名称，尚不监听端口。 */
function createServer(handler = createDefaultHandler()) {
    const server = new grpc.Server();
    server.addService(messageProto.VarifyService.service, { GetVarifyCode: handler });
    server.closeAdapters =
        /** 关闭此服务持有的处理器适配器。 */
        () => handler.close?.();
    return server;
}

/** 异步绑定并启动服务；绑定错误或无效端口拒绝 Promise，成功返回实际端口。 */
function startServer({ server, address, credentials, logger = console }) {
    return new Promise(
        /** 把异步绑定结果转换为 Promise。 */
        (resolve, reject) => {
        server.bindAsync(address, credentials,
            /** 验证绑定错误和端口后才启动服务并完成 Promise。 */
            (error, boundPort) => {
            if (error) {
                reject(error);
                return;
            }
            if (!Number.isInteger(boundPort) || boundPort <= 0) {
                reject(new Error('gRPC server did not bind a port'));
                return;
            }
            server.start();
            logger.log(`grpc server started on port ${boundPort}`);
            resolve(boundPort);
        });
    });
}

/** 读取并验证 IPv4 监听地址及端口，非法值抛异常。 */
function getBindAddress(environment = process.env) {
    const address = environment.CHAT_VARIFY_BIND_ADDRESS ?? '0.0.0.0:50051';
    const match = /^(\d+\.\d+\.\d+\.\d+):(\d+)$/.exec(address);
    if (!match || !net.isIPv4(match[1]) || Number(match[2]) < 1 || Number(match[2]) > 65535) {
        throw new Error('Invalid CHAT_VARIFY_BIND_ADDRESS; expected IPv4:port (1..65535)');
    }
    return address;
}

/** 启动服务并返回实例；启动失败先关闭适配器再传播异常。 */
async function main({ server, logger = console } = {}) {
    const address = getBindAddress();
    server ??= createServer();
    return startServer({
        server,
        address,
        credentials: grpc.ServerCredentials.createInsecure(),
        logger
    }).then(
        /** 绑定启动完成后向调用方返回服务实例。 */
        () => server).catch(
        /** 启动失败时关闭已经创建的适配器并重新抛错。 */
        async (error) => {
        await server.closeAdapters?.();
        throw error;
    });
}

if (require.main === module) {
    main().then(
        /** 服务就绪后登记进程信号和幂等关闭流程。 */
        (server) => {
        let closing = false;
        const stop =
            /** 先取消适配器 I/O 再关闭 gRPC，十秒期限后强制结束。 */
            async () => {
            if (closing) return;
            closing = true;
            await server.closeAdapters?.();
            const deadline = setTimeout(
                /** 优雅关闭超过期限时强制关闭 gRPC。 */
                () => server.forceShutdown(), 10000);
            server.tryShutdown(
                /** 优雅关闭完成后取消强制关闭期限。 */
                () => clearTimeout(deadline));
        };
        process.once('SIGINT',
            /** SIGINT 到达时触发已登记的关闭流程。 */
            () => { void stop(); });
        process.once('SIGTERM',
            /** SIGTERM 到达时触发已登记的关闭流程。 */
            () => { void stop(); });
    }).catch(
        /** 启动失败时设置非零退出码，仅输出不含凭据的原因。 */
        (error) => {
        const reason = /\bEADDRINUSE\b/.test(error.message) ? ' (EADDRINUSE)' : '';
        console.error(`grpc server failed to start${reason}`);
        process.exitCode = 1;
    });
}

module.exports = { createGetVarifyCodeHandler, createServer, startServer, main, getBindAddress };
