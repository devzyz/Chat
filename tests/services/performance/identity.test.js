'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { options, validateRun } = require('../../../scripts/ci/performance');
const { sync } = require('./scenarios');

test('skill validates source and exact run and refuses malformed options', /** 验证技能不会下载另一场运行的证据。 */ () => {
    const receipt = { sha: 'a'.repeat(40), runId: '123' };
    const run = { headSha: receipt.sha, databaseId: 123, event: 'workflow_dispatch', workflowName: 'CI' };
    validateRun(run, receipt);
    for (const change of [{ headSha: 'b'.repeat(40) }, { databaseId: 124 }, { event: 'push' }, { workflowName: 'other' }]) {
        assert.throws(/** 不同身份必须失败。 */ () => validateRun({ ...run, ...change }, receipt));
    }
    assert.deepEqual(options(['--ref', 'develop', '--profile', 'smoke']), { '--ref': 'develop', '--profile': 'smoke' });
    for (const args of [['--ref'], ['--unknown', 'x'], ['--ref', 'x', '--ref', 'y']]) {
        assert.throws(/** 未知、遗漏、重复参数均拒绝。 */ () => options(args));
    }
});

test('history requires strictly advancing pages and unique message identities', /** 验证补拉不接受游标卡住或重复消息。 */ async () => {
    const pages = [
        { msgs: [{ message_id: 1, msg_uuid: 'first' }], next_cursor: 1, load_more: true },
        { msgs: [{ message_id: 2, msg_uuid: 'second' }], next_cursor: 2, load_more: false }
    ];
    const client = { uid: 1, /** 按调用顺序交付权威历史夹具。 */ async correlated() { return pages.shift(); } };
    const result = await sync(client, 1); assert.equal(result.cursor, 2); assert.equal(result.rows.length, 2);
    for (const page of [
        { msgs: [], next_cursor: 0, load_more: true },
        { msgs: [{ message_id: 0, msg_uuid: 'bad' }], next_cursor: 0, load_more: false },
        { msgs: [{ message_id: 1, msg_uuid: 'same' }, { message_id: 2, msg_uuid: 'same' }], next_cursor: 2, load_more: false },
        { msgs: [{ message_id: 2, msg_uuid: 'first' }, { message_id: 1, msg_uuid: 'second' }], next_cursor: 1, load_more: false },
        { msgs: [{ message_id: 1, msg_uuid: 'bad' }], next_cursor: 2, load_more: false }
    ]) {
        await assert.rejects(sync({ uid: 1, /** 返回受控非法页面。 */ async correlated() { return page; } }, 1));
    }
    const systemHistory = await sync({ uid: 1,
        /** 返回生产好友建立时创建的无 UUID 系统历史。 */ async correlated() {
            return { msgs: [{ message_id: 1, msg_uuid: '' }, { message_id: 2, msg_uuid: '' }], next_cursor: 2, load_more: false };
        } }, 1);
    assert.equal(systemHistory.rows.length, 2);
});
