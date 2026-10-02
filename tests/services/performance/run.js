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
const { prepare, privateMessages, groupMessages, offline, resources } = require('./scenarios');
const { write, verify } = require('./report');

/** 运行完整性能矩阵，逐场景保留证据，任何失败都不绕过最终清理。 */
async function run() {
    const profileName = process.env.CHAT_PERFORMANCE_PROFILE || 'baseline';
    assert.ok(Object.hasOwn(profiles, profileName));
    const profile = profiles[profileName];
    const root = path.resolve(process.env.CHAT_PERFORMANCE_OUTPUT || 'out/performance');
    const sha = process.env.GITHUB_SHA; assert.match(sha || '', /^[a-f0-9]{40}$/);
    const report = { schemaVersion: 1, sha, profile: profileName, parameters: profile,
        requestId: process.env.CHAT_PERFORMANCE_REQUEST_ID || '', seed: 'chat-performance-v1',
        url: `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`,
        environment: { cpus: os.cpus().length, cpuModel: os.cpus()[0].model, memoryBytes: os.totalmem(),
            node: process.version, kernel: os.release(), cpuTicksPerSecond: Number(execFileSync('getconf', ['CLK_TCK'], { encoding: 'utf8' })) },
        status: 'running', results: [], failures: [], phases: [], samples: [] };
    let env, sampler, stage = 'setup', round = 0;
    const started = performance.now();
    const phase = /** 为准备、预热和测量分别保存耗时。 */ async (name, action) => {
        const begin = performance.now(); stage = `${round}-${name}`;
        try { return await action(); }
        finally { report.phases.push({ round, name, seconds: (performance.now() - begin) / 1000 }); }
    };
    let abort;
    const interrupted = new Promise(/** 为外部截止信号建立失败通知。 */ (_, reject) => { abort = reject; });
    const onSignal = /** GitHub 超时预留清理窗口。 */ () => {
        for (const client of env?.clients || []) client.close(); abort(new Error('run-interrupted'));
    };
    process.once('SIGTERM', onSignal); process.once('SIGINT', onSignal);
    try {
        env = new Environment();
        await phase('setup', /** 启动真实隔离环境。 */ () => env.setup());
        sampler = setInterval(/** 采样每个正式服务的资源使用，采样失败可见。 */ () => {
            try { report.samples.push({ elapsedSeconds: (performance.now() - started) / 1000, stage, processes: env.sample() }); }
            catch { report.failures.push({ stage, category: 'process-sample-failed' }); }
        }, 1000);
        const execute = /** 执行多轮独立账号数据的完整场景集合。 */ async () => {
            for (round = 1; round <= profile.repeats; round++) {
                const accounts = [];
                await phase('accounts', /** 准备独立账号，不将注册邮件吞吐混入登录指标。 */ async () => {
                    for (let index = 0; index < profile.connections; index++) accounts.push(await env.register(round * 10000 + index));
                });
                const scenario = /** 先预热再测量，原始失败指标立即保存。 */ async (name, action) => {
                    const warmup = await phase(`${name}-warmup`, /** 使用相同业务路径预热。 */ () => action(profile.warmupSeconds));
                    validate(warmup.metrics || warmup);
                    const result = await phase(`${name}-measure`, /** 运行所选档位的正式测量窗口。 */ () => action(profile.seconds));
                    const row = { round, name, ...(result.metrics ? result : { metrics: result }) };
                    report.results.push(row); write(root, report);
                    // Continue other scenes to preserve diagnostic coverage; final verification fails closed.
                };
                await scenario('login', /** 固定账号集合登录一次，登录包含 Gate/Status 与 Chat 认证。 */ seconds => measure({
                    seconds, rate: accounts.length / seconds, maxPending: accounts.length,
                    action: /** 保持每账号单会话语义。 */ async index => { accounts[index].client?.close(); await env.login(accounts[index]); }
                }));
                const clients = accounts.map(/** 取正式认证客户端。 */ account => account.client);
                assert.ok(clients.every(/** 所有连接必须真正认证且仍存活。 */ client => client && !client.closed), 'missing-connections');
                const topology = await phase('relationships', /** 建立生产好友及群聊关系。 */ () => prepare(env, clients));
                await scenario('same-server', /** 测量同实例私聊。 */ seconds => privateMessages(env, topology.same, clients, profile, seconds));
                await scenario('cross-server', /** 测量跨实例私聊。 */ seconds => privateMessages(env, topology.cross, clients, profile, seconds));
                await scenario('group', /** 测量群提交和二十人成员补拉。 */ seconds => groupMessages(env, topology.groups, profile, seconds));
                await scenario('resources', /** 测量图片及附件传输。 */ seconds => resources(env, topology.cross, profile, seconds));
                await scenario('mixed', /** 同时测量跨服文本和资源传输的互相影响。 */ async seconds => {
                    const [messages, files] = await Promise.all([privateMessages(env, topology.cross, clients, profile, seconds),
                        resources(env, topology.cross, profile, seconds)]);
                    return { metrics: messages, resources: files };
                });
                // Offline seeding is intentionally outside the measured recovery action.
                const recovery = await phase('offline-prepare-and-measure', /** 测量每个会话的一百条离线消息恢复。 */ () => offline(env, topology.cross, profile.seconds));
                report.results.push({ round, name: 'offline', metrics: recovery }); write(root, report);
                for (const account of accounts) account.client.close();
            }
        };
        await Promise.race([execute(), interrupted]);
    } catch (error) {
        report.failures.push({ stage, category: /^[a-z][a-z0-9-]{0,70}$/.test(error.message) ? error.message : 'assertion-or-operation',
            location: error.stack?.split('\n')[1]?.trim().replaceAll(process.cwd(), '<repo>') });
    } finally {
        clearInterval(sampler);
        if (env) {
            try { report.cleanup = await phase('cleanup', /** 收集全部资源的最终清理证据。 */ () => env.teardown()); }
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
if (require.main === module) run().catch(/** 初始化失败必须使 CI 失败，不输出凭据。 */ () => { console.error('performance initialization failed'); process.exitCode = 1; });
module.exports = { run };
