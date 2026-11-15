'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { rates, verifyWindow } = require('./comprehensive');
const { verifyDataset } = require('./largeHistory');
const { selectPolicy } = require('../../../scripts/ci/ciPolicy');
const { comprehensive: profile } = require('./profiles.json');

// 验证显式手动选择、新旧负载独立、完整场景和实存数据分布。
test('comprehensive workload requires manual dispatch and six million rows across 120 chats', () => {
    const event = { inputs: { mode: 'performance', performance_profile: 'comprehensive' },
        repository: { default_branch: 'develop' }, pull_request: { base: { ref: 'develop' } } };
    for (const trigger of ['push', 'pull_request', 'schedule']) {
        assert.notEqual(selectPolicy(trigger, 'refs/heads/develop', event).mode, 'performance');
    }
    assert.equal(selectPolicy('workflow_dispatch', 'refs/heads/develop', event).performance_profile, 'comprehensive');
    const rowsPerChat = [...Array(24).fill(200000), ...Array(96).fill(12500)];
    verifyDataset({ rowsPerChat, totalRows: 6000000 }, profile.dataset);
    assert.equal(profile.connections, 240);
    assert.equal(Object.keys(rates(profile)).length, 15);
});

// 模拟真实样本数量，以受控缺项确认热私聊、群接收、混合传输和离线账号都被校验。
test('comprehensive windows reject undersized loads and missing delivery and transfer evidence', () => {
    // 构造只含本门禁需要的固定指标。
    const metric = count => ({ pass: true, planned: count, completed: count, unsent: 0, errors: {},
        latencyMs: { count }, ackMs: { count }, deliveryMs: { count } });
    const normal = metric(6000), hot = metric(9000);
    const videoBytes = 262144, bytes = 9 * (68 + 1048576 + 10485760 + videoBytes);
    const files = { ...metric(36), bytes, plannedBytes: bytes, elapsedSeconds: 30, mibPerSecond: bytes / 1048576 / 30 };
    verifyWindow(profile, 'hot-private', hot, 30);
    assert.throws(() => verifyWindow(profile, 'hot-private', normal, 30));
    verifyWindow(profile, 'mixed', { metrics: normal, resources: files }, 30, videoBytes);
    assert.throws(() => verifyWindow(profile, 'mixed', { metrics: normal }, 30));
    assert.throws(() => verifyWindow(profile, 'resources', { ...files, bytes: bytes - videoBytes }, 30, videoBytes));
    assert.throws(() => verifyWindow(profile, 'resources', { ...files, plannedBytes: 10 }, 30, videoBytes));
    assert.throws(() => verifyWindow(profile, 'resources', { ...files, mibPerSecond: 1 }, 30, videoBytes));
    verifyWindow(profile, 'group', { ...metric(360), deliveryMs: { count: 6840 } }, 30);
    assert.throws(() => verifyWindow(profile, 'group', metric(360), 30));
    verifyWindow(profile, 'offline', metric(120), 6);
    assert.throws(() => verifyWindow(profile, 'offline', metric(119), 30));
});
