const grpc = require('@grpc/grpc-js');
const messageProto = require('./proto');
const constModule = require('./const');
const { v4: uuidv4 } = require('uuid');

function createGetVarifyCodeHandler({ redisModule, emailModule, generateUuid = uuidv4, logger = console }) {
    return async function GetVarifyCode(call, callback) {
        logger.log('email is ', call.request.email);

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
            logger.log('uniqueId is ', uniqueId);
            const text = '您的验证码为' + uniqueId + '请十分钟内完成注册';
            const mailOptions = {
                from: '1358451905@qq.com',
                to: call.request.email,
                subject: '验证码',
                text
            };

            const sendResult = await emailModule.SendMail(mailOptions);
            logger.log('send res is ', sendResult);

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
            logger.log('catch error is ', error);
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

function main() {
    const server = createServer();
    server.bindAsync('0.0.0.0:50051', grpc.ServerCredentials.createInsecure(), () => {
        server.start();
        console.log('grpc server started');
    });
    return server;
}

if (require.main === module) {
    main();
}

module.exports = { createGetVarifyCodeHandler, createServer, main };
