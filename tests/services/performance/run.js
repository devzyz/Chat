'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const { performance } = require('node:perf_hooks');
const { execFileSync } = require('node:child_process');
const { Environment } = require('./environment');
const profiles = require('./profiles.json');
const { measure, validate } = require('./metrics');
const { prepare, privateMessages, groupMessages, prepareOffline, recoverOffline, resources } = require('./scenarios');
const { write, verify } = require('./report');

/** 运行完整性能矩阵，逐场景保留证据，任何失败都不绕过最终清理。 */
async function run() {
    const profileName = process.env.CHAT_PERFORMANCE_PROFILE || 'baseline';
    assert.ok(Object.hasOwn(profiles, profileName));
    const profile = profiles[profileName];
    const root = path.resolve(process.env.CHAT_PERFORMANCE_OUTPUT || 'out/performance');
    const sha = process.env.GITHUB_SHA; assert.match(sha || '', /^[a-f0-9]{40}$/);
    const report = { schemaVersion: 2, sha, profile: profileName, parameters: profile,
        requestId: process.env.CHAT_PERFORMANCE_REQUEST_ID || '', seed: 'chat-performance-v1',
        url: `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`,
        environment: { cpus: os.cpus().length, cpuModel: os.cpus()[0].model, memoryBytes: os.totalmem(),
            node: process.version, kernel: os.release(), cpuTicksPerSecond: Number(execFileSync('getconf', ['CLK_TCK'], { encoding: 'utf8' })) },
        status: 'running', results: [], warmups: [], failures: [], phases: [], samples: [] };
    const controller = new AbortController();
    let env, sampler, previousSample, stage = 'setup', round = 0;
    const started = performance.now();
    // 为准备、预热和测量分别保存耗时。
    const phase = async (name, action) => {
        if (name !== 'cleanup') controller.signal.throwIfAborted();
        const begin = performance.now(); stage = `${round}-${name}`;
        try { return await action(); }
        finally { report.phases.push({ round, name, seconds: (performance.now() - begin) / 1000 }); }
    };
    // GitHub 超时预留清理窗口。
    const onSignal = () => {
        controller.abort(new Error('run-interrupted'));
        for (const client of env?.clients || []) client.close();
    };
    process.once('SIGTERM', onSignal); process.once('SIGINT', onSignal);
    try {
        env = new Environment(controller.signal);
        // 启动真实隔离环境。
        await phase('setup', () => env.setup());
        // 在负载采样前验证审查发现的生产边界。
        await phase('audit-regressions', () => env.auditRegressions());
        // 采样每个正式服务的资源使用，采样失败可见。
        sampler = setInterval(() => {
            try {
                const sample = { elapsedSeconds: (performance.now() - started) / 1000, stage, processes: env.sample() };
                for (const item of sample.processes) {
                    // 仅比较相同真实 PID。
                    const previous = previousSample?.processes.find(value => value.pid === item.pid);
                    item.cpuPercent = previous ? (item.cpuTicks - previous.cpuTicks) / report.environment.cpuTicksPerSecond /
                        (sample.elapsedSeconds - previousSample.elapsedSeconds) * 100 : null;
                }
                report.samples.push(sample); previousSample = sample;
            }
            catch { report.failures.push({ stage, category: 'process-sample-failed' }); }
        }, 1000);
        // 执行多轮独立账号数据的完整场景集合。
        const execute = async () => {
            for (round = 1; round <= profile.repeats; round++) {
                const accounts = [];
                // 准备独立账号，不将注册邮件吞吐混入登录指标。
                await phase('accounts', async () => {
                    for (let index = 0; index < profile.connections; index++) accounts.push(await env.register(round * 10000 + index));
                });
                // 先预热再测量，数据准备独立计时，原始失败指标立即保存。
                const scenario = async (name, action, prepareAction) => {
                    const warmupFixture = prepareAction ? await phase(`${name}-warmup-prepare`, prepareAction) : undefined;
                    // 使用相同业务路径预热。
                    const warmup = await phase(`${name}-warmup`, () => action(profile.warmupSeconds, warmupFixture));
                    report.warmups.push({ round, name, result: warmup }); write(root, report);
                    try {
                        validate(warmup.metrics || warmup);
                        if (name === 'mixed') validate(warmup.resources);
                    } catch { report.failures.push({ stage: `${round}-${name}-warmup`, category: 'warmup-business-failed' }); }
                    const fixture = prepareAction ? await phase(`${name}-prepare`, prepareAction) : undefined;
                    // 运行所选档位的正式测量窗口。
                    const result = await phase(`${name}-measure`, () => action(profile.seconds, fixture));
                    const row = { round, name, ...(result.metrics ? result : { metrics: result }) };
                    report.results.push(row); write(root, report);
                    // Continue other scenes to preserve diagnostic coverage; final verification fails closed.
                };
                // 固定账号集合登录一次，覆盖 Gate 选服及 Chat 凭据校验。
                await scenario('login', seconds => measure({
                    seconds, rate: accounts.length / seconds, plannedCount: accounts.length, maxPending: accounts.length, signal: controller.signal,
                    // 保持每账号单会话语义。
                    action: async index => { accounts[index].client?.close(); await env.login(accounts[index]); }
                }));
                // 通过真实连接数发布及选服形成双实例负载。
                await phase('instance-publication', () => env.balance(accounts));
                // 取正式认证客户端。
                const clients = accounts.map(account => account.client);
                // 所有连接必须真正认证且仍存活。
                assert.ok(clients.every(client => client && !client.closed), 'missing-connections');
                // 建立生产好友及群聊关系。
                const topology = await phase('relationships', () => prepare(env, clients));
                // 测量同实例私聊。
                await scenario('same-server', seconds => privateMessages(env, topology.same, clients, profile, seconds));
                // 测量跨实例私聊。
                await scenario('cross-server', seconds => privateMessages(env, topology.cross, clients, profile, seconds));
                // 测量群提交和二十人成员补拉。
                await scenario('group', seconds => groupMessages(env, topology.groups, profile, seconds));
                // 测量图片及附件传输。
                await scenario('resources', seconds => resources(env, topology.cross, profile, seconds));
                // 同时测量跨服文本和资源传输的互相影响。
                await scenario('mixed', async seconds => {
                    const outcomes = await Promise.allSettled([privateMessages(env, topology.cross, clients, profile, seconds),
                        resources(env, topology.cross, profile, seconds)]);
                    for (const outcome of outcomes) if (outcome.status === 'rejected') throw outcome.reason;
                    // 读取已完整收敛的业务结果。
                    const [messages, files] = outcomes.map(outcome => outcome.value);
                    return { metrics: messages, resources: files };
                });
                // 测量每个会话的一百条离线消息恢复。
                await scenario('offline', (seconds, fixture) =>
                    recoverOffline(env, topology.cross, seconds, fixture),
                // 为预热及正式测量各自重新生成一百条离线积压。
                () => prepareOffline(env, topology.cross));
                for (const account of accounts) account.client.close();
            }
        };
        await execute();
    } catch (error) {
        const firstLine = error.message.split('\n')[0];
        report.failures.push({ stage, category: /^[a-z][a-z0-9-]{0,70}$/.test(firstLine) ? firstLine : 'assertion-or-operation',
            actualNumber: typeof error.actual === 'number' ? error.actual : undefined,
            expectedNumber: typeof error.expected === 'number' ? error.expected : undefined,
            // 只保存栈位置，不输出断言对象。
            location: error.stack?.split('\n').find(line => /^\s+at /.test(line))?.trim() });
    } finally {
        clearInterval(sampler);
        if (env) {
            // 收集全部资源的最终清理证据。
            try { report.cleanup = await phase('cleanup', () => env.teardown(root)); }
            catch { report.cleanup = { complete: false }; }
        }
        if (!report.cleanup?.complete) report.failures.push({ stage: 'cleanup', category: 'cleanup-incomplete' });
        report.status = 'passed';
        try { verify(report, sha); } catch { report.status = 'failed'; }
        const markdown = write(root, report);
        if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, markdown);
        process.off('SIGTERM', onSignal); process.off('SIGINT', onSignal);
    }
    if (report.status !== 'passed') process.exitCode = 1;
}
// 初始化失败必须使 CI 失败，不输出凭据。
if (require.main === module) run().catch(() => { console.error('performance initialization failed'); process.exitCode = 1; });
module.exports = { run };
