const grpc = require('@grpc/grpc-js');
const net = require('node:net');
const messageProto = require('./proto');
const constModule = require('./const');
const { v4: uuidv4 } = require('uuid');

function createGetVarifyCodeHandler({ redisModule, emailModule, senderEmail, generateUuid = uuidv4, logger = console }) {
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
                from: senderEmail,
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
    const { email_user } = require('./config');
    const emailModule = require('./email');
    const redisModule = require('./redis');
    return createGetVarifyCodeHandler({ redisModule, emailModule, senderEmail: email_user });
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

function getBindAddress(environment = process.env) {
    const address = environment.CHAT_VARIFY_BIND_ADDRESS ?? '0.0.0.0:50051';
    const match = /^(\d+\.\d+\.\d+\.\d+):(\d+)$/.exec(address);
    if (!match || !net.isIPv4(match[1]) || Number(match[2]) < 1 || Number(match[2]) > 65535) {
        throw new Error('Invalid CHAT_VARIFY_BIND_ADDRESS; expected IPv4:port (1..65535)');
    }
    return address;
}

async function main({ server, logger = console } = {}) {
    const address = getBindAddress();
    server ??= createServer();
    return startServer({
        server,
        address,
        credentials: grpc.ServerCredentials.createInsecure(),
        logger
    }).then(() => server);
}

if (require.main === module) {
    main().catch((error) => {
        const reason = /\bEADDRINUSE\b/.test(error.message) ? ' (EADDRINUSE)' : '';
        console.error(`grpc server failed to start${reason}`);
        process.exitCode = 1;
    });
}

module.exports = { createGetVarifyCodeHandler, createServer, startServer, main, getBindAddress };
