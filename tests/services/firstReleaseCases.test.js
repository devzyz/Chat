'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
const { realAcceptanceEnabled, runFirstReleaseCases } = require('./firstReleaseCases');

// 验证默认关闭、明确开启和非法值均遵循严格门禁。
test('real acceptance is opt in and refuses incomplete selectors', () => {
    for (const flag of ['', '0']) assert.equal(realAcceptanceEnabled('3D', flag), false);
    assert.equal(realAcceptanceEnabled('3D', '1'), true);
    for (const selector of ['3D-00', '3D-03', '3C-07'])
        // 非完整选择器不能误触发首版专项。
        assert.throws(() => realAcceptanceEnabled(selector, '1'));
    for (const flag of ['true', 'yes', '2'])
        // 拒绝拼写错误以避免悄然跳过要求的验收。
        assert.throws(() => realAcceptanceEnabled('3D', flag));
});

// 替身只验证编排失败语义，不充当真实业务验收证据。
test('failed public authentication aborts release journey and retires owned client', async () => {
    const owned = {};
    const retired = [];
    const recorded = [];
    const users = [];
    const failure = new Error('public authentication rejected');
    await assert.rejects(runFirstReleaseCases({ users,
        // 空编排环境无需真实连接保活。
        keepAlive: async () => {},
        // 返回唯一已登记的测试所有权对象。
        client: async () => owned,
        // 注入认证失败检查调用者不会继续记成功。
        authenticate: async () => { throw failure; },
        // 记录 finally 释放的客户端所有权。
        retire: async instance => retired.push(instance),
        // 按真实记录器方式等待场景结果，不吞掉错误。
        record: async (id, name, action) => {
            recorded.push(id); await action();
        }
    }), failure);
    assert.deepEqual(recorded, ['E03-RELEASE-01']);
    assert.deepEqual(retired, [owned]);
    assert.equal(users.length, 1);
});
