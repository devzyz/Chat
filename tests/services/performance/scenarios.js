'use strict';
const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');
const { performance } = require('node:perf_hooks');
const { setTimeout: delay } = require('node:timers/promises');
const { distribution, measure } = require('./metrics');
const text = 'p'.repeat(256);

/** 跟踪本场景的消息身份、真实确认和接收状态，不把重试通知计作新消息。 */
class Messages {
    /** 绑定本轮真实客户端通知，保存可审计的消息身份。 */
    constructor(clients) {
        this.entries = new Map(); this.ack = []; this.delivery = []; this.listeners = []; this.invalid = 0;
        for (const client of clients) {
            const listener = /** 处理生产消息通知，只观察当前场景的消息。 */ (id, value) => {
                if (id === 1018) for (const row of value.notify_msgs || []) this.observe(client.uid, row.msg_uuid, row.msg_content, row.message_id);
            };
            client.on('notification', listener); this.listeners.push([client, listener]);
        }
    }
    /** 记录权威消息内容与服务端身份；重复通知不增加吞吐。 */
    observe(uid, uuid, content, messageId) {
        const entry = this.entries.get(uuid);
        if (!entry || !entry.targets.has(uid)) return;
        if (content !== text || !Number.isSafeInteger(messageId) || messageId <= 0) { this.invalid++; return; }
        if (entry.observed.has(uid)) return;
        entry.observed.add(uid); entry.ids.add(messageId); this.delivery.push(performance.now() - entry.start);
        if (entry.observed.size === entry.targets.size) entry.resolve();
    }
    /** 提交文本并等待所有目标可见，发送和接收延迟独立记录。 */
    async send(sender, chatId, receivers, epoch, wait = true) {
        const uuid = randomUUID();
        let resolve;
        const visible = new Promise(/** 保存此消息的全部接收完成回调。 */ done => { resolve = done; });
        const entry = { targets: new Set(receivers.map(/** 取接收者身份。 */ client => client.uid)),
            observed: new Set(), ids: new Set(), start: performance.now(), resolve };
        this.entries.set(uuid, entry);
        const fields = { from_uid: sender.uid, to_uid: epoch ? 0 : receivers[0].uid, chat_id: chatId,
            text_array: [{ msg_uuid: uuid, msg_content: text }] };
        if (epoch) Object.assign(fields, { chat_type: 'group', membership_epoch: epoch });
        const response = await sender.request(1016, 1017, fields,
            /** 通过 UUID 关联提交结果。 */ value => value.uuid_msgId?.some(/** 匹配本条消息身份。 */ item => item.msg_uuid === uuid) ||
                (value.error !== 0 && value.client_msg_uuids?.includes(uuid)));
        const saved = response.uuid_msgId.find(/** 找到本条已持久化消息。 */ item => item.msg_uuid === uuid);
        assert.ok(Number.isSafeInteger(saved.message_id) && saved.message_id > 0);
        entry.id = saved.message_id; this.ack.push(performance.now() - entry.start);
        if (!wait) return uuid;
        let timer;
        try {
            await Promise.race([visible, new Promise(/** 建立接收截止期限。 */ (_, reject) => {
                timer = setTimeout(/** 期限到达仍未观察则保留丢失证据。 */ () => reject(new Error('delivery-timeout')), epoch ? 15000 : 10000);
            })]);
            assert.equal(entry.ids.size, 1); assert.ok(entry.ids.has(entry.id));
        } finally { clearTimeout(timer); }
        return uuid;
    }
    /** 核对真实数据库中的每个 UUID 恰好一行。 */
    async audit(sql) {
        const uuids = [...this.entries.keys()];
        for (let start = 0; start < uuids.length; start += 200) {
            const batch = uuids.slice(start, start + 200);
            const ids = batch.map(/** 只编码内部生成的 UUID，不接收外部 SQL。 */ uuid => `'${uuid}'`).join(',');
            const value = await sql.execute(`SELECT COUNT(*), COUNT(DISTINCT client_msg_uuid) FROM chat_message WHERE client_msg_uuid IN (${ids})`);
            assert.equal(value, `${batch.length}\t${batch.length}`, 'persistence-count');
        }
        assert.equal(this.invalid, 0, 'invalid-delivery');
    }
    /** 移除本场景的通知观察者。 */
    close() { for (const [client, listener] of this.listeners) client.off('notification', listener); }
}

/** 分页读取权威历史，要求游标严格前进且同一次补拉没有重复消息。 */
async function sync(client, chatId, cursor = 0, epoch, observe = null) {
    const rows = [], seen = new Set();
    for (let page = 0; page < 10000; page++) {
        const result = await client.correlated(1027, { mode: 'sync_v1', uid: client.uid, chat_id: chatId,
            after_id: cursor, ...(epoch ? { chat_type: 'group', membership_epoch: epoch } : {}) });
        assert.ok(Array.isArray(result.msgs), 'missing-history');
        let previousId = cursor;
        for (const row of result.msgs) {
            assert.ok(Number.isSafeInteger(row.message_id) && row.message_id > previousId, 'invalid-cursor');
            previousId = row.message_id;
            assert.ok(!seen.has(row.msg_uuid), 'duplicate-history'); seen.add(row.msg_uuid);
            rows.push(row); if (observe) observe(row);
        }
        assert.ok(Number.isSafeInteger(result.next_cursor) && result.next_cursor >= cursor, 'invalid-cursor');
        if (result.msgs.length) assert.equal(result.next_cursor, result.msgs.at(-1).message_id);
        if (!result.load_more) return { rows, cursor: result.next_cursor };
        assert.ok(result.next_cursor > cursor, 'stalled-cursor'); cursor = result.next_cursor;
    }
    throw new Error('history-page-limit');
}

/** 建立同服、跨服配对和每组二十人的群，所有关系通过生产接口生成。 */
async function prepare(env, clients) {
    const byPort = new Map();
    for (const client of clients) { if (!byPort.has(client.port)) byPort.set(client.port, []); byPort.get(client.port).push(client); }
    assert.equal(byPort.size, 2, 'two-live-instances');
    const [left, right] = [...byPort.values()];
    const known = new Map();
    const pair = /** 复用已建立好友的私聊身份，避免重复申请。 */ async (first, second) => {
        const key = [first.uid, second.uid].sort(/** 排序形成无方向好友身份。 */ (a, b) => a - b).join(':');
        if (!known.has(key)) known.set(key, await env.befriend(first, second));
        return { first, second, chatId: known.get(key) };
    };
    const same = [], cross = [], groups = [];
    for (const values of [left, right]) for (let index = 0; index + 1 < values.length; index += 2) same.push(await pair(values[index], values[index + 1]));
    for (let index = 0; index < Math.min(left.length, right.length); index++) cross.push(await pair(left[index], right[index]));
    assert.ok(same.length && cross.length);
    for (let start = 0; start < clients.length; start += 20) {
        const members = clients.slice(start, start + 20); assert.equal(members.length, 20);
        for (const member of members.slice(1)) await pair(members[0], member);
        const group = await members[0].correlated(1034, { name: `perf-${start}`, members: members.slice(1).map(/** 取邀请用户。 */ value => value.uid) });
        const epochs = [];
        for (const member of members) {
            const info = await member.correlated(1036, { chat_id: group.chat_id, after_uid: 0 });
            assert.match(info.membership_epoch, /^[1-9][0-9]*$/); epochs.push(info.membership_epoch);
        }
        groups.push({ members, chatId: group.chat_id, epochs });
    }
    return { same, cross, groups };
}

/** 测量私聊的提交、实际通知和数据库唯一性。 */
async function privateMessages(env, pairs, clients, profile, seconds) {
    const messages = new Messages(clients);
    try {
        const result = await measure({ seconds, rate: profile.rate, maxPending: clients.length, signal: env.signal,
            action: /** 将每次计划发送均匀分配给会话。 */ index => {
                const pair = pairs[index % pairs.length]; return messages.send(pair.first, pair.chatId, [pair.second]);
            } });
        result.ackMs = distribution(messages.ack); result.deliveryMs = distribution(messages.delivery);
        await auditResult(messages, env.sql, result); return result;
    } finally { messages.close(); }
}

/** 以生产两秒周期补拉群消息，分别记录提交确认与所有成员可见延迟。 */
async function groupMessages(env, groups, profile, seconds) {
    const messages = new Messages([]); let stopped = false; const errors = [];
    const pollers = groups.flatMap(/** 为每个非发送成员建立独立游标。 */ group => group.members.slice(1).map(
        /** 持续执行有界同步，组内写入和读取走真实服务。 */ async (client, index) => {
            let cursor = 0;
            try {
                while (!stopped) {
                    await delay(2000);
                    const value = await sync(client, group.chatId, cursor, group.epochs[index + 1],
                        /** 将权威历史行计入真实可见事件。 */ row => messages.observe(client.uid, row.msg_uuid, row.content, row.message_id));
                    cursor = value.cursor;
                }
            } catch { errors.push('group-poll-failed'); }
        }));
    try {
        const result = await measure({ seconds, rate: groups.length, maxPending: groups.length * 16, signal: env.signal,
            action: /** 每个二十人群每秒一条消息。 */ index => {
                const group = groups[index % groups.length];
                return messages.send(group.members[0], group.chatId, group.members.slice(1), group.epochs[0]);
            } });
        result.ackMs = distribution(messages.ack); result.deliveryMs = distribution(messages.delivery);
        stopped = true; await Promise.all(pollers); assert.equal(errors.length, 0, 'group-poll-failed');
        await auditResult(messages, env.sql, result); return result;
    } finally { stopped = true; await Promise.all(pollers); messages.close(); }
}

/** 对每个接收方先离线积压一百条，再经登录和分页恢复核对完整内容。 */
async function offline(env, pairs, seconds) {
    const messages = new Messages([]); const expected = new Map();
    for (const pair of pairs) {
        env.signal?.throwIfAborted();
        pair.second.close();
        const prior = await sync(pair.first, pair.chatId);
        const ids = [];
        for (let index = 0; index < 100; index++) {
            env.signal?.throwIfAborted(); ids.push(await messages.send(pair.first, pair.chatId, [pair.second], null, false));
        }
        expected.set(pair.chatId, { cursor: prior.cursor, ids });
    }
    const result = await measure({ seconds, rate: pairs.length / seconds, plannedCount: pairs.length, maxPending: pairs.length, signal: env.signal,
        action: /** 测量重新认证及一百条离线消息的完整补拉。 */ async index => {
            const pair = pairs[index];
            const account = env.accounts.find(/** 用稳定 UID 查找原账号。 */ value => value.uid === pair.second.uid);
            pair.second = await env.login(account);
            const fixture = expected.get(pair.chatId);
            const restored = await sync(pair.second, pair.chatId, fixture.cursor);
            assert.deepEqual(restored.rows.map(/** 提取补拉消息身份。 */ row => row.msg_uuid), fixture.ids);
            assert.ok(restored.rows.every(/** 核对消息内容没有损坏。 */ row => row.content === text));
        } });
    await auditResult(messages, env.sql, result); return result;
}

/** 使用固定有效 PNG 和确定性附件，不依赖外部文件或网络。 */
function resourceFixture(index) {
    if (index % 3 === 0) return { name: 'pixel.png', media: 'image/png',
        bytes: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=', 'base64') };
    return { name: 'payload.bin', media: 'application/octet-stream', bytes: Buffer.alloc(index % 3 === 1 ? 1048576 : 10485760, 79) };
}

/** 完整上传、提交聊天资源引用并由接收者下载校验，整个动作受六十秒期限约束。 */
async function transfer(env, pair, index) {
    const fixture = resourceFixture(index); const sha256 = createHash('sha256').update(fixture.bytes).digest('hex');
    const signal = env.signal ? AbortSignal.any([env.signal, AbortSignal.timeout(60000)]) : AbortSignal.timeout(60000);
    const request = /** 发送具备真实 Status 鉴权的资源 HTTP 请求。 */ async (client, route, method, body, extra = {}) => {
        const response = await fetch(`${env.resource}${route}`, { method, body, redirect: 'error', signal,
            headers: { 'X-User-Id': String(client.uid), Authorization: `Bearer ${client.token}`, ...extra } });
        if (!response.ok) throw new Error(`resource-http-${response.status}`); return response;
    };
    const upload = await (await request(pair.first, '/uploads', 'POST', JSON.stringify({ name: fixture.name,
        media_type: fixture.media, size: String(fixture.bytes.length), sha256 }), { 'Content-Type': 'application/json' })).json();
    for (let offset = 0; offset < fixture.bytes.length; offset += 65536) {
        const bytes = fixture.bytes.subarray(offset, offset + 65536);
        const progress = await (await request(pair.first, `/uploads/${upload.upload_id}`, 'PATCH', bytes,
            { 'Upload-Offset': String(offset), 'Content-Type': 'application/octet-stream' })).json();
        assert.equal(Number(progress.offset), offset + bytes.length);
    }
    const ready = await (await request(pair.first, `/uploads/${upload.upload_id}/complete`, 'POST', '')).json();
    assert.equal(ready.sha256, sha256);
    const uuid = randomUUID();
    await pair.first.request(1016, 1017, { from_uid: pair.first.uid, to_uid: pair.second.uid, chat_id: pair.chatId,
        resource_id: ready.resource_id, text_array: [{ msg_uuid: uuid, msg_content: '' }] },
    /** 关联资源消息的持久化确认。 */ response => response.uuid_msgId?.some(/** 匹配资源 UUID。 */ item => item.msg_uuid === uuid) || response.client_msg_uuids?.includes(uuid));
    const download = Buffer.from(await (await request(pair.second, `/resources/${ready.resource_id}`, 'GET')).arrayBuffer());
    assert.equal(download.length, fixture.bytes.length); assert.equal(createHash('sha256').update(download).digest('hex'), sha256);
    assert.equal(await env.sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${uuid}'`), '1');
    return download.length;
}

/** 有限并发与固定节奏测量完整资源传输，不向磁盘无限灌入数据。 */
async function resources(env, pairs, profile, seconds) {
    let bytes = 0;
    const result = await measure({ seconds, rate: profile.resourceConcurrency / 5, maxPending: profile.resourceConcurrency, signal: env.signal,
        action: /** 每个并发槽约五秒尝试一个不同大小资源。 */ async index => { bytes += await transfer(env, pairs[index % pairs.length], index); } });
    return { ...result, bytes, mibPerSecond: bytes / 1048576 / result.elapsedSeconds };
}
/** 审计失败保留原始吞吐与延迟，附加明确失败分类。 */
async function auditResult(messages, sql, result) {
    try { await messages.audit(sql); }
    catch { result.pass = false; result.errors['persistence-or-content-audit'] = 1; }
}
module.exports = { Messages, sync, prepare, privateMessages, groupMessages, offline, resources, resourceFixture };
