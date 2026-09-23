'use strict';

const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');

/** 验证真实公开发送产生的多页历史可在客户端重启后完整恢复并去重。 */
async function runHistoryRecoveryCases({ bob, users, sql, record, chatId, restartClient }) {
    let expected;
    await record('E03-RECOVER-01', 'public sends persist at least three production history pages', /** 发送至少三页消息，并以数据库内容与摘要建立恢复期望。 */ async () => {
        const sent = new Map();
        // The production row cap is ten; encoded byte limits can shorten a page.
        for (let index = 0; index < 21; ++index) {
            const uuid = randomUUID();
            const text = `history-${index}: Unicode \u4e16\u754c`;
            sent.set(uuid, createHash('sha256').update(text).digest('hex'));
            await bob.control.command('send', { chatId, toUid: users[0].uid, uuid, text });
        }
        const rows = await sql.execute(`SELECT message_id,send_id,COALESCE(client_msg_uuid,''),SHA2(content,256) FROM chat_message WHERE chat_id=${chatId} ORDER BY message_id`);
        expected = rows.split('\n').map(/** 将数据库行解析为可比较的消息身份和内容摘要。 */ line => {
            const [messageId, sender, uuid, sha256] = line.split('\t');
            return { messageId, sender: Number(sender), uuid, sha256 };
        });
        assert.ok(expected.length >= 21);
        for (const [uuid, hash] of sent) {
            const matches = expected.filter(/** 定位对应 UUID 与发送方的持久化消息。 */ row => row.uuid === uuid && row.sender === users[1].uid);
            assert.equal(matches.length, 1);
            assert.equal(matches[0].sha256, hash);
        }
        assert.ok(expected.every(/** 校验期望消息具有有效持久化标识和内容摘要。 */ row => /^[1-9][0-9]*$/.test(row.messageId) && /^[a-f0-9]{64}$/.test(row.sha256)));
    });
    let recovered;
    const snapshot = /** 读取重启客户端当前会话的消息快照。 */ () => recovered.control.command('snapshot', { chatId });
    const identity = /** 提取可跨客户端与数据库对比的消息字段。 */ row => ({ messageId: row.messageId, sender: row.sender, uuid: row.uuid, sha256: row.sha256 });
    await record('E03-RECOVER-02', 'new client process authenticates through discovery with an empty message model', /** 重启客户端并确认历史加载前内存消息为空。 */ async () => {
        recovered = await restartClient();
        assert.equal((await snapshot()).messages.length, 0);
    });
    let finalState;
    await record('E03-RECOVER-03', 'forward history traversal recovers every durable ID UUID and hash in order', /** 按递增游标有界遍历历史页，核对每页大小、顺序和完整内容。 */ async () => {
        let cursor = '0';
        let pages = 0;
        do {
            assert.ok(pages < 16, 'history traversal exceeded its fixed bound');
            assert.equal((await recovered.control.command('history', { chatId, cursor })).error, 0);
            finalState = await snapshot();
            assert.ok(BigInt(finalState.cursor) > BigInt(cursor));
            const previousCount = pages === 0 ? 0 : expected.findIndex(/** 定位上一页游标在数据库期望序列中的位置。 */ row => row.messageId === cursor) + 1;
            const pageCount = finalState.messages.length - previousCount;
            assert.ok(pageCount > 0 && pageCount <= 10);
            assert.deepEqual(finalState.messages.map(identity), expected.slice(0, finalState.messages.length));
            cursor = finalState.cursor;
            ++pages;
        } while (finalState.more);
        assert.ok(pages >= 3);
        assert.deepEqual(finalState.messages.map(identity), expected);
        assert.equal(finalState.cursor, expected.at(-1).messageId);
    });
    await record('E03-RECOVER-04', 'overlapping replay and exhausted history preserve identity order and terminal cursor', /** 重复及重叠拉取历史后核对不重复、不回退且终止标志稳定。 */ async () => {
        for (const cursor of [expected[8].messageId, finalState.cursor]) {
            assert.equal((await recovered.control.command('history', { chatId, cursor })).error, 0);
            const state = await snapshot();
            assert.deepEqual(state.messages.map(identity), expected);
            assert.equal(state.cursor, finalState.cursor);
            assert.equal(state.more, false);
        }
    });
    return recovered;
}

module.exports = { runHistoryRecoveryCases };
