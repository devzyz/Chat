'use strict';

const assert = require('node:assert/strict');
const { poll } = require('./dependencyCoordinator');

/** 通过真实客户端与数据库验证好友申请、接受、重复操作和会话一致性。 */
async function runFriendshipCases({ alice, bob, users, sql, record }) {
    const [first, second] = users;
    const fields = /** 构造含 Unicode 描述的好友请求字段。 */ toUid => ({ toUid, description: 'Hello, 世界 👋', backname: 'fixture peer' });
    const query = /** 在本次隔离数据库中执行断言查询。 */ statement => sql.execute(statement);
    const relation = /** 读取指定客户端对另一用户的关系快照。 */ (client, otherUid) => client.control.command('snapshot', { otherUid });
    await record('E03-JOURNEY-01', 'invalid self and unknown friend leave no application', /** 验证向自己或未知用户申请不会创建申请和好友记录。 */ async () => {
        for (const toUid of [first.uid, 2147483647]) {
            assert.notEqual((await alice.control.command('apply', fields(toUid))).error, 0);
        }
        assert.equal(await query('SELECT COUNT(*) FROM apply_friend'), '0');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '0');
    });
    await record('E03-JOURNEY-02', 'cross-instance application reaches the recipient production model', /** 验证有效申请通知对端，且尚未提前建立好友关系。 */ async () => {
        assert.equal((await alice.control.command('apply', fields(second.uid))).error, 0);
        await poll(/** 等待被申请方快照显示待处理申请。 */ async () => (await relation(bob, first.uid)).applied === true, 10000);
        assert.equal((await relation(bob, first.uid)).friend, false);
        assert.equal(await query(`SELECT COUNT(*) FROM apply_friend WHERE from_uid=${first.uid} AND to_uid=${second.uid} AND status=0`), '1');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '0');
    });
    await record('E03-JOURNEY-03', 'duplicate application preserves one pending durable relation', /** 验证重复申请不新增数据库记录或提前建立好友关系。 */ async () => {
        assert.equal((await alice.control.command('apply', fields(second.uid))).error, 0);
        assert.equal(await query('SELECT COUNT(*) FROM apply_friend'), '1');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '0');
        assert.equal((await relation(bob, first.uid)).applied, true);
    });
    let chatId;
    await record('E03-JOURNEY-04', 'recipient acceptance links both models and reciprocal durable friends', /** 验证接受申请后双方成为好友，并关联同一私聊会话。 */ async () => {
        assert.equal((await bob.control.command('accept', fields(first.uid))).error, 0);
        let a, b;
        await poll(/** 等待双方关系和会话标识达到一致。 */ async () => {
            a = await relation(alice, second.uid); b = await relation(bob, first.uid);
            return a.friend === true && b.friend === true && a.chatId > 0 && a.chatId === b.chatId;
        }, 10000);
        chatId = a.chatId;
        assert.equal(await query(`SELECT COUNT(*) FROM friend WHERE (self_id=${first.uid} AND other_id=${second.uid}) OR (self_id=${second.uid} AND other_id=${first.uid})`), '2');
        assert.equal(await query('SELECT COUNT(*) FROM friend'), '2');
        assert.equal(await query('SELECT COUNT(*) FROM apply_friend WHERE status=1'), '1');
        assert.equal(await query('SELECT COUNT(*) FROM private_chat'), '1');
    });
    await record('E03-JOURNEY-05', 'repeated acceptance fails without additional friends chats or greetings', /** 验证重复接受被拒绝，且好友、会话和消息记录保持不变。 */ async () => {
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
