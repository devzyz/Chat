const assert = require('node:assert/strict');
const { test } = require('node:test');

const constModule = require('../../const');
const { createGetVarifyCodeHandler } = require('../../server');

const recipient = 'recipient@example.test';
const silentLogger = { /** 丢弃测试日志，避免适配器输出干扰断言。 */ log() {} };

/** 调用验证码处理器并要求 gRPC 回调恰好完成一次，返回业务响应。 */ async function invoke(handler) {
    const callbackCalls = [];
    await handler({ request: { email: recipient } }, /** 收集处理器的回调错误及响应供次数断言。 */ (error, response) => {
        callbackCalls.push({ error, response });
    });
    assert.equal(callbackCalls.length, 1, 'gRPC callback must complete exactly once');
    assert.equal(callbackCalls[0].error, null);
    assert.equal(callbackCalls[0].response.email, recipient);
    return callbackCalls[0].response;
}

// V06-MOD-01
test('requiring the handler module does not load Redis or SMTP adapters', /** 验证导入服务模块不会提前加载 Redis 和邮件适配器。 */ () => {
    // Arrange
    const redisPath = require.resolve('../../redis');
    const emailPath = require.resolve('../../email');

    // Act
    const serverModule = require('../../server');

    // Assert
    assert.equal(typeof serverModule.createGetVarifyCodeHandler, 'function');
    assert.equal(typeof serverModule.createServer, 'function');
    assert.equal(typeof serverModule.main, 'function');
    assert.equal(require.cache[redisPath], undefined);
    assert.equal(require.cache[emailPath], undefined);
});

// V06-HDL-01
test('cached code is reused without UUID generation or Redis write', /** 验证已有验证码复用且不重新写入或生成 UUID。 */ async () => {
    // Arrange
    const sentMessages = [];
    const redisModule = {
        /** 核对缓存键并返回已有验证码。 */ async getRedis(key) {
            assert.equal(key, constModule.code_prefix + recipient);
            return 'A1B2';
        },
        /** 缓存命中时若再次写入验证码则使测试失败。 */ async setRedisExpire() {
            assert.fail('cached code must not be written again');
        }
    };
    const emailModule = {
        /** 捕获待发邮件并模拟接受。 */ async sendMail(options) {
            sentMessages.push(options);
            return 'accepted';
        }
    };
    const handler = createGetVarifyCodeHandler({
        redisModule,
        emailModule,
        /** 缓存命中时若生成新 UUID 则使测试失败。 */ generateUuid() {
            assert.fail('cached code must not generate a UUID');
        },
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.Success);
    assert.equal(sentMessages.length, 1);
    assert.equal(sentMessages[0].to, recipient);
    assert.match(sentMessages[0].text, /A1B2/);
});

// V06-HDL-07
test('mail uses the sender supplied by runtime configuration', /** 验证邮件使用注入的发件地址。 */ async () => {
    const senderEmail = 'sender@example.test';
    let sentMessage;
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            /** 返回已有验证码供发件地址断言。 */ async getRedis() {
                return 'A1B2';
            }
        },
        emailModule: {
            /** 捕获邮件选项供发件地址断言。 */ async sendMail(options) {
                sentMessage = options;
                return 'accepted';
            }
        },
        senderEmail,
        logger: silentLogger
    });

    const response = await invoke(handler);

    assert.equal(response.error, constModule.Errors.Success);
    assert.ok(sentMessage.from === senderEmail, 'mail must use the injected sender address');
});

// V06-HDL-02
test('missing code generates four characters and stores a 600 second TTL', /** 验证缓存未命中时生成并写入新验证码。 */ async () => {
    // Arrange
    const writes = [];
    const sentMessages = [];
    const redisModule = {
        /** 模拟缓存未命中。 */ async getRedis() {
            return null;
        },
        /** 记录验证码缓存写入的键、值及期限并模拟成功。 */ async setRedisExpire(key, value, ttlSeconds) {
            writes.push({ key, value, ttlSeconds });
            return true;
        }
    };
    const emailModule = {
        /** 捕获包含新验证码的邮件。 */ async sendMail(options) {
            sentMessages.push(options);
            return 'accepted';
        }
    };
    const handler = createGetVarifyCodeHandler({
        redisModule,
        emailModule,
        generateUuid: /** 提供固定 UUID 前缀以断言新验证码生成。 */ () => 'WXYZ-extra',
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.Success);
    assert.deepEqual(writes, [{
        key: constModule.code_prefix + recipient,
        value: 'WXYZ',
        ttlSeconds: 600
    }]);
    assert.equal(sentMessages.length, 1);
    assert.match(sentMessages[0].text, /WXYZ/);
});

// V06-HDL-03
test('failed Redis write returns RedisErr without sending mail', /** 验证缓存写失败阻止发送邮件。 */ async () => {
    // Arrange
    let mailCalls = 0;
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            /** 模拟缓存未命中以触发写入。 */ async getRedis() {
                return null;
            },
            /** 核对失败写入的键、验证码及期限并模拟 Redis 拒绝。 */ async setRedisExpire(key, value, ttlSeconds) {
                assert.equal(key, constModule.code_prefix + recipient);
                assert.equal(value, 'R3D1');
                assert.equal(ttlSeconds, 600);
                return false;
            }
        },
        emailModule: {
            /** 计数意外邮件调用以验证失败分支。 */ async sendMail() {
                mailCalls += 1;
                return 'accepted';
            }
        },
        generateUuid: /** 提供固定 UUID 前缀以复现缓存写失败。 */ () => 'R3D1-extra',
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.RedisErr);
    assert.equal(mailCalls, 0);
});

// V06-HDL-04
test('false mail result returns Exception', /** 验证 SMTP 拒收映射为业务错误。 */ async () => {
    // Arrange
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            /** 提供已缓存验证码以隔离邮件失败。 */ async getRedis() {
                return 'M4IL';
            }
        },
        emailModule: {
            /** 模拟邮件发送失败。 */ async sendMail() {
                return false;
            }
        },
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.Exception);
});

test('structured SMTP statuses retain the public error mapping and single callback', /** 逐项验证 SMTP 结构化状态与业务码映射。 */ async () => {
    for (const status of ['Delivered', 'Rejected', 'Unavailable', 'DeadlineExceeded', 'InvalidConfig']) {
        const handler = createGetVarifyCodeHandler({
            redisModule: { /** 提供邮件状态测试所需验证码。 */ async getRedis() { return 'M4IL'; } },
            emailModule: { /** 返回本轮注入的 SMTP 状态。 */ async sendMail() { return { status }; } },
            logger: silentLogger
        });
        const response = await invoke(handler);
        assert.equal(response.error, status === 'Delivered' ? constModule.Errors.Success : constModule.Errors.Exception);
    }
});

// V06-HDL-05
test('Redis read rejection returns Exception without sending mail', /** 验证 Redis 抛异常时处理器完成一次且不发邮件。 */ async () => {
    // Arrange
    let mailCalls = 0;
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            /** 注入 Redis 读取异常。 */ async getRedis() {
                throw new Error('injected Redis read failure');
            }
        },
        emailModule: {
            /** 统计异常路径是否误发邮件。 */ async sendMail() {
                mailCalls += 1;
                return 'accepted';
            }
        },
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.Exception);
    assert.equal(mailCalls, 0);
});

// V06-HDL-06
test('mail rejection returns Exception', /** 验证 SMTP 抛异常仍完成业务回调。 */ async () => {
    // Arrange
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            /** 提供验证码以到达 SMTP 异常分支。 */ async getRedis() {
                return 'F4IL';
            }
        },
        emailModule: {
            /** 注入 SMTP 异常。 */ async sendMail() {
                throw new Error('injected SMTP failure');
            }
        },
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.Exception);
});

// V06-LOG-01
test('default handler events never disclose the recipient or verification code', /** 验证日志不泄漏验证码或服务商敏感细节。 */ async () => {
    const entries = [];
    const logger = {
        /** 收集日志文本以检查敏感数据是否泄露。 */ log(...values) {
            entries.push(values.map(String).join(' '));
        }
    };
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            /** 提供敏感验证码以检测日志泄漏。 */ async getRedis() {
                return 'S3CR';
            }
        },
        emailModule: {
            /** 返回带服务商细节的响应以检测日志泄漏。 */ async sendMail() {
                return 'accepted-with-provider-detail';
            }
        },
        logger
    });

    const response = await invoke(handler);

    assert.equal(response.error, constModule.Errors.Success);
    const output = entries.join('\n');
    assert.doesNotMatch(output, /recipient@example\.test/);
    assert.doesNotMatch(output, /S3CR/);
    assert.doesNotMatch(output, /accepted-with-provider-detail/);
});
