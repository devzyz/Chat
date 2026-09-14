'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { performance } = require('node:perf_hooks');
const { setImmediate: nextTurn } = require('node:timers/promises');

test('connection count waits for a production refresh while keeping both controllers active', async t => {
    t.mock.timers.enable({ apis: ['setTimeout'] });
    const { waitForConnectionCount } = require('./connectionCount');
    let now = 0;
    t.mock.method(performance, 'now', () => now);
    let disconnected = false;
    const lastSnapshot = [0, 0];
    const controls = lastSnapshot.map((_, index) => ({
        command: async command => {
            assert.equal(command, 'snapshot');
            assert.ok(now - lastSnapshot[index] < 60000, 'controller inactivity deadline');
            lastSnapshot[index] = now;
            return { status: 'snapshot' };
        }
    }));
    const coordinator = { redis: async () => ({
        hget: async (key, name) => {
            assert.equal(key, 'logincount');
            assert.equal(name, 'owned-server');
            return now >= 60000 ? '1' : '0';
        },
        disconnect: () => { disconnected = true; }
    }) };
    let outcome;
    const pending = waitForConnectionCount(coordinator, 'owned-server', 1, controls)
        .then(() => { outcome = 'pass'; }, error => { outcome = error.message; });
    for (; now <= 71000 && outcome === undefined; now += 100) {
        t.mock.timers.tick(100);
        await nextTurn();
    }
    assert.equal(outcome, 'pass');
    assert.ok(lastSnapshot.every(value => value >= 59900));
    assert.equal(disconnected, true);
    await pending;

    // A count that never reaches the requested value must still fail and release Redis.
    disconnected = false;
    outcome = undefined;
    const stalled = waitForConnectionCount(coordinator, 'owned-server', 0, controls)
        .then(() => { outcome = 'unexpected-pass'; }, error => { outcome = error.message; });
    const limit = now + 71000;
    for (; now <= limit && outcome === undefined; now += 100) {
        t.mock.timers.tick(100);
        await nextTurn();
    }
    assert.equal(outcome, 'health deadline');
    assert.equal(disconnected, true);
    await stalled;
});
