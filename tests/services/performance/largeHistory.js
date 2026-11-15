'use strict';

const assert = require('node:assert/strict');
const { measure } = require('./metrics');

const historyScenarios = ['history-middle', 'history-tail', 'history-cross-write'];
const windowRows = 100;
const middleWindows = 32;
const batchSize = 2500;

// 为一个会话分配可重建的合法 UUID 前缀，后十二位为从一开始的行号。
function uuidPrefix(slot) {
    return `${slot.toString(16).padStart(8, '0')}-0000-4000-8000-`;
}

// 通过公开账号、登录和好友接口建立跨服私聊，数据装载单独计时。
async function historyPairs(env, profile) {
    const accounts = [];
    for (let index = 0; index < profile.connections; index++) {
        const account = await env.register(index);
        await env.login(account);
        accounts.push(account);
    }
    await env.balance(accounts);
    // 按真实 TCP 端点分组，确保每个写入都经过两个独立实例。
    const [left, right] = env.topology.servers.map(server =>
        accounts.filter(account => account.client.port === server.port));
    assert.equal(left.length, profile.dataset.longChats + profile.dataset.ordinaryChats);
    assert.equal(right.length, left.length);
    const pairs = [];
    for (let slot = 0; slot < left.length; slot++) {
        const first = left[slot].client, second = right[slot].client;
        const chatId = await env.befriend(first, second);
        for (const id of [first.uid, second.uid, chatId]) assert.ok(Number.isSafeInteger(id) && id > 0);
        pairs.push({ first, second, chatId, slot, prefix: uuidPrefix(slot),
            size: slot < profile.dataset.longChats ? profile.dataset.longRows : profile.dataset.ordinaryRows });
    }
    return pairs;
}

// 向本次隔离数据库分批装载，在会话之间交错写入，核实实际行数并缓存权威游标。
async function seedHistory(env, pairs, dataset) {
    await env.sql.execute('CREATE TEMPORARY TABLE perf_sequence (n INT PRIMARY KEY)');
    // 此临时数表复用于所有批次，生成有界的单条 INSERT SELECT。
    const sequence = Array.from({ length: batchSize }, (_, index) => `(${index + 1})`);
    await env.sql.execute(`INSERT INTO perf_sequence VALUES ${sequence.join(',')}`);
    for (let base = 0; base < Math.max(dataset.longRows, dataset.ordinaryRows); base += batchSize) {
        for (const pair of pairs) {
            if (base >= pair.size) continue;
            env.signal.throwIfAborted();
            await env.sql.execute(`INSERT INTO chat_message (chat_id,send_id,recv_id,content,status,client_msg_uuid)
                SELECT ${pair.chatId},${pair.first.uid},${pair.second.uid},
                    RPAD(CONCAT('history-',${pair.slot},'-',${base}+n,'/'),${dataset.contentBytes},'x'),0,
                    CONCAT('${pair.prefix}',LPAD(${base}+n,12,'0'))
                FROM perf_sequence WHERE n <= ${Math.min(batchSize, pair.size - base)} ORDER BY n`);
        }
    }
    const rowsPerChat = [];
    for (const pair of pairs) {
        const sender = `send_id=${pair.first.uid}`;
        const actual = Number(await env.sql.execute(`SELECT COUNT(*) FROM chat_message
            WHERE ${sender} AND chat_id=${pair.chatId} AND client_msg_uuid LIKE '${pair.prefix}%'`));
        assert.equal(actual, pair.size, 'dataset-chat-count');
        rowsPerChat.push(actual);
        // 从会话的中间区段选取确定的多个起点，而非反复读取同一页。
        pair.middle = Array.from({ length: middleWindows }, (_, index) =>
            Math.floor(pair.size * (0.25 + 0.5 * index / middleWindows)));
        pair.tail = pair.size - windowRows;
        // 只在准备阶段按 UUID 查询真实主键，测量阶段使用生产游标。
        const keys = [...pair.middle, pair.tail].map(n => `'${pair.prefix}${String(n).padStart(12, '0')}'`);
        const anchors = await env.sql.execute(`SELECT RIGHT(client_msg_uuid,12),message_id FROM chat_message
            WHERE ${sender} AND client_msg_uuid IN (${keys.join(',')})`);
        // 将每个行号关联到数据库实际分配的主键。
        pair.cursors = new Map(anchors.split('\n').map(line => line.split('\t').map(Number)));
        for (const n of [...pair.middle, pair.tail]) assert.ok(pair.cursors.get(n) > 0, 'missing-history-anchor');
    }
    return { rowsPerChat, totalRows: rowsPerChat.reduce((total, count) => total + count, 0) };
}

// 测量从指定深度连续补拉一百条，逐条核对参与者、UUID、载荷和游标。
async function readWindow(pair, ordinal, contentBytes) {
    let cursor = pair.cursors.get(ordinal);
    const end = ordinal + windowRows;
    while (ordinal < end) {
        const page = await pair.second.correlated(1027, { mode: 'sync_v1', uid: pair.second.uid,
            chat_id: pair.chatId, after_id: cursor });
        assert.ok(Array.isArray(page.msgs) && page.msgs.length > 0 && page.msgs.length <= 50, 'history-page-size');
        for (const row of page.msgs) {
            assert.ok(Number.isSafeInteger(row.message_id) && row.message_id > cursor, 'history-order');
            cursor = row.message_id;
            ordinal++;
            assert.equal(row.send_id, pair.first.uid, 'history-sender');
            assert.equal(row.recv_id, pair.second.uid, 'history-recipient');
            assert.equal(row.msg_uuid, pair.prefix + String(ordinal).padStart(12, '0'), 'history-identity');
            assert.equal(row.content, `history-${pair.slot}-${ordinal}/`.padEnd(contentBytes, 'x'), 'history-content');
        }
        assert.equal(page.next_cursor, cursor, 'history-cursor');
        assert.ok(ordinal === end || (ordinal < end && page.load_more === true), 'history-incomplete');
    }
}

// 轮转读取全部会话的中段或尾部，用吞吐和延迟描述一次完整补拉。
function historyReads(env, pairs, profile, seconds, tail) {
    return measure({ seconds, rate: profile.rate, maxPending: pairs.length, signal: env.signal,
        // 将计划位置分派到不同会话和抽样窗口。
        action: index => {
            const pair = pairs[index % pairs.length];
            const ordinal = tail ? pair.tail : pair.middle[Math.floor(index / pairs.length) % middleWindows];
            return readWindow(pair, ordinal, profile.dataset.contentBytes);
        } });
}

// 报告必须包含指定分布中每个会话的真实数据量。
function verifyDataset(evidence, dataset) {
    const expected = [...Array(dataset.longChats).fill(dataset.longRows),
        ...Array(dataset.ordinaryChats).fill(dataset.ordinaryRows)];
    assert.deepEqual(evidence?.rowsPerChat, expected, 'dataset-shape');
    assert.equal(evidence.totalRows, dataset.longChats * dataset.longRows + dataset.ordinaryChats * dataset.ordinaryRows,
        'dataset-total');
}

module.exports = { historyScenarios, historyPairs, seedHistory, readWindow, historyReads, verifyDataset };
