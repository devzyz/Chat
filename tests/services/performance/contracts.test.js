'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { once } = require('node:events');
const { setTimeout: delay } = require('node:timers/promises');
const { encode, Decoder, Client } = require('./client');
const { distribution, measure, validate } = require('./metrics');
const { verify, scenarios } = require('./report');
const { selectPolicy } = require('../../../scripts/ci/ciPolicy');

test('frames preserve fragmentation and coalescing and reject malformed lengths', /** 使用真实编码字节验证增量解析。 */ () => {
    const decoder = new Decoder(), frames = [];
    const input = Buffer.concat([encode(1006, { error: 0 }), encode(1021, { error: 0 })]);
    for (const byte of input) decoder.feed(Buffer.from([byte]), /** 收集解码结果。 */ (id, value) => frames.push([id, value]));
    assert.deepEqual(frames, [[1006, { error: 0 }], [1021, { error: 0 }]]);
    const combined = []; new Decoder().feed(input, /** 收集合包帧。 */ id => combined.push(id));
    assert.deepEqual(combined, [1006, 1021]);
    assert.throws(/** 零长度报文不能被接收。 */ () => new Decoder().feed(Buffer.from([3, 238, 0, 0]), () => {}));
    assert.throws(/** 超限请求必须在写 socket 前拒绝。 */ () => encode(1016, { body: 'x'.repeat(2048) }));
});

/** 创建本次 loopback 服务器，所有 socket 在测试结束时清理。 */
async function fixture(t, receive) {
    const sockets = new Set();
    const server = net.createServer(/** 在连接上使用相同生产帧合同。 */ socket => {
        sockets.add(socket); const decoder = new Decoder();
        socket.on('data', /** 把完整请求交给测试场景。 */ bytes => decoder.feed(bytes,
            /** 登录由夹具确认，其他请求由用例驱动。 */ (id, value) => {
                if (id === 1005) socket.write(encode(1006, { error: 0 })); else receive(socket, id, value);
            }));
    });
    server.listen(0, '127.0.0.1'); await once(server, 'listening');
    t.after(/** 清理本次监听和所有已接受连接。 */ async () => {
        for (const socket of sockets) socket.destroy(); server.close(); await once(server, 'close');
    });
    const client = new Client(server.address().port, 1, 'fixture', 100);
    t.after(/** 结束测试客户端心跳与等待者。 */ () => client.close());
    return client.connect();
}

test('correlation handles out-of-order responses and notifications', /** 错序真实 socket 回包必须仍匹配正确请求。 */ async t => {
    const received = [];
    const client = await fixture(t, /** 收集两个请求并倒序返回。 */ (socket, id, value) => {
        received.push(value);
        if (received.length === 2) {
            socket.write(encode(1018, { error: 0, notify_msgs: [] }));
            for (const item of [...received].reverse()) socket.write(encode(id + 1, { ...item, error: 0 }));
        }
    });
    const results = await Promise.all([client.correlated(1036, { index: 1 }), client.correlated(1036, { index: 2 })]);
    assert.deepEqual(results.map(/** 提取响应与原请求的关联。 */ value => value.index), [1, 2]);
});

test('timeouts, business errors, disconnect and backpressure reject pending work', /** 验证失败路径不能被当作业务成功。 */ async t => {
    const client = await fixture(t, /** 默认不回包以验证截止时间。 */ () => {});
    await assert.rejects(client.request(1020, 1021, { uid: 1 }), /timeout/);
    const pending = client.request(1020, 1021, { uid: 1 }); client.close(); await assert.rejects(pending, /shutdown/);
    const denied = await fixture(t, /** 返回明确业务拒绝。 */ (socket, id) => socket.write(encode(id + 1, { error: 7 })));
    await assert.rejects(denied.request(1020, 1021, { uid: 1 }), /business/);
    const requests = Array.from({ length: 64 }, /** 填满有限在途请求。 */ () => denied.correlated(1036, {}));
    const completed = Promise.allSettled(requests);
    await assert.rejects(denied.correlated(1036, {}), /backpressure/); denied.close(); await completed;
});

test('percentiles and empty samples cannot manufacture a pass', /** 核对统计边界和缺失样本拒绝。 */ () => {
    assert.equal(distribution([]).p95, null);
    assert.deepEqual(distribution([4, 1, 3, 2]), { count: 4, p50: 2, p95: 4, p99: 4, max: 4 });
    assert.throws(/** 非数值样本不能参与统计。 */ () => distribution([NaN]));
    assert.throws(/** 空报告即便声称成功也应拒绝。 */ () => validate({ pass: true, completed: 0 }));
});

test('fixed rate accounts for successful, failed and unsent operations', /** 通过有界慢操作验证压测端不会隐藏积压。 */ async () => {
    const good = await measure({ seconds: 0.03, rate: 100, maxPending: 3, action: /** 立即完成确定性动作。 */ async () => {} });
    validate(good); assert.equal(good.planned, 3);
    const slow = await measure({ seconds: 0.1, rate: 100, maxPending: 1, action: /** 制造明确在途积压。 */ () => delay(80) });
    assert.ok(slow.unsent > 0); assert.equal(slow.pass, false);
    const bad = await measure({ seconds: 0.01, rate: 100, maxPending: 1, action: /** 注入业务失败。 */ async () => { throw new Error('fixture-failure'); } });
    assert.equal(bad.errors['fixture-failure'], 1); assert.equal(bad.pass, false);
});

test('report rejects missing scenes, duplicate scenes, wrong SHA and incomplete cleanup', /** 用完整夹具及受控损坏验证证据门禁。 */ () => {
    const sha = 'a'.repeat(40);
    const metric = { pass: true, completed: 1, planned: 1, unsent: 0, errors: {}, latencyMs: { count: 1 } };
    const report = { sha, status: 'passed', profile: 'smoke', cleanup: { complete: true }, failures: [],
        results: scenarios.map(/** 创建完整唯一场景证据。 */ name => ({ round: 1, name, metrics: metric })) };
    verify(report, sha);
    assert.throws(/** 源码身份不符拒绝。 */ () => verify(report, 'b'.repeat(40)));
    assert.throws(/** 缺场景拒绝。 */ () => verify({ ...report, results: report.results.slice(1) }, sha));
    assert.throws(/** 重复场景拒绝。 */ () => verify({ ...report, results: [...report.results, report.results[0]] }, sha));
    assert.throws(/** 清理缺失拒绝。 */ () => verify({ ...report, cleanup: null }, sha));
});

test('performance is manually isolated from full, quick, maintenance and publication', /** 验证性能选择与已有 CI 路由兼容。 */ () => {
    const event = { repository: { default_branch: 'develop' }, inputs: { mode: 'performance', performance_profile: 'smoke' } };
    assert.equal(selectPolicy('workflow_dispatch', 'refs/heads/feature', event).performance_profile, 'smoke');
    assert.equal(selectPolicy('push', 'refs/heads/develop', event).mode, 'quick');
    for (const flag of ['refresh_tools', 'cold_linux', 'real_acceptance']) {
        assert.throws(/** 混用维护专项时不能静默退化。 */ () => selectPolicy('workflow_dispatch', 'refs/heads/develop',
            { ...event, inputs: { ...event.inputs, [flag]: true } }));
    }
    assert.throws(/** 未知负载档位必须失败。 */ () => selectPolicy('workflow_dispatch', 'refs/heads/develop',
        { ...event, inputs: { mode: 'performance', performance_profile: 'unknown' } }));
});
