const assert = require('node:assert/strict');
const { test } = require('node:test');

const constModule = require('../../const');
const { createGetVarifyCodeHandler } = require('../../server');

const recipient = 'recipient@example.test';
const silentLogger = { log() {} };

async function invoke(handler) {
    const callbackCalls = [];
    await handler({ request: { email: recipient } }, (error, response) => {
        callbackCalls.push({ error, response });
    });
    assert.equal(callbackCalls.length, 1, 'gRPC callback must complete exactly once');
    assert.equal(callbackCalls[0].error, null);
    assert.equal(callbackCalls[0].response.email, recipient);
    return callbackCalls[0].response;
}

// V06-MOD-01
test('requiring the handler module does not load Redis or SMTP adapters', () => {
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
test('cached code is reused without UUID generation or Redis write', async () => {
    // Arrange
    const sentMessages = [];
    const redisModule = {
        async GetRedis(key) {
            assert.equal(key, constModule.code_prefix + recipient);
            return 'A1B2';
        },
        async setRedisExpire() {
            assert.fail('cached code must not be written again');
        }
    };
    const emailModule = {
        async SendMail(options) {
            sentMessages.push(options);
            return 'accepted';
        }
    };
    const handler = createGetVarifyCodeHandler({
        redisModule,
        emailModule,
        generateUuid() {
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

// V06-HDL-02
test('missing code generates four characters and stores a 600 second TTL', async () => {
    // Arrange
    const writes = [];
    const sentMessages = [];
    const redisModule = {
        async GetRedis() {
            return null;
        },
        async setRedisExpire(key, value, ttlSeconds) {
            writes.push({ key, value, ttlSeconds });
            return true;
        }
    };
    const emailModule = {
        async SendMail(options) {
            sentMessages.push(options);
            return 'accepted';
        }
    };
    const handler = createGetVarifyCodeHandler({
        redisModule,
        emailModule,
        generateUuid: () => 'WXYZ-extra',
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
test('failed Redis write returns RedisErr without sending mail', async () => {
    // Arrange
    let mailCalls = 0;
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            async GetRedis() {
                return null;
            },
            async setRedisExpire(key, value, ttlSeconds) {
                assert.equal(key, constModule.code_prefix + recipient);
                assert.equal(value, 'R3D1');
                assert.equal(ttlSeconds, 600);
                return false;
            }
        },
        emailModule: {
            async SendMail() {
                mailCalls += 1;
                return 'accepted';
            }
        },
        generateUuid: () => 'R3D1-extra',
        logger: silentLogger
    });

    // Act
    const response = await invoke(handler);

    // Assert
    assert.equal(response.error, constModule.Errors.RedisErr);
    assert.equal(mailCalls, 0);
});

// V06-HDL-04
test('false mail result returns Exception', async () => {
    // Arrange
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            async GetRedis() {
                return 'M4IL';
            }
        },
        emailModule: {
            async SendMail() {
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

// V06-HDL-05
test('Redis read rejection returns Exception without sending mail', async () => {
    // Arrange
    let mailCalls = 0;
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            async GetRedis() {
                throw new Error('injected Redis read failure');
            }
        },
        emailModule: {
            async SendMail() {
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
test('mail rejection returns Exception', async () => {
    // Arrange
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            async GetRedis() {
                return 'F4IL';
            }
        },
        emailModule: {
            async SendMail() {
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
test('default handler events never disclose the recipient or verification code', async () => {
    const entries = [];
    const logger = {
        log(...values) {
            entries.push(values.map(String).join(' '));
        }
    };
    const handler = createGetVarifyCodeHandler({
        redisModule: {
            async GetRedis() {
                return 'S3CR';
            }
        },
        emailModule: {
            async SendMail() {
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
