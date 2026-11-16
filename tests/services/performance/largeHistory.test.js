'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const profile = require('./profiles.json')['large-history'];
const { selectPolicy } = require('../../../scripts/ci/ciPolicy');
const { readWindow, verifyDataset } = require('./largeHistory');

// 伪造输入也不能使 push、PR 或定时任务选择历史负载。
test('large history is opted into only on manual dispatch', () => {
    const event = { inputs: { mode: 'performance', performance_profile: 'large-history' },
        repository: { default_branch: 'develop' }, pull_request: { base: { ref: 'develop' } } };
    for (const trigger of ['push', 'pull_request', 'schedule']) {
        assert.notEqual(selectPolicy(trigger, 'refs/heads/develop', event).mode, 'performance');
    }
    assert.equal(selectPolicy('workflow_dispatch', 'refs/heads/develop', event).performance_profile, 'large-history');
    assert.equal(selectPolicy('workflow_dispatch', 'refs/heads/develop', { inputs: { mode: 'performance' } })
        .performance_profile, 'baseline');
});

// 即使总数正确，一个会话的历史缺失或分布错误也不能出具通过的报告。
test('large history requires the measured million rows and per-chat distribution', () => {
    const rowsPerChat = [...Array(4).fill(200000), ...Array(16).fill(12500)];
    verifyDataset({ rowsPerChat, totalRows: 1000000 }, profile.dataset);
    // 报告合计少一条时应拒绝通过。
    assert.throws(() => verifyDataset({ rowsPerChat, totalRows: 999999 }, profile.dataset));
    // 总数正确但各会话数据分布错误时仍然失败。
    assert.throws(() => verifyDataset({ rowsPerChat: rowsPerChat.toReversed(), totalRows: 1000000 }, profile.dataset));
    // 未提供数据库行数时拒绝通过。
    assert.throws(() => verifyDataset(undefined, profile.dataset));
});

// 在深游标之后验证完整分页，受控破坏的响应必须使加载失败。
test('deep history reads verify content, identities, page order and completeness', async () => {
    const prefix = '00000001-0000-4000-8000-', start = 100000;
    for (const fault of ['none', 'content', 'identity', 'cursor', 'recipient', 'truncated']) {
        let requests = 0;
        const pair = { first: { uid: 1 }, chatId: 3, slot: 1, prefix, cursors: new Map([[start, start * 2]]),
            second: { uid: 2,
                // 用跳号主键模拟其他会话与当前会话交错写入。
                async correlated(id, request) {
                    assert.equal(id, 1027);
                    assert.equal(request.after_id, (start + requests * 50) * 2);
                    const first = start + requests++ * 50 + 1;
                    // 只污染对应断言的数据，以证明每项验证生效。
                    const msgs = Array.from({ length: 50 }, (_, index) => {
                        const n = first + index;
                        return { message_id: 2 * n, send_id: 1, recv_id: fault === 'recipient' ? 3 : 2,
                            msg_uuid: prefix + String(fault === 'identity' ? n + 1 : n).padStart(12, '0'),
                            content: fault === 'content' ? 'wrong' : `history-1-${n}/`.padEnd(256, 'x') };
                    });
                    return { msgs, next_cursor: fault === 'cursor' ? 10 : msgs.at(-1).message_id,
                        load_more: fault !== 'truncated' };
                } } };
        if (fault === 'none') { await readWindow(pair, start, 256); assert.equal(requests, 2); }
        else await assert.rejects(readWindow(pair, start, 256), /history-/);
    }
});
