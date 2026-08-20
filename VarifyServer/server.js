const grpc = require('@grpc/grpc-js');
const messageProto = require('./proto');
const constModule = require('./const');
const { v4: uuidv4 } = require('uuid');

function createGetVarifyCodeHandler({ redisModule, emailModule, generateUuid = uuidv4, logger = console }) {
    return async function GetVarifyCode(call, callback) {
        logger.log('verification request received');

        try {
            const key = constModule.code_prefix + call.request.email;
            const queryResult = await redisModule.GetRedis(key);
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
                from: '1358451905@qq.com',
                to: call.request.email,
                subject: '验证码',
                text
            };

            const sendResult = await emailModule.SendMail(mailOptions);
            if (!sendResult) {
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

function createDefaultHandler() {
    const emailModule = require('./email');
    const redisModule = require('./redis');
    return createGetVarifyCodeHandler({ redisModule, emailModule });
}

function createServer(handler = createDefaultHandler()) {
    const server = new grpc.Server();
    server.addService(messageProto.VarifyService.service, { GetVarifyCode: handler });
    return server;
}

function startServer({ server, address, credentials, logger = console }) {
    return new Promise((resolve, reject) => {
        server.bindAsync(address, credentials, (error, boundPort) => {
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

function main({ server = createServer(), logger = console } = {}) {
    void startServer({
        server,
        address: '0.0.0.0:50051',
        credentials: grpc.ServerCredentials.createInsecure(),
        logger
    }).catch(() => {
        const logError = typeof logger.error === 'function' ? logger.error.bind(logger) : logger.log.bind(logger);
        logError('grpc server failed to start');
    });
    return server;
}

if (require.main === module) {
    main();
}

module.exports = { createGetVarifyCodeHandler, createServer, startServer, main };
