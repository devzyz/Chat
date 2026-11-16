'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const { randomUUID } = require('node:crypto');
const { measure, validate } = require('./metrics');
const { prepare, privateMessages, groupMessages, resources, transfer, prepareOffline, recoverOffline,
    plannedResourceBytes } = require('./scenarios');
const { seedHistory, historyReads } = require('./largeHistory');

// 每种速率均对应一个完整业务动作，恢复按实际离线账号逐一计数。
function rates(profile) {
    return {
        registration: 2, login: profile.loginRate, 'same-server': profile.rate, 'cross-server': profile.rate,
        'hot-private': profile.peakRate, 'history-middle': profile.historyRate, 'history-tail': profile.historyRate,
        'social-directory': profile.directoryRate, 'receipt-retry': profile.receiptRate, group: profile.connections / 20,
        'group-management': 4, avatar: 4, resources: profile.resourceConcurrency / 5, mixed: profile.rate, offline: null
    };
}

// 校验矩阵中每次预热和正式窗口的目标样本数，以及复合场景的独立指标。
function verifyWindow(profile, name, value, seconds, videoBytes) {
    const metrics = value.metrics || value;
    const rate = rates(profile)[name];
    assert.notEqual(rate, undefined);
    const planned = rate === null ? profile.connections / 2 : Math.ceil(seconds * rate);
    validate(metrics);
    assert.equal(metrics.planned, planned, 'comprehensive-planned');
    if (['same-server', 'cross-server', 'hot-private', 'mixed', 'group'].includes(name)) {
        assert.equal(metrics.ackMs?.count, planned, 'missing-acks');
        assert.equal(metrics.deliveryMs?.count, planned * (name === 'group' ? 19 : 1), 'missing-deliveries');
    }
    if (name === 'resources' || name === 'mixed') {
        const files = name === 'mixed' ? value.resources : metrics;
        validate(files);
        assert.equal(files.planned, Math.ceil(seconds * profile.resourceConcurrency / 5), 'resource-planned');
        assert.ok(Number.isSafeInteger(videoBytes) && videoBytes > 65536, 'missing-video-size');
        assert.equal(files.plannedBytes, plannedResourceBytes(files.planned, videoBytes), 'resource-fixture-size');
        assert.equal(files.bytes, files.plannedBytes, 'resource-byte-count');
        assert.equal(files.mibPerSecond, files.bytes / 1048576 / files.elapsedSeconds, 'resource-throughput');
    }
}

// 查完整好友目录并向真实服务器读取对端公开资料。
async function directory(pair) {
    const info = await pair.first.correlated(1046, { kind: 'profile', target_uid: pair.second.uid });
    assert.equal(info.profile.uid, pair.second.uid, 'profile-uid');
    let after = '0';
    const contacts = new Set();
    for (;;) {
        const page = await pair.first.correlated(1046, { kind: 'contacts', after });
        for (const item of page.items) {
            assert.ok(!contacts.has(item.uid), 'duplicate-contact');
            contacts.add(item.uid);
        }
        if (!page.load_more) break;
        assert.ok(BigInt(page.next) > BigInt(after) && contacts.size <= 240, 'directory-cursor');
        after = page.next;
    }
    assert.ok(contacts.has(pair.second.uid), 'missing-friend');
}

// 重放同一消息后上报送达和已读，再从发送者会话按 revision 查询权威结果。
async function receiptRetry(pair) {
    const uuid = randomUUID();
    const body = { chat_id: pair.chatId, from_uid: pair.first.uid, to_uid: pair.second.uid,
        text_array: [{ msg_uuid: uuid, msg_content: 'receipt-retry' }] };
    // 无关联的通知不会满足此请求。
    const send = () => pair.first.request(1016, 1017, body,
        // 错误响应同样按本次 UUID 关联。
        reply => reply.uuid_msgId?.some(
            // 在成功确认列表中查找原 UUID。
            item => item.msg_uuid === uuid) || reply.client_msg_uuids?.includes(uuid));
    const saved = (await send()).uuid_msgId;
    assert.deepEqual((await send()).uuid_msgId, saved, 'replay-identity');
    const messageId = saved[0].message_id;
    assert.ok(Number.isSafeInteger(messageId) && messageId > 0);
    // 同一条消息由接收者逐级确认。
    const report = level => pair.second.correlated(1029, { version: 1, chat_id: pair.chatId,
        items: [{ message_id: messageId, level }] });
    const delivered = await report('delivered');
    assert.equal(delivered.items[0].level, 'delivered');
    const read = await report('read');
    assert.equal(read.items[0].level, 'read');
    assert.equal(read.items[0].recipient_uid, pair.second.uid);
    const change = await pair.first.correlated(1032, { version: 1, chat_id: pair.chatId,
        after_revision: delivered.latest_revision });
    // 确认发送端补拉到了同一条消息的最新已读版本。
    assert.ok(change.items.some(item => item.message_id === messageId && item.level === 'read' &&
        item.revision === read.items[0].revision), 'receipt-sync');
}

// 建立临时群、改名、由成员读取并解散，检查管理版本连续推进。
async function groupManagement(pair) {
    const created = await pair.first.correlated(1034, { name: 'load-group', members: [pair.second.uid] });
    const chatId = created.chat_id;
    const renamed = await pair.first.correlated(1038, { chat_id: chatId, operation: 'rename',
        expected_revision: '1', name: 'renamed-load-group' });
    assert.equal(renamed.group_revision, '2');
    const info = await pair.second.correlated(1036, { chat_id: chatId, after_uid: 0 });
    assert.equal(info.member_count, 2);
    assert.equal(info.group_name, 'renamed-load-group');
    const removed = await pair.first.correlated(1038, { chat_id: chatId, operation: 'dissolve', expected_revision: '2' });
    assert.equal(removed.group_state, 'dissolved');
    assert.equal(removed.group_revision, '3');
}

// 固定一套生产拓扑和历史库，所有独立窗口完成后再切断接收端测量恢复。
async function comprehensive(env, profile, report, phase, repeat) {
    const accounts = [], loginAccounts = [];
    const video = fs.readFileSync(process.env.CHAT_PERFORMANCE_VIDEO);
    const avatar = fs.readFileSync(process.env.CHAT_PERFORMANCE_AVATAR);
    assert.ok(video.length > 65536 && avatar.length > 0, 'missing-media-fixture');
    report.videoBytes = video.length;
    // 先完成注册、认证与实例均衡，再对固定在线用户施加业务负载。
    await phase('accounts', async () => {
        for (let index = 0; index < profile.connections; index++) {
            const account = await env.register(index);
            await env.login(account);
            accounts.push(account);
        }
        await env.balance(accounts);
    });
    // 保持这些连接在线，登录压测采用独立账号集。
    const clients = accounts.map(account => account.client);
    // 通过实际好友和群管理接口准备各场景共用的关系。
    const topology = await phase('relationships', () => prepare(env, clients));
    const pairs = topology.cross;
    // 在测量窗口之外预装历史并记录实存行数。
    report.dataset = await phase('seed-history', () => seedHistory(env, pairs, profile.dataset));
    const loads = rates(profile);
    // 以场景规定的开环速率执行按账号分散的业务动作。
    const operations = (name, action) => repeat(name,
        // 按本次窗口时长计算计划动作数，在固定节奏下执行。
        seconds => measure({ seconds, rate: loads[name],
        maxPending: profile.connections, signal: env.signal, action }));
    let registration = profile.connections;
    // 为每次注册使用独立地址，并保留成功账号供登录测量。
    await operations('registration', async () => { loginAccounts.push(await env.register(registration++)); });
    // 完成 Gate 选服和 TCP 认证后主动关闭本次连接。
    await operations('login', async index => {
        const account = loginAccounts[index % loginAccounts.length];
        const client = await env.login(account);
        client.close();
    });
    // 在每个实例内部轮转发送，核对本地在线送达。
    await repeat('same-server', seconds => privateMessages(env, topology.same, clients, profile, seconds));
    // 使用跨实例配对，等待远端接收者观察到消息。
    await repeat('cross-server', seconds => privateMessages(env, pairs, clients, profile, seconds));
    // 将峰值写入集中到指定数量的长会话。
    await repeat('hot-private', seconds => privateMessages(env, pairs.slice(0, profile.hotPairs), clients,
        { ...profile, rate: profile.peakRate }, seconds));
    const reads = { ...profile, rate: profile.historyRate };
    // 从轮换的中段游标发起连续分页读取。
    await repeat('history-middle', seconds => historyReads(env, pairs, reads, seconds, false));
    // 从预装历史的尾部读取固定数量的消息。
    await repeat('history-tail', seconds => historyReads(env, pairs, reads, seconds, true));
    // 轮换查询联系人资料并核对完整好友目录。
    await operations('social-directory', index => directory(pairs[index % pairs.length]));
    // 向同一会话重试原 UUID 并在发送端核实已读回执。
    await operations('receipt-retry', index => receiptRetry(pairs[index % pairs.length]));
    // 在每个群提交消息，等待其余成员全部补拉完成。
    await repeat('group', seconds => groupMessages(env, topology.groups, profile, seconds));
    // 在配对好友间完成建群、改名、成员查询和解散。
    await operations('group-management', index => groupManagement(pairs[index % pairs.length]));
    // 轮换发布头像，并由另一账号读取及校验。
    await operations('avatar', index => transfer(env, pairs[index % pairs.length], index, undefined, avatar));
    // 轮转传输图片、文件和视频，核对续传和下载内容。
    await repeat('resources', seconds => resources(env, pairs, profile, seconds, video));
    // 同时运行跨服文本与文件负载，分别保留完成证据。
    await repeat('mixed', async seconds => {
        const outcomes = await Promise.allSettled([privateMessages(env, pairs, clients, profile, seconds),
            resources(env, pairs, profile, seconds, video)]);
        for (const item of outcomes) if (item.status === 'rejected') throw item.reason;
        return { metrics: outcomes[0].value, resources: outcomes[1].value };
    });
    // 使用当前窗口准备的积压验证重登与消息恢复。
    await repeat('offline', (seconds, fixture) => recoverOffline(env, pairs, seconds, fixture),
        // 每次断线重新生成积压，SQL 只用于准备起点与测量后的持久化核查。
        () => prepareOffline(env, pairs, true));
}

module.exports = { comprehensive, rates, verifyWindow };
