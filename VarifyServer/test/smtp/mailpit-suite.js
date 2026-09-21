'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { performance } = require('node:perf_hooks');
const { setTimeout: delay } = require('node:timers/promises');
const { createSmtpAdapter } = require('../../email');
const { faultCases } = require('./smtpFaults');

async function poll(probe) {
    const deadline = performance.now() + 5000;
    while (performance.now() < deadline) {
        if (await probe()) return;
        await delay(50);
    }
    throw new Error('SMTP correlation deadline');
}

async function runSmtpCases(coordinator, record) {
    const { config } = coordinator;
    assert.equal(config.host, '127.0.0.1');
    assert.match(config.runId, /^[a-f0-9]{32}$/);
    assert.equal(config.recipient, `${config.runId}@example.invalid`);
    assert.ok(coordinator.owned.has('mailpit'));
    const correlation = `${config.runId}-smtp06-${crypto.randomBytes(8).toString('hex')}`;
    const subjects = [correlation + '-initial', correlation + '-recovery', correlation + '-wrong-secure'];
    const search = async () => coordinator.mailApi(
        `/api/v1/search?query=${encodeURIComponent(`to:${config.recipient}`)}`);
    const baseline = new Set((await search()).messages.map((message) => message.ID));
    let primaryFailure;
    const send = async (subject, overrides = {}) => {
        // lifecycle(start) may replace ephemeral host ports. Read them at each send.
        const adapter = createSmtpAdapter({ host: config.host, port: config.ports.smtp, secure: false,
            auth: 'none', deadlineMs: 2000, ...overrides });
        try {
            return await adapter.SendMail({ from: 'adapter@example.invalid', to: config.recipient,
                subject, text: coordinator.mailBody });
        } finally { adapter.close(); }
    };
    try {
        await record('V09-SMTP-01', 'production adapter sends without authentication', async () => {
            assert.deepEqual(await send(subjects[0]), { status: 'Delivered' });
        });
        await record('V09-SMTP-02', 'Mailpit API correlates exactly one run message', async () => {
            await poll(async () => (await search()).messages.filter((mail) => mail.Subject === subjects[0]).length === 1);
        });
        await record('V09-SMTP-03', 'wrong TLS mode fails without delivery', async () => {
            const result = await send(subjects[2], { secure: true, deadlineMs: 500 });
            assert.ok(['Unavailable', 'DeadlineExceeded'].includes(result.status));
            assert.equal((await search()).messages.filter((mail) => mail.Subject === subjects[2]).length, 0);
        });
        await record('V09-SMTP-04', 'stopped Mailpit is unavailable within deadline', async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            try { assert.deepEqual(await send(subjects[1]), { status: 'Unavailable' }); }
            finally { await coordinator.lifecycle('mailpit', 'start'); }
            await poll(async () => {
                try { return await coordinator.mailApi('/readyz'); } catch { return false; }
            });
        });
        await record('V09-SMTP-05', 'production adapter recovers at refreshed SMTP port', async () => {
            assert.deepEqual(await send(subjects[1]), { status: 'Delivered' });
            await poll(async () => (await search()).messages.filter((mail) => mail.Subject === subjects[1]).length === 1);
        });
        for (const entry of faultCases) await record(entry.id, entry.name, entry.action);
    } catch (error) { primaryFailure = error; }
    finally {
        try {
            await record('V09-SMTP-12', 'SMTP run cleanup preserves prior fixture messages', async () => {
                const messages = (await search()).messages;
                const ids = messages.filter((mail) => subjects.includes(mail.Subject) && !baseline.has(mail.ID))
                    .map((mail) => mail.ID);
                if (ids.length) await coordinator.mailApi('/api/v1/messages', {
                    method: 'DELETE', headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ IDs: ids })
                });
                const remaining = (await search()).messages;
                assert.equal(remaining.filter((mail) => subjects.includes(mail.Subject)).length, 0);
                assert.ok([...baseline].every((id) => remaining.some((mail) => mail.ID === id)),
                    'SMTP cleanup must preserve the coordinator baseline');
            });
        } catch (cleanupError) {
            if (primaryFailure) throw new AggregateError([primaryFailure, cleanupError], 'SMTP proof and cleanup failed');
            throw cleanupError;
        }
    }
    if (primaryFailure) throw primaryFailure;
}

module.exports = { runSmtpCases };
