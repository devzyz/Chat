'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { performance } = require('node:perf_hooks');
const { setTimeout: delay } = require('node:timers/promises');
const { createSmtpAdapter } = require('../../email');
const { faultCases } = require('./smtpFaults');

/** 每五十毫秒探测邮件状态，五秒内未就绪则报告关联期限失败。 */ async function poll(probe) {
    const deadline = performance.now() + 5000;
    while (performance.now() < deadline) {
        if (await probe()) return;
        await delay(50);
    }
    throw new Error('SMTP correlation deadline');
}

/** 通过独立 Mailpit 验证发送、重启恢复与消息关联，并只清理本轮邮件。 */ async function runSmtpCases(coordinator, record) {
    const { config } = coordinator;
    assert.equal(config.host, '127.0.0.1');
    assert.match(config.runId, /^[a-f0-9]{32}$/);
    assert.equal(config.recipient, `${config.runId}@example.invalid`);
    assert.ok(coordinator.owned.has('mailpit'));
    const correlation = `${config.runId}-smtp06-${crypto.randomBytes(8).toString('hex')}`;
    const subjects = [correlation + '-initial', correlation + '-recovery', correlation + '-wrong-secure'];
    const search = /** 按本次运行的收件人查询 Mailpit 邮件。 */ async () => coordinator.mailApi(
        `/api/v1/search?query=${encodeURIComponent(`to:${config.recipient}`)}`);
    const baseline = new Set((await search()).messages.map(/** 提取基线邮件标识，供清理时保留。 */ (message) => message.ID));
    let primaryFailure;
    const send = /** 读取最新临时端口发送指定主题，完成后关闭适配器。 */ async (subject, overrides = {}) => {
        // lifecycle(start) may replace ephemeral host ports. Read them at each send.
        const adapter = createSmtpAdapter({ host: config.host, port: config.ports.smtp, secure: false,
            auth: 'none', deadlineMs: 2000, ...overrides });
        try {
            return await adapter.sendMail({ from: 'adapter@example.invalid', to: config.recipient,
                subject, text: coordinator.mailBody });
        } finally { adapter.close(); }
    };
    try {
        await record('V09-SMTP-01', 'production adapter sends without authentication', /** 验证测试邮件提交返回 Delivered。 */ async () => {
            assert.deepEqual(await send(subjects[0]), { status: 'Delivered' });
        });
        await record('V09-SMTP-02', 'Mailpit API correlates exactly one run message', /** 验证相应主题邮件恰好到达一次。 */ async () => {
            await poll(/** 探测首次发送的主题是否只出现一次。 */ async () => (await search()).messages.filter(/** 按首次发送主题筛选邮件。 */ (mail) => mail.Subject === subjects[0]).length === 1);
        });
        await record('V09-SMTP-03', 'wrong TLS mode fails without delivery', /** 验证对明文端点使用 TLS 失败且没有误投邮件。 */ async () => {
            const result = await send(subjects[2], { secure: true, deadlineMs: 500 });
            assert.ok(['Unavailable', 'DeadlineExceeded'].includes(result.status));
            assert.equal((await search()).messages.filter(/** 筛选 TLS 失败场景对应主题。 */ (mail) => mail.Subject === subjects[2]).length, 0);
        });
        await record('V09-SMTP-04', 'stopped Mailpit is unavailable within deadline', /** 停用 Mailpit 验证不可用错误，再恢复并等待服务就绪。 */ async () => {
            await coordinator.lifecycle('mailpit', 'stop');
            try { assert.deepEqual(await send(subjects[1]), { status: 'Unavailable' }); }
            finally { await coordinator.lifecycle('mailpit', 'start'); }
            await poll(/** 探测 Mailpit 就绪接口，暂时不可用时继续等待。 */ async () => {
                try { return await coordinator.mailApi('/readyz'); } catch { return false; }
            });
        });
        await record('V09-SMTP-05', 'production adapter recovers at refreshed SMTP port', /** 验证服务恢复后能重新发送且只投递一次。 */ async () => {
            assert.deepEqual(await send(subjects[1]), { status: 'Delivered' });
            await poll(/** 探测恢复后发送的邮件是否恰好一封。 */ async () => (await search()).messages.filter(/** 筛选恢复后发送的邮件主题。 */ (mail) => mail.Subject === subjects[1]).length === 1);
        });
        for (const entry of faultCases) await record(entry.id, entry.name, entry.action);
    } catch (error) { primaryFailure = error; }
    finally {
        try {
            await record('V09-SMTP-12', 'SMTP run cleanup preserves prior fixture messages', /** 仅删除本次新增测试邮件并确认基线邮件保留。 */ async () => {
                const messages = (await search()).messages;
                const ids = messages.filter(/** 筛选本次主题且不在基线中的新增邮件。 */ (mail) => subjects.includes(mail.Subject) && !baseline.has(mail.ID))
                    .map(/** 提取待删邮件标识。 */ (mail) => mail.ID);
                if (ids.length) await coordinator.mailApi('/api/v1/messages', {
                    method: 'DELETE', headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ IDs: ids })
                });
                const remaining = (await search()).messages;
                assert.equal(remaining.filter(/** 筛选本次测试主题以断言清理完成。 */ (mail) => subjects.includes(mail.Subject)).length, 0);
                assert.ok([...baseline].every(/** 逐项确认基线邮件仍存在。 */ (id) => remaining.some(/** 按基线标识查找保留邮件。 */ (mail) => mail.ID === id)),
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
