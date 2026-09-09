'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
const { createSmtpAdapter } = require('../../email');
const config = { host: '127.0.0.1', port: 1025, secure: false, auth: 'none', deadlineMs: 100 };
const message = { from: 'sender@example.invalid', to: 'run@example.invalid', text: 'synthetic-body' };

test('SMTP invalid settings fail before creating transport', async () => {
    for (const change of [{ port: '1025junk' }, { secure: 'false' }, { deadlineMs: 0 }, { auth: 'unknown' },
        { host: null }, { port: null }, { secure: null }, { auth: null }, { deadlineMs: null }]) {
        const adapter = createSmtpAdapter({ ...config, ...change }, () => assert.fail('unexpected transport'));
        assert.deepEqual(await adapter.SendMail(message), { status: 'InvalidConfig' });
    }
});

test('SMTP no-auth options and delivery result omit provider details', async () => {
    const adapter = createSmtpAdapter(config, (options) => {
        assert.equal(options.auth, undefined);
        assert.equal(options.logger, false);
        assert.equal(options.debug, false);
        assert.equal(options.socketTimeout, 100);
        return { sendMail(mail, callback) { callback(null, { accepted: [mail.to], response: 'private detail' }); },
            close() {} };
    });
    assert.deepEqual(await adapter.SendMail(message), { status: 'Delivered' });
});

test('SMTP error classification never exposes server content', async () => {
    for (const [error, status] of [[{ responseCode: 550 }, 'Rejected'], [{ code: 'ECONNRESET' }, 'Unavailable'],
        [{ code: 'ETIMEDOUT' }, 'DeadlineExceeded'], [{ code: 'EAUTH' }, 'Rejected']]) {
        const adapter = createSmtpAdapter(config, () => ({ sendMail(mail, done) {
            done({ ...error, message: 'synthetic-secret', response: 'synthetic-body' });
        }, close() {} }));
        assert.deepEqual(await adapter.SendMail(message), { status });
    }
});

test('SMTP deadline closes resources and ignores late completion', { timeout: 2000 }, async () => {
    let late;
    let closes = 0;
    const adapter = createSmtpAdapter(config, () => ({ sendMail(mail, done) { late = done; },
        close() { closes += 1; } }));
    let completions = 0;
    const result = await adapter.SendMail(message).then((value) => { completions += 1; return value; });
    late(null, { accepted: [message.to] });
    late({ responseCode: 550 });
    assert.deepEqual(result, { status: 'DeadlineExceeded' });
    assert.equal(completions, 1);
    assert.equal(closes, 1);
});

test('SMTP adapter close is idempotent and prevents later work', async () => {
    const adapter = createSmtpAdapter(config, () => ({ sendMail() {}, close() {} }));
    const pending = adapter.SendMail(message);
    adapter.close();
    adapter.close();
    assert.deepEqual(await pending, { status: 'Unavailable' });
    assert.deepEqual(await adapter.SendMail(message), { status: 'Unavailable' });
});

test('SMTP authenticated transport requires credentials and keeps its configured options', async () => {
    assert.deepEqual(await createSmtpAdapter({ ...config, auth: 'login' }).SendMail(message),
        { status: 'InvalidConfig' });
    const credentials = { user: 'sender@example.invalid', pass: 'synthetic-only' };
    const adapter = createSmtpAdapter({ ...config, auth: 'login', credentials, secure: true }, (options) => {
        assert.deepEqual(options.auth, credentials);
        assert.equal(options.secure, true);
        assert.equal(options.host, config.host);
        assert.equal(options.port, config.port);
        return { sendMail(mail, done) { done(null, { accepted: [mail.to] }); }, close() {} };
    });
    assert.deepEqual(await adapter.SendMail(message), { status: 'Delivered' });
});

test('SMTP success without accepted recipients is rejected', async () => {
    const adapter = createSmtpAdapter(config, () => ({
        sendMail(mail, done) { done(null, { accepted: [], rejected: [mail.to] }); }, close() {}
    }));
    assert.deepEqual(await adapter.SendMail(message), { status: 'Rejected' });
});

test('SMTP synchronous transport failure is bounded and sanitized', async () => {
    const adapter = createSmtpAdapter(config, () => { throw new Error('synthetic-sensitive-detail'); });
    assert.deepEqual(await adapter.SendMail(message), { status: 'Unavailable' });
});
