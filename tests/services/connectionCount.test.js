'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { performance } = require('node:perf_hooks');
const { setImmediate: nextTurn } = require('node:timers/promises');

test('connection count waits for a production refresh while keeping both controllers active', /** 用虚拟时间验证等待连接数期间持续维持客户端活跃，且最终释放 Redis。 */ async t => {
    t.mock.timers.enable({ apis: ['setTimeout'] });
    const { waitForConnectionCount } = require('./connectionCount');
    let now = 0;
    t.mock.method(performance, 'now', /** 提供受测试驱动的单调时间。 */ () => now);
    let disconnected = false;
    const lastSnapshot = [0, 0];
    const controls = lastSnapshot.map(/** 为每个逻辑客户端建立独立的活跃时间观察器。 */ (_, index) => ({
        command: /** 核对快照命令及空闲间隔，并刷新该客户端活跃时刻。 */ async command => {
            assert.equal(command, 'snapshot');
            assert.ok(now - lastSnapshot[index] < 60000, 'controller inactivity deadline');
            lastSnapshot[index] = now;
            return { status: 'snapshot' };
        }
    }));
    const coordinator = { redis: /** 提供按虚拟发布周期更新连接数的 Redis 替身。 */ async () => ({
        hget: /** 核对实例键并在发布周期到达后返回目标计数。 */ async (key, name) => {
            assert.equal(key, 'logincount');
            assert.equal(name, 'owned-server');
            return now >= 60000 ? '1' : '0';
        },
        disconnect: /** 记录 Redis 连接已经释放。 */ () => { disconnected = true; }
    }) };
    let outcome;
    const pending = waitForConnectionCount(coordinator, 'owned-server', 1, controls)
        .then(/** 记录连接数等待成功。 */ () => { outcome = 'pass'; }, /** 保存等待失败的诊断供断言。 */ error => { outcome = error.message; });
    for (; now <= 71000 && outcome === undefined; now += 100) {
        t.mock.timers.tick(100);
        await nextTurn();
    }
    assert.equal(outcome, 'pass');
    assert.ok(lastSnapshot.every(/** 确认客户端活跃维护持续到发布周期附近。 */ value => value >= 59900));
    assert.equal(disconnected, true);
    await pending;

    // A count that never reaches the requested value must still fail and release Redis.
    disconnected = false;
    outcome = undefined;
    const stalled = waitForConnectionCount(coordinator, 'owned-server', 0, controls)
        .then(/** 标记不应发生的成功，以检测超时合同失效。 */ () => { outcome = 'unexpected-pass'; }, /** 保存超时路径的错误供断言。 */ error => { outcome = error.message; });
    const limit = now + 71000;
    for (; now <= limit && outcome === undefined; now += 100) {
        t.mock.timers.tick(100);
        await nextTurn();
    }
    assert.equal(outcome, 'health deadline');
    assert.equal(disconnected, true);
    await stalled;
});
