'use strict';

const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');

async function runHistoryRecoveryCases({ bob, users, sql, record, chatId, restartClient }) {
    let expected;
    await record('E03-RECOVER-01', 'public sends persist at least three production history pages', async () => {
        const sent = new Map();
        // The production page size is ten; fail below if its boundary changes.
        for (let index = 0; index < 21; ++index) {
            const uuid = randomUUID();
            const text = `history-${index}: Unicode \u4e16\u754c`;
            sent.set(uuid, createHash('sha256').update(text).digest('hex'));
            await bob.control.command('send', { chatId, toUid: users[0].uid, uuid, text });
        }
        const rows = await sql.execute(`SELECT message_id,send_id,COALESCE(client_msg_uuid,''),SHA2(content,256) FROM chat_message WHERE chat_id=${chatId} ORDER BY message_id`);
        expected = rows.split('\n').map(line => {
            const [messageId, sender, uuid, sha256] = line.split('\t');
            return { messageId, sender: Number(sender), uuid, sha256 };
        });
        assert.ok(expected.length >= 21);
        for (const [uuid, hash] of sent) {
            const matches = expected.filter(row => row.uuid === uuid && row.sender === users[1].uid);
            assert.equal(matches.length, 1);
            assert.equal(matches[0].sha256, hash);
        }
        assert.ok(expected.every(row => /^[1-9][0-9]*$/.test(row.messageId) && /^[a-f0-9]{64}$/.test(row.sha256)));
    });
    let recovered;
    const snapshot = () => recovered.control.command('snapshot', { chatId });
    const identity = row => ({ messageId: row.messageId, sender: row.sender, uuid: row.uuid, sha256: row.sha256 });
    await record('E03-RECOVER-02', 'new client process authenticates through discovery with an empty message model', async () => {
        recovered = await restartClient();
        assert.equal((await snapshot()).messages.length, 0);
    });
    let finalState;
    await record('E03-RECOVER-03', 'forward history traversal recovers every durable ID UUID and hash in order', async () => {
        let cursor = '0';
        let pages = 0;
        do {
            assert.ok(pages < 16, 'history traversal exceeded its fixed bound');
            assert.equal((await recovered.control.command('history', { chatId, cursor })).error, 0);
            finalState = await snapshot();
            assert.ok(BigInt(finalState.cursor) > BigInt(cursor));
            if (pages === 0) assert.equal(finalState.messages.length, 10);
            assert.deepEqual(finalState.messages.map(identity), expected.slice(0, finalState.messages.length));
            cursor = finalState.cursor;
            ++pages;
        } while (finalState.more);
        assert.ok(pages >= 3);
        assert.deepEqual(finalState.messages.map(identity), expected);
        assert.equal(finalState.cursor, expected.at(-1).messageId);
    });
    await record('E03-RECOVER-04', 'overlapping replay and exhausted history preserve identity order and terminal cursor', async () => {
        for (const cursor of [expected[8].messageId, finalState.cursor]) {
            assert.equal((await recovered.control.command('history', { chatId, cursor })).error, 0);
            const state = await snapshot();
            assert.deepEqual(state.messages.map(identity), expected);
            assert.equal(state.cursor, finalState.cursor);
            assert.equal(state.more, false);
        }
    });
}

module.exports = { runHistoryRecoveryCases };
