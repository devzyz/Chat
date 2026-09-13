'use strict';

const assert = require('node:assert/strict');
const { poll } = require('./dependencyCoordinator');

async function runFriendshipCases({ alice, bob, users, sql, record }) {
    const [first, second] = users;
    const fields = toUid => ({ toUid, description: 'Hello, 世界 👋', backname: 'fixture peer' });
    const query = statement => sql.execute(statement);
    const relation = (client, otherUid) => client.control.command('snapshot', { otherUid });
    await record('E03-JOURNEY-01', 'invalid self and unknown friend leave no application', async () => {
        for (const toUid of [first.uid, 2147483647]) {
            assert.notEqual((await alice.control.command('apply', fields(toUid))).error, 0);
        }
        assert.equal(await query('SELECT COUNT(*) FROM apply_friend'), '0');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '0');
    });
    await record('E03-JOURNEY-02', 'cross-instance application reaches the recipient production model', async () => {
        assert.equal((await alice.control.command('apply', fields(second.uid))).error, 0);
        await poll(async () => (await relation(bob, first.uid)).applied === true, 10000);
        assert.equal((await relation(bob, first.uid)).friend, false);
        assert.equal(await query(`SELECT COUNT(*) FROM apply_friend WHERE from_uid=${first.uid} AND to_uid=${second.uid} AND status=0`), '1');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '0');
    });
    await record('E03-JOURNEY-03', 'duplicate application preserves one pending durable relation', async () => {
        assert.equal((await alice.control.command('apply', fields(second.uid))).error, 0);
        assert.equal(await query('SELECT COUNT(*) FROM apply_friend'), '1');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '0');
        assert.equal((await relation(bob, first.uid)).applied, true);
    });
    let chatId;
    await record('E03-JOURNEY-04', 'recipient acceptance links both models and reciprocal durable friends', async () => {
        assert.equal((await bob.control.command('accept', fields(first.uid))).error, 0);
        let a, b;
        await poll(async () => {
            a = await relation(alice, second.uid); b = await relation(bob, first.uid);
            return a.friend === true && b.friend === true && a.chatId > 0 && a.chatId === b.chatId;
        }, 10000);
        chatId = a.chatId;
        assert.equal(await query(`SELECT COUNT(*) FROM friend WHERE (self_id=${first.uid} AND other_id=${second.uid}) OR (self_id=${second.uid} AND other_id=${first.uid})`), '2');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '2');
        assert.equal(await query('SELECT COUNT(*) FROM apply_friend WHERE status=1'), '1');
        assert.equal(await query('SELECT COUNT(*) FROM private_chat'), '1');
    });
    await record('E03-JOURNEY-05', 'repeated acceptance fails without additional friends chats or greetings', async () => {
        const messages = await query('SELECT COUNT(*) FROM chat_message');
        assert.notEqual((await bob.control.command('accept', fields(first.uid))).error, 0);
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '2');
        assert.equal(await query('SELECT COUNT(*) FROM private_chat'), '1');
        assert.equal(await query('SELECT COUNT(*) FROM chat_message'), messages);
        assert.equal((await relation(alice, second.uid)).chatId, chatId);
        assert.equal((await relation(bob, first.uid)).chatId, chatId);
    });
    return chatId;
}

module.exports = { runFriendshipCases };
