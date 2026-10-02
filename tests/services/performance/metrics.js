'use strict';
const assert = require('node:assert/strict');
const { performance, monitorEventLoopDelay } = require('node:perf_hooks');
const { setTimeout: delay } = require('node:timers/promises');

/** 计算最近秩百分位，空样本保持 null，不伪报零延迟。 */
function distribution(samples) {
    if (!samples.length) return { count: 0, p50: null, p95: null, p99: null, max: null };
    assert.ok(samples.every(/** 只接收有限非负观测值。 */ value => Number.isFinite(value) && value >= 0));
    const sorted = [...samples].sort(/** 按数值升序排列观测。 */ (a, b) => a - b);
    const percentile = /** 读取最近秩百分位。 */ p => sorted[Math.ceil(sorted.length * p) - 1];
    return { count: sorted.length, p50: percentile(0.5), p95: percentile(0.95), p99: percentile(0.99), max: sorted.at(-1) };
}

/** 使用固定计划时刻投递负载；超出积压上限时记录未发送，不补发突发流量。 */
async function measure({ seconds, rate, maxPending, action, signal, plannedCount }) {
    const start = performance.now();
    const pending = new Set();
    const latency = [], lag = [], errors = {};
    let sent = 0, completed = 0, unsent = 0;
    const loop = monitorEventLoopDelay({ resolution: 20 }); loop.enable();
    const total = plannedCount ?? Math.ceil(seconds * rate);
    assert.ok(Number.isSafeInteger(total) && total > 0);
    for (let index = 0; index < total; index++) {
        if (signal?.aborted) break;
        const due = start + index * 1000 / rate;
        const wait = due - performance.now();
        if (wait > 0) await delay(wait);
        if (signal?.aborted) break;
        const late = Math.max(0, performance.now() - due); lag.push(late);
        if (pending.size >= maxPending || late > Math.max(100, 1000 / rate)) { unsent++; continue; }
        sent++;
        const begin = performance.now();
        const operation = Promise.resolve().then(/** 启动一个真实业务动作。 */ () => action(index))
            .then(/** 保存成功完成的业务延迟。 */ () => { completed++; latency.push(performance.now() - begin); })
            .catch(/** 聚合固定错误分类，避免记录凭据及报文。 */ error => {
                const category = /^[a-z][a-z0-9-]{0,70}$/.test(error.message) ? error.message : 'assertion-or-operation';
                errors[category] = (errors[category] || 0) + 1;
            }).finally(/** 从有界在途集合移除完成动作。 */ () => pending.delete(operation));
        pending.add(operation);
    }
    await Promise.all(pending);
    const remainder = start + seconds * 1000 - performance.now();
    if (remainder > 0 && !signal?.aborted) await delay(remainder);
    const end = performance.now(); loop.disable();
    signal?.throwIfAborted();
    const elapsedSeconds = Math.max(seconds, (end - start) / 1000);
    return { planned: total, sent, completed, unsent, errors, seconds, elapsedSeconds,
        throughput: completed / elapsedSeconds, latencyMs: distribution(latency), schedulingLagMs: distribution(lag),
        eventLoopMs: { p95: loop.percentile(95) / 1e6, max: loop.max / 1e6 },
        pass: completed === total && Object.keys(errors).length === 0 && unsent === 0 };
}

/** 校验完整单次场景证据，缺样本或任一业务失败均不可通过。 */
function validate(result) {
    assert.equal(result.pass, true, 'scenario-failed');
    assert.ok(result.completed > 0 && result.latencyMs.count === result.completed, 'missing-samples');
    assert.equal(result.completed, result.planned);
    assert.equal(result.unsent, 0);
    assert.equal(Object.keys(result.errors).length, 0);
}
module.exports = { distribution, measure, validate };
