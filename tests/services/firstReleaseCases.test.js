'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
const { realAcceptanceEnabled, runFirstReleaseCases } = require('./firstReleaseCases');

test('real acceptance is opt in and refuses incomplete selectors', /** 验证默认关闭、明确开启和非法值均遵循严格门禁。 */ () => {
    for (const flag of ['', '0']) assert.equal(realAcceptanceEnabled('3D', flag), false);
    assert.equal(realAcceptanceEnabled('3D', '1'), true);
    for (const selector of ['3D-00', '3D-03', '3C-07'])
        assert.throws(/** 非完整选择器不能误触发首版专项。 */ () => realAcceptanceEnabled(selector, '1'));
    for (const flag of ['true', 'yes', '2'])
        assert.throws(/** 拒绝拼写错误以避免悄然跳过要求的验收。 */ () => realAcceptanceEnabled('3D', flag));
});

test('failed public authentication aborts release journey and retires owned client', /** 替身只验证编排失败语义，不充当真实业务验收证据。 */ async () => {
    const owned = {};
    const retired = [];
    const recorded = [];
    const users = [];
    const failure = new Error('public authentication rejected');
    await assert.rejects(runFirstReleaseCases({ users,
        keepAlive: /** 空编排环境无需真实连接保活。 */ async () => {},
        client: /** 返回唯一已登记的测试所有权对象。 */ async () => owned,
        authenticate: /** 注入认证失败检查调用者不会继续记成功。 */ async () => { throw failure; },
        retire: /** 记录 finally 释放的客户端所有权。 */ async instance => retired.push(instance),
        record: /** 按真实记录器方式等待场景结果，不吞掉错误。 */ async (id, name, action) => {
            recorded.push(id); await action();
        }
    }), failure);
    assert.deepEqual(recorded, ['E03-RELEASE-01']);
    assert.deepEqual(retired, [owned]);
    assert.equal(users.length, 1);
});
