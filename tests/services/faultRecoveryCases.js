'use strict';
const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');
const { poll } = require('./dependencyCoordinator');

/** 验证 ACK 丢弃后的重试、通知重放去重及实例重启恢复，并核对中继证据。 */
async function runFaultRecoveryCases({ alice, bob, users, chatId, sql, record, prepareRelay,
    relogin, restartBob, invalidHistory, stopRelay }) {
    const dropUuid = randomUUID(), replayUuid = randomUUID();
    let relay;
    const rows = /** 读取指定客户端当前会话的消息列表。 */ client => client.control.command('snapshot', { chatId }).then(/** 从快照响应提取消息列表。 */ value => value.messages);
    const durable = /** 按本次生成的 UUID 查询持久化消息标识。 */ uuid => sql.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`);
    const send = /** 向指定用户发送带固定 UUID 的测试消息。 */ (client, target, uuid, text) => client.control.command('send', { chatId, toUid: target.uid, uuid, text });
    const observe = /** 等待双方各出现唯一已提交消息，并与数据库标识和内容摘要核对。 */ async (uuid, text, sender) => {
        let matches;
        await poll(/** 并行读取双方快照，直到目标消息均唯一且已有持久化标识。 */ async () => {
            matches = await Promise.all([alice, bob].map(/** 从一个客户端快照筛选目标 UUID 与发送方。 */ async client =>
                (await rows(client)).filter(/** 匹配目标消息身份，避免混入其他发送者记录。 */ row => row.uuid === uuid && row.sender === sender)));
            return matches.every(/** 确认仅一条目标记录且持久化标识大于零。 */ items => items.length === 1 && BigInt(items[0].messageId) > 0);
        }, 10000);
        const id = await durable(uuid);
        for (const items of matches) {
            assert.equal(items[0].messageId, id);
            assert.equal(items[0].sha256, createHash('sha256').update(text).digest('hex'));
        }
        return id;
    };
    await record('E03-RECOVER-05', 'owned codec relay replaces only the fault-path endpoint after server restart', /** 启动故障中继并让发送客户端经中继重新登录。 */ async () => {
        relay = await prepareRelay(dropUuid, replayUuid);
        await relogin(alice, 0);
    });
    let id;
    await record('E03-RECOVER-06', 'committed ACK is dropped and its client generation disconnects', /** 验证丢弃已提交 ACK 后客户端断开且保留未确认项，数据库已持久化。 */ async () => {
        const result = await send(alice, users[1], dropUuid, 'uncertain committed message');
        assert.equal(result.status, 'disconnected');
        const evidence = relay.read();
        assert.equal(evidence.droppedAck, true);
        id = await durable(dropUuid);
        assert.equal(evidence.committedId, id);
        const pending = (await rows(alice)).filter(/** 筛选 ACK 被丢弃的本地待确认消息。 */ row => row.uuid === dropUuid);
        assert.equal(pending.length, 1);
        assert.equal(pending[0].messageId, '0');
        assert.equal((await alice.control.command('snapshot')).active, false);
    });
    await record('E03-RECOVER-07', 'same process reauthenticates and retries original bytes to the same durable ID', /** 验证重新登录后重试收敛为原提交标识，数据库仍只有一条记录。 */ async () => {
        await relogin(alice, 0);
        assert.equal(await observe(dropUuid, 'uncertain committed message', users[0].uid), id);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${dropUuid}'`), '1');
    });
    await record('E03-RECOVER-08', 'replayed real peer notification produces one model item', /** 验证重放通知确实发生，双方模型仍保持目标消息唯一。 */ async () => {
        assert.equal((await send(bob, users[0], replayUuid, 'replayed notification')).error, 0);
        await observe(replayUuid, 'replayed notification', users[1].uid);
        assert.equal(relay.read().replayedNotification, true);
    });
    await record('E03-RECOVER-09', 'Chat B restart releases its old session and discovery restores cross-instance delivery', /** 验证 Chat B 重启及重登后，跨实例消息继续正常提交和展示。 */ async () => {
        await restartBob();
        await relogin(bob, 1);
        const uuid = randomUUID();
        assert.equal((await send(alice, users[1], uuid, 'after Chat B restart')).error, 0);
        await observe(uuid, 'after Chat B restart', users[0].uid);
    });
    await record('E03-RECOVER-10', 'invalid wire history cursors return bounded stable errors without messages', invalidHistory);
    await record('E03-RECOVER-11', 'fault relay releases every owned connection and records completed fault evidence', /** 停止所属中继并核对完成、丢弃、重放及提交标识证据。 */ async () => {
        await stopRelay(relay.owned);
        const evidence = relay.read();
        assert.equal(evidence.complete, true);
        assert.equal(evidence.droppedAck, true);
        assert.equal(evidence.replayedNotification, true);
        assert.equal(evidence.committedId, id);
    });
}
module.exports = { runFaultRecoveryCases };
