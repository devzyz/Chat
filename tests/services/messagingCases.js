'use strict';

const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');
const { poll } = require('./dependencyCoordinator');

async function runMessagingCases({ alice, bob, users, sql, record, chatId, outsider }) {
    const clients = [alice, bob];
    const send = (index, uuid, text, extra = {}) => clients[index].control.command('send', {
        chatId, toUid: users[1 - index].uid, uuid, text, ...extra });
    const rows = client => client.control.command('snapshot', { chatId }).then(result => result.messages);
    const durable = async (sender, uuid) => {
        assert.match(uuid, /^[a-f0-9-]{36}$/);
        return sql.execute(`SELECT message_id FROM chat_message WHERE send_id=${sender} AND client_msg_uuid='${uuid}'`);
    };
    const observe = async (sender, uuid, text) => {
        let copies;
        await poll(async () => {
            copies = await Promise.all(clients.map(async client =>
                (await rows(client)).filter(row => row.sender === sender && row.uuid === uuid)));
            return copies.every(items => items.length === 1 && BigInt(items[0].messageId) > 0);
        }, 10000);
        const id = copies[0][0].messageId;
        const hash = createHash('sha256').update(text).digest('hex');
        for (const items of copies) {
            assert.equal(items[0].messageId, id);
            assert.equal(items[0].sha256, hash);
        }
        assert.equal(await durable(sender, uuid), id);
        return id;
    };
    const forward = randomUUID();
    const backward = randomUUID();
    await record('E03-XMSG-01', 'bidirectional delivery correlates sender peer models and durable server IDs', async () => {
        for (const [index, uuid] of [forward, backward].entries()) {
            const text = `direction-${index}: 世界 👋`;
            assert.equal((await send(index, uuid, text)).error, 0);
            await observe(users[index].uid, uuid, text);
        }
    });
    await record('E03-XMSG-02', 'different authenticated senders may share one UUID without losing either model row', async () => {
        const uuid = randomUUID();
        const results = await Promise.all([send(0, uuid, 'from alice'), send(1, uuid, 'from bob')]);
        assert.ok(results.every(result => result.error === 0));
        const first = await observe(users[0].uid, uuid, 'from alice');
        const second = await observe(users[1].uid, uuid, 'from bob');
        assert.notEqual(first, second);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '2');
    });
    await record('E03-XMSG-03', 'same sender UUID retries preserve one durable row and one model item', async () => {
        const before = await durable(users[0].uid, forward);
        for (let attempt = 0; attempt < 2; ++attempt) {
            assert.equal((await send(0, forward, 'direction-0: 世界 👋', attempt === 0 ? { copies: 2 } : {})).error, 0);
            assert.equal(await observe(users[0].uid, forward, 'direction-0: 世界 👋'), before);
        }
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE send_id=${users[0].uid} AND client_msg_uuid='${forward}'`), '1');
    });
    await record('E03-XMSG-04', 'conflicting payload leaves the original durable identity and content unchanged', async () => {
        const before = await durable(users[0].uid, forward);
        const result = await send(0, forward, 'conflicting replacement');
        assert.ok(Number.isInteger(result.error) && result.error > 0);
        assert.equal(await observe(users[0].uid, forward, 'direction-0: 世界 👋'), before);
        assert.equal(await sql.execute(`SELECT content FROM chat_message WHERE send_id=${users[0].uid} AND client_msg_uuid='${forward}'`), 'direction-0: 世界 👋');
    });
    await record('E03-XMSG-05', 'invalid chat membership fails without persistence or recipient delivery', async () => {
        const uuid = randomUUID();
        const before = (await rows(bob)).length;
        const result = await send(0, uuid, 'must not commit', { chatId: 2147483647 });
        assert.ok(Number.isInteger(result.error) && result.error > 0);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '0');
        assert.equal((await rows(bob)).length, before);
    });
    await record('E03-XMSG-07', 'nonmember cannot send or read a real private chat and retries cannot change its scope', async () => {
        const created = await alice.control.command('create', { toUid: outsider.uid });
        assert.equal(created.error, 0);
        const privateChat = created.chatId;
        assert.ok(privateChat > 0 && privateChat !== chatId);
        const privateUuid = randomUUID();
        await send(0, privateUuid, 'private to outsider', { chatId: privateChat, toUid: outsider.uid });
        const privateId = await durable(users[0].uid, privateUuid);
        assert.match(privateId, /^[1-9][0-9]*$/);
        const forgedUuid = randomUUID();
        const deniedSend = await send(1, forgedUuid, 'not a member', { chatId: privateChat, toUid: users[0].uid });
        assert.ok(Number.isInteger(deniedSend.error) && deniedSend.error > 0);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${forgedUuid}'`), '0');
        const deniedHistory = await bob.control.command('history', { chatId: privateChat, cursor: '0' });
        assert.ok(Number.isInteger(deniedHistory.error) && deniedHistory.error > 0);
        assert.ok(!(await bob.control.command('snapshot', { chatId: privateChat })).messages.some(row => row.uuid === privateUuid));
        assert.equal((await alice.control.command('history', { chatId: privateChat, cursor: '0' })).error, 0);
        const restored = (await alice.control.command('snapshot', { chatId: privateChat })).messages;
        assert.equal(restored.find(row => row.uuid === privateUuid)?.messageId, privateId);
        const original = await durable(users[0].uid, forward);
        const conflict = await send(0, forward, 'direction-0: 世界 👋', { chatId: privateChat, toUid: outsider.uid });
        assert.ok(Number.isInteger(conflict.error) && conflict.error > 0);
        assert.equal(await durable(users[0].uid, forward), original);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${forward}'`), '1');
    });
    for (const client of clients) assert.equal((await rows(client)).length, 5);
}

async function runOfflineMessageCase({ bob, users, sql, record, chatId }) {
    await record('E03-XMSG-06', 'offline peer keeps a committed message recoverable by authorized history and UUID retry', async () => {
        const uuid = randomUUID();
        const request = { chatId, toUid: users[0].uid, uuid, text: 'offline durable message' };
        await bob.control.command('send', request);
        const id = await sql.execute(`SELECT message_id FROM chat_message WHERE send_id=${users[1].uid} AND client_msg_uuid='${uuid}'`);
        assert.match(id, /^[1-9][0-9]*$/);
        await bob.control.command('send', request);
        assert.equal(await sql.execute(`SELECT message_id FROM chat_message WHERE send_id=${users[1].uid} AND client_msg_uuid='${uuid}'`), id);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
        assert.equal((await bob.control.command('history', { chatId, cursor: '0' })).error, 0);
        const matches = (await bob.control.command('snapshot', { chatId })).messages.filter(row => row.uuid === uuid);
        assert.equal(matches.length, 1);
        assert.equal(matches[0].messageId, id);
    });
}

module.exports = { runMessagingCases, runOfflineMessageCase };
