'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
const { createSmtpAdapter } = require('../../email');
const config = { host: '127.0.0.1', port: 1025, secure: false, auth: 'none', deadlineMs: 100 };
const message = { from: 'sender@example.invalid', to: 'run@example.invalid', text: 'synthetic-body' };

test('SMTP invalid settings fail before creating transport', /** 验证非法配置返回 InvalidConfig。 */ async () => {
    for (const change of [{ port: '1025junk' }, { secure: 'false' }, { deadlineMs: 0 }, { auth: 'unknown' },
        { host: null }, { port: null }, { secure: null }, { auth: null }, { deadlineMs: null }]) {
        const adapter = createSmtpAdapter({ ...config, ...change }, /** 配置应提前拒绝时若创建传输则令测试失败。 */ () => assert.fail('unexpected transport'));
        assert.deepEqual(await adapter.sendMail(message), { status: 'InvalidConfig' });
    }
});

test('SMTP no-auth options and delivery result omit provider details', /** 验证无认证配置以及禁用邮件库调试输出。 */ async () => {
    const adapter = createSmtpAdapter(config, /** 核对无认证及日志关闭配置，返回成功发送替身。 */ (options) => {
        assert.equal(options.auth, undefined);
        assert.equal(options.logger, false);
        assert.equal(options.debug, false);
        assert.equal(options.socketTimeout, 100);
        return { /** 模拟收件人被接受并携带不应公开的响应细节。 */ sendMail(mail, callback) { callback(null, { accepted: [mail.to], response: 'private detail' }); },
            /** 提供无外部资源的关闭接口。 */ close() {} };
    });
    assert.deepEqual(await adapter.sendMail(message), { status: 'Delivered' });
});

test('SMTP error classification never exposes server content', /** 验证 SMTP 异常分类为稳定状态。 */ async () => {
    for (const [error, status] of [[{ responseCode: 550 }, 'Rejected'], [{ code: 'ECONNRESET' }, 'Unavailable'],
        [{ code: 'ETIMEDOUT' }, 'DeadlineExceeded'], [{ code: 'EAUTH' }, 'Rejected']]) {
        const adapter = createSmtpAdapter(config, /** 创建返回敏感诊断的发送失败替身。 */ () => ({ /** 通过回调注入指定 SMTP 错误及测试敏感内容。 */ sendMail(mail, done) {
            done({ ...error, message: 'synthetic-secret', response: 'synthetic-body' });
        }, /** 提供无外部资源的关闭接口。 */ close() {} }));
        assert.deepEqual(await adapter.sendMail(message), { status });
    }
});

test('SMTP deadline closes resources and ignores late completion', { timeout: 2000 }, /** 验证总期限关闭连接且忽略迟到完成。 */ async () => {
    let late;
    let closes = 0;
    const adapter = createSmtpAdapter(config, /** 创建保留回调并记录关闭次数的超时替身。 */ () => ({ /** 保存发送完成回调以模拟期限后的迟到结果。 */ sendMail(mail, done) { late = done; },
        /** 累计传输关闭次数。 */ close() { closes += 1; } }));
    let completions = 0;
    const result = await adapter.sendMail(message).then(/** 累计发送完成次数并透传结果。 */ (value) => { completions += 1; return value; });
    late(null, { accepted: [message.to] });
    late({ responseCode: 550 });
    assert.deepEqual(result, { status: 'DeadlineExceeded' });
    assert.equal(completions, 1);
    assert.equal(closes, 1);
});

test('SMTP adapter close is idempotent and prevents later work', /** 验证主动关闭取消发送并阻止后续发送。 */ async () => {
    const adapter = createSmtpAdapter(config, /** 创建永不完成发送的替身以验证关闭取消。 */ () => ({ /** 刻意不调用完成回调以保持发送挂起。 */ sendMail() {}, /** 提供无外部资源的关闭接口。 */ close() {} }));
    const pending = adapter.sendMail(message);
    adapter.close();
    adapter.close();
    assert.deepEqual(await pending, { status: 'Unavailable' });
    assert.deepEqual(await adapter.sendMail(message), { status: 'Unavailable' });
});

test('SMTP authenticated transport requires credentials and keeps its configured options', /** 验证认证缺项不暴露凭据。 */ async () => {
    assert.deepEqual(await createSmtpAdapter({ ...config, auth: 'login' }).sendMail(message),
        { status: 'InvalidConfig' });
    const credentials = { user: 'sender@example.invalid', pass: 'synthetic-only' };
    const adapter = createSmtpAdapter({ ...config, auth: 'login', credentials, secure: true }, /** 核对认证和 TLS 连接选项后返回发送成功替身。 */ (options) => {
        assert.deepEqual(options.auth, credentials);
        assert.equal(options.secure, true);
        assert.equal(options.host, config.host);
        assert.equal(options.port, config.port);
        return { /** 模拟目标收件人被接受。 */ sendMail(mail, done) { done(null, { accepted: [mail.to] }); }, /** 提供无外部资源的关闭接口。 */ close() {} };
    });
    assert.deepEqual(await adapter.sendMail(message), { status: 'Delivered' });
});

test('SMTP success without accepted recipients is rejected', /** 验证无接受者的邮件返回拒收。 */ async () => {
    const adapter = createSmtpAdapter(config, /** 创建收件人全部拒绝的传输替身。 */ () => ({
        /** 返回空接受列表及被拒绝收件人。 */ sendMail(mail, done) { done(null, { accepted: [], rejected: [mail.to] }); }, /** 提供无外部资源的关闭接口。 */ close() {}
    }));
    assert.deepEqual(await adapter.sendMail(message), { status: 'Rejected' });
});

test('SMTP synchronous transport failure is bounded and sanitized', /** 验证 transport 构造异常被映射为不可用。 */ async () => {
    const adapter = createSmtpAdapter(config, /** 模拟传输工厂同步失败并携带不得泄露的测试诊断。 */ () => { throw new Error('synthetic-sensitive-detail'); });
    assert.deepEqual(await adapter.sendMail(message), { status: 'Unavailable' });
});
