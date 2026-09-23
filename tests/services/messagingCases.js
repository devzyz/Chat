'use strict';

const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');
const { poll } = require('./dependencyCoordinator');

/** 验证双实例消息收敛、幂等、冲突与成员权限，对照客户端快照和数据库。 */
async function runMessagingCases({ alice, bob, users, sql, record, chatId, outsider }) {
    const clients = [alice, bob];
    const send = /** 从指定客户端向另一用户发送消息，允许覆盖故障测试字段。 */ (index, uuid, text, extra = {}) => clients[index].control.command('send', {
        chatId, toUid: users[1 - index].uid, uuid, text, ...extra });
    const rows = /** 读取指定客户端会话快照中的消息列表。 */ client => client.control.command('snapshot', { chatId }).then(/** 从快照结果提取消息集合。 */ result => result.messages);
    const durable = /** 校验 UUID 格式后按发送方查询持久化消息标识。 */ async (sender, uuid) => {
        assert.match(uuid, /^[a-f0-9-]{36}$/);
        return sql.execute(`SELECT message_id FROM chat_message WHERE send_id=${sender} AND client_msg_uuid='${uuid}'`);
    };
    const observe = /** 等待双方消息唯一收敛，并核对内容摘要及数据库身份。 */ async (sender, uuid, text) => {
        let copies;
        await poll(/** 并行检查双方目标消息是否均已有唯一持久化记录。 */ async () => {
            copies = await Promise.all(clients.map(/** 从单个客户端快照筛选指定发送方与 UUID。 */ async client =>
                (await rows(client)).filter(/** 匹配发送方和客户端 UUID 组成的消息身份。 */ row => row.sender === sender && row.uuid === uuid)));
            return copies.every(/** 确认单条目标消息已获得正的持久化标识。 */ items => items.length === 1 && BigInt(items[0].messageId) > 0);
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
    await record('E03-XMSG-01', 'bidirectional delivery correlates sender peer models and durable server IDs', /** 验证两个方向的 Unicode 消息均提交并在双方收敛。 */ async () => {
        for (const [index, uuid] of [forward, backward].entries()) {
            const text = `direction-${index}: 世界 👋`;
            assert.equal((await send(index, uuid, text)).error, 0);
            await observe(users[index].uid, uuid, text);
        }
    });
    await record('E03-XMSG-02', 'different authenticated senders may share one UUID without losing either model row', /** 验证不同发送方使用同一 UUID 时产生各自独立的消息。 */ async () => {
        const uuid = randomUUID();
        const results = await Promise.all([send(0, uuid, 'from alice'), send(1, uuid, 'from bob')]);
        assert.ok(results.every(/** 确认并发发送结果均为成功。 */ result => result.error === 0));
        const first = await observe(users[0].uid, uuid, 'from alice');
        const second = await observe(users[1].uid, uuid, 'from bob');
        assert.notEqual(first, second);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '2');
    });
    await record('E03-XMSG-03', 'same sender UUID retries preserve one durable row and one model item', /** 验证同一消息的重复发送与重试复用原记录。 */ async () => {
        const before = await durable(users[0].uid, forward);
        for (let attempt = 0; attempt < 2; ++attempt) {
            assert.equal((await send(0, forward, 'direction-0: 世界 👋', attempt === 0 ? { copies: 2 } : {})).error, 0);
            assert.equal(await observe(users[0].uid, forward, 'direction-0: 世界 👋'), before);
        }
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE send_id=${users[0].uid} AND client_msg_uuid='${forward}'`), '1');
    });
    await record('E03-XMSG-04', 'conflicting payload leaves the original durable identity and content unchanged', /** 验证相同 UUID 的不同内容被拒绝，原消息身份和内容不变。 */ async () => {
        const before = await durable(users[0].uid, forward);
        const result = await send(0, forward, 'conflicting replacement');
        assert.ok(Number.isInteger(result.error) && result.error > 0);
        assert.equal(await observe(users[0].uid, forward, 'direction-0: 世界 👋'), before);
        assert.equal(await sql.execute(`SELECT content FROM chat_message WHERE send_id=${users[0].uid} AND client_msg_uuid='${forward}'`), 'direction-0: 世界 👋');
    });
    await record('E03-XMSG-05', 'invalid chat membership fails without persistence or recipient delivery', /** 验证不存在的会话拒绝发送且不写库、不通知对方。 */ async () => {
        const uuid = randomUUID();
        const before = (await rows(bob)).length;
        const result = await send(0, uuid, 'must not commit', { chatId: 2147483647 });
        assert.ok(Number.isInteger(result.error) && result.error > 0);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '0');
        assert.equal((await rows(bob)).length, before);
    });
    await record('E03-XMSG-07', 'nonmember cannot send or read a real private chat and retries cannot change its scope', /** 验证非成员不能发送或读取另一私聊的消息。 */ async () => {
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
        assert.ok(!(await bob.control.command('snapshot', { chatId: privateChat })).messages.some(/** 在本地快照中查找应受成员权限保护的消息。 */ row => row.uuid === privateUuid));
        assert.equal((await alice.control.command('history', { chatId: privateChat, cursor: '0' })).error, 0);
        const restored = (await alice.control.command('snapshot', { chatId: privateChat })).messages;
        assert.equal(restored.find(/** 检查另一快照是否泄漏受保护的私聊消息。 */ row => row.uuid === privateUuid)?.messageId, privateId);
        const original = await durable(users[0].uid, forward);
        const conflict = await send(0, forward, 'direction-0: 世界 👋', { chatId: privateChat, toUid: outsider.uid });
        assert.ok(Number.isInteger(conflict.error) && conflict.error > 0);
        assert.equal(await durable(users[0].uid, forward), original);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${forward}'`), '1');
    });
    for (const client of clients) assert.equal((await rows(client)).length, 5);
}

/** 在对端离线时验证提交持久性、UUID 重试和授权历史恢复。 */
async function runOfflineMessageCase({ bob, users, sql, record, chatId }) {
    await record('E03-XMSG-06', 'offline peer keeps a committed message recoverable by authorized history and UUID retry', /** 发送离线消息并重试，核对唯一持久化记录及历史可恢复性。 */ async () => {
        const uuid = randomUUID();
        const request = { chatId, toUid: users[0].uid, uuid, text: 'offline durable message' };
        await bob.control.command('send', request);
        const id = await sql.execute(`SELECT message_id FROM chat_message WHERE send_id=${users[1].uid} AND client_msg_uuid='${uuid}'`);
        assert.match(id, /^[1-9][0-9]*$/);
        await bob.control.command('send', request);
        assert.equal(await sql.execute(`SELECT message_id FROM chat_message WHERE send_id=${users[1].uid} AND client_msg_uuid='${uuid}'`), id);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
        assert.equal((await bob.control.command('history', { chatId, cursor: '0' })).error, 0);
        const matches = (await bob.control.command('snapshot', { chatId })).messages.filter(/** 从历史快照定位本次离线消息。 */ row => row.uuid === uuid);
        assert.equal(matches.length, 1);
        assert.equal(matches[0].messageId, id);
    });
}

module.exports = { runMessagingCases, runOfflineMessageCase };
