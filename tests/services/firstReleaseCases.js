'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { randomUUID, createHash } = require('node:crypto');
const { poll, runCommand } = require('./dependencyCoordinator');

/** 严格限制真实首版验收只在显式开启的完整 3D 选择器执行。 */
function realAcceptanceEnabled(selector, value = process.env.CHAT_REAL_ACCEPTANCE) {
    assert.ok(value === undefined || value === '' || value === '0' || value === '1', 'invalid real acceptance flag');
    if (value !== '1') return false;
    assert.equal(selector, '3D', 'real acceptance requires full 3D selector');
    return true;
}

/** 执行四个独立真实客户端的群、资源和目录闭环；调用者负责最终进程及数据清理。 */
async function runFirstReleaseCases({ record, client, authenticate, retire, keepAlive, probeGroupSend, users, root, resourceUrl, sql }) {
    const actors = [];
    const identities = [];
    let chatId;
    let oldEpoch;
    let privateChat;
    let oldResource;
    const createUuid = randomUUID();
    const oldUuid = randomUUID();
    const textUuid = randomUUID();
    const offlineUuid = randomUUID();
    const resources = [];
    const command = /** 把控制通道结果归一为生产接口结果，不把控制完成当业务成功。 */ async (index, name, fields = {}) => {
        const response = await actors[index].instance.control.command(name, fields);
        return response.result ? { error: response.error, ...response.result } : response;
    };
    const success = /** 只有明确的业务成功码才能满足成功断言。 */ result => assert.equal(result.error, 0);
    const denied = /** 必须获得明确拒绝码，不能以超时或缺失结果替代权限拒绝。 */ result =>
        assert.ok(Number.isInteger(result.error) && result.error > 0);
    const resourceDenied = /** 独立真实 GET 的 403 证明授权拒绝，网络失败不能满足断言。 */ async (index, descriptor) => {
        const result = await command(index, 'resource-probe', { descriptor });
        success(result); assert.equal(result.httpStatus, 403);
    };
    const state = /** 读取客户端已落盘群状态。 */ index => command(index, 'group-state', { chatId });
    const info = /** 查询权威群版本及成员资料。 */ async index => {
        const result = await command(index, 'group-info', { chatId, uuid: randomUUID(), after: 0 });
        success(result); return result;
    };
    const manage = /** 按当前权威版本提交管理操作，可保留身份重试。 */ async (index, operation, params = {}, overrides = {}) => {
        const current = await info(index);
        return command(index, 'group-manage', { chatId, uuid: randomUUID(), version: current.group_revision,
            operation, params, ...overrides });
    };
    const messages = /** 读取实际消息模型而非直接制造客户端事实。 */ async index =>
        (await command(index, 'local-history', { chatId, before: '0' })).messages;
    const hasMessage = /** 有界等待消息出现在真实客户端模型并且没有重复项。 */ async (index, uuid) => {
        await info(index);
        await poll(/** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            await command(index, 'sync', { chatId });
            const rows = (await messages(index)).filter(/** 匹配指定消息身份。 */ row => row.uuid === uuid);
            return rows.length === 1 && BigInt(rows[0].messageId) > 0;
        }, 15000);
    };
    const test = /** 为首版专项保留独立连续 Test ID，并维护原双账号的控制存活期限。 */ async (number, name, action) => {
        await keepAlive();
        await record(`E03-RELEASE-${String(number).padStart(2, '0')}`, name, action);
        await keepAlive();
    };
    try {
        await test(1, 'four public accounts and accepted friendships', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            for (const logical of ['releasea', 'releaseb', 'releasec', 'released']) {
                const identity = randomUUID().replaceAll('-', '');
                const user = { logical, name: `${logical}_${identity}`, email:
                    `${identity}@example.invalid`,
                    password:
                    randomUUID().slice(0, 12) };
                users.push(user);
                const instance = await client(logical);
                actors.push({ user, instance });
                const observed = await authenticate(instance, user);
                assert.equal(observed.active, true);
                assert.ok(Number.isSafeInteger(user.uid) && user.uid > 0);
                assert.ok(Number.isSafeInteger(instance.pid) && instance.pid > 0);
                identities.push({ logical, pid: instance.pid, uid: user.uid, active: observed.active });
                await keepAlive();
            }
            assert.equal(new Set(identities.map(/** 提取真实客户端进程身份。 */ value => value.pid)).size, 4);
            assert.equal(new Set(identities.map(/** 提取公开注册产生的账号身份。 */ value => value.uid)).size, 4);
            for (let index = 1; index < 4; ++index) {
                success(await command(0, 'apply', { toUid: actors[index].user.uid, description: 'release acceptance', backname: 'peer' }));
                await poll(/** 轮询生产客户端的关系状态。 */ async () => (await command(index, 'snapshot', { otherUid: actors[0].user.uid })).applied, 10000);
                success(await command(index, 'accept', { toUid: actors[0].user.uid, description: 'accepted', backname: 'owner' }));
                await poll(/** 轮询生产客户端的关系状态。 */ async () => (await command(0, 'snapshot', { otherUid: actors[index].user.uid })).friend, 10000);
                await keepAlive();
            }
            privateChat = (await command(0, 'snapshot', { otherUid: actors[3].user.uid })).chatId;
            assert.ok(privateChat > 0);
        });
        await test(2, 'create group and persist original message', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const result = await command(0, 'group-create', { uuid: createUuid, name: 'release group',
                members: [actors[1].user.uid, actors[2].user.uid] });
            success(result); chatId = result.chat_id;
            assert.ok(chatId > 0);
            success(await command(0, 'group-send', { chatId, uuid: oldUuid, text: 'before joining' }));
            await hasMessage(1, oldUuid);
            const source = path.join(root, 'before-join.txt');
            fs.writeFileSync(source, 'resource before member joining');
            const uploaded = await command(0, 'upload', { path: source });
            success(uploaded); oldResource = { ...uploaded }; delete oldResource.error;
            success(await command(0, 'resource-send', { chatId, uuid: randomUUID(), descriptor: oldResource }));
        });
        await test(3, 'new membership excludes earlier server history', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            success(await manage(0, 'add', { members: [actors[3].user.uid] }));
            await info(3); oldEpoch = (await state(3)).membership_epoch;
            assert.ok(BigInt(oldEpoch) > 0);
            await command(3, 'sync', { chatId });
            success(await command(3, 'history', { chatId, cursor: '0' }));
            assert.ok(!(await messages(3)).some(/** 检查受入群边界保护的消息。 */ row => row.uuid === oldUuid));
            await resourceDenied(3, oldResource);
        });
        await test(4, 'ordinary members and stale revisions cannot mutate group', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const forbidden = await manage(1, 'rename', { name: 'forbidden' });
            denied(forbidden); assert.equal(forbidden.group_error, 'Forbidden');
            const previous = await info(0);
            success(await manage(0, 'rename', { name: 'release group' }));
            const conflict = await manage(0, 'rename', { name: 'stale' }, { version: previous.group_revision });
            denied(conflict); assert.equal(conflict.group_error, 'VersionConflict');
            assert.equal((await info(0)).group_name, 'release group');
        });
        await test(5, 'group message UUID replay preserves one durable row', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const fields = { chatId, uuid: textUuid, text: 'after joining searchable' };
            success(await command(0, 'group-send', fields));
            success(await command(0, 'group-send', fields));
            denied(await command(0, 'group-send', { ...fields, text: 'conflicting content' }));
            await hasMessage(3, textUuid);
            assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${textUuid}'`), '1');
        });
        await test(6, 'PNG ten-second video and file upload commit and verified download', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const png = path.join(root, 'release.png');
            await runCommand('ffmpeg', ['-v', 'error', '-f', 'lavfi', '-i', 'color=c=blue:s=32x32',
                '-frames:v', '1', '-y', png], { timeout: 10000 });
            const video = path.join(root, 'release.mp4');
            await runCommand('ffmpeg', ['-v', 'error', '-f', 'lavfi', '-i', 'color=c=blue:s=320x240:r=25',
                '-t', '10', '-c:v', 'mpeg4', '-y', video], { timeout: 30000 });
            await keepAlive();
            const file = path.join(root, 'release.bin'); fs.writeFileSync(file, Buffer.alloc(1300000, 79));
            for (const source of [png, video, file]) {
                await keepAlive();
                const uploaded = await command(0, 'upload', { path: source, resourceUrl });
                success(uploaded);
                const descriptor = { ...uploaded };
                delete descriptor.error;
                assert.ok(descriptor && descriptor.resource_id);
                const uuid = randomUUID();
                success(await command(0, 'resource-send', { chatId, uuid, descriptor }));
                success(await command(0, 'resource-send', { chatId, uuid, descriptor }));
                await hasMessage(3, uuid);
                await keepAlive();
                const target = path.join(root, `downloaded-${path.basename(source)}`);
                const downloaded = await command(3, 'download', { descriptor, path: target, resourceUrl });
                success(downloaded);
                const sha256 = createHash('sha256').update(fs.readFileSync(source)).digest('hex');
                assert.equal(downloaded.sha256, sha256);
                resources.push({ descriptor, uuid, sha256, source });
                if (source === png) await runCommand('ffmpeg', ['-v', 'error', '-xerror', '-i', downloaded.path,
                    '-f', 'null', '-'], { timeout: 10000 });
                if (source === video) {
                    const probe = JSON.parse(await runCommand('ffprobe', ['-v', 'error', '-count_frames', '-select_streams', 'v:0',
                        '-show_entries', 'stream=nb_read_frames', '-of', 'json', downloaded.path], { timeout: 30000 }));
                    assert.equal(Number(probe.streams[0].nb_read_frames), 250);
                }
            }
        });
        await test(7, 'offline member relogin restores text and all resource references', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            await command(2, 'logout');
            success(await command(0, 'group-send', { chatId, uuid: offlineUuid, text: 'offline group message' }));
            await authenticate(actors[2].instance, actors[2].user, false);
            await info(2); await hasMessage(2, offlineUuid);
            for (const item of resources) {
                await hasMessage(2, item.uuid);
                const downloaded = await command(2, 'download', { descriptor: item.descriptor,
                    path: path.join(root, `offline-${path.basename(item.source)}`), resourceUrl });
                success(downloaded); assert.equal(downloaded.sha256, item.sha256);
            }
        });
        await test(8, 'removed member loses server message and resource access', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            success(await manage(0, 'remove', { target_uid: actors[3].user.uid }));
            denied(await command(3, 'group-info', { chatId, uuid: randomUUID() }));
            await probeGroupSend(actors[3].instance, actors[3].user, chatId, oldEpoch);
            denied(await command(3, 'history', { chatId, cursor: '0' }));
            for (const item of resources) await resourceDenied(3, item.descriptor);
        });
        await test(9, 'independent private references restore each resource permission', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            for (const item of resources) {
                success(await command(0, 'resource-send', { chatId: privateChat, toUid: actors[3].user.uid,
                    uuid: randomUUID(), descriptor: item.descriptor }));
                const downloaded = await command(3, 'download', { descriptor: item.descriptor,
                    path: path.join(root, `private-${path.basename(item.source)}`), resourceUrl });
                success(downloaded); assert.equal(downloaded.sha256, item.sha256);
            }
        });
        await test(10, 'rejoin advances epoch and excludes absent-period history', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const absentUuid = randomUUID();
            success(await command(0, 'group-send', { chatId, uuid: absentUuid, text: 'while absent' }));
            success(await manage(0, 'add', { members: [actors[3].user.uid] }));
            await info(3);
            assert.notEqual((await state(3)).membership_epoch, oldEpoch);
            await probeGroupSend(actors[3].instance, actors[3].user, chatId, oldEpoch);
            await info(3);
            await command(3, 'sync', { chatId });
            success(await command(3, 'history', { chatId, cursor: '0' }));
            assert.ok(!(await messages(3)).some(/** 检查受入群边界保护的消息。 */ row => row.uuid === absentUuid));
        });
        await test(11, 'rename remark and local lookup reflect durable directory', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const current = await info(0);
            const rename = { chatId, uuid: randomUUID(), version: current.group_revision,
                operation: 'rename', params: { name: 'renamed release' } };
            const savedRemotely = await command(0, 'group-manage', { ...rename, localSaveFailure: true });
            success(savedRemotely); assert.equal(savedRemotely.local_save_failed, true);
            assert.equal((await info(1)).group_name, 'renamed release');
            success(await command(0, 'storage-unlock'));
            success(await command(0, 'group-manage', rename));
            assert.equal((await state(0)).name, 'renamed release');
            success(await command(0, 'remark', { uuid: randomUUID(), toUid: actors[1].user.uid, backname: 'release remark' }));
            const groups = await command(0, 'directory-find', { kind: 'conversations', text: 'renamed release' });
            assert.ok(groups.rows.some(/** 精确匹配群身份与重命名结果。 */ row => row.id === chatId && row.name === 'renamed release'));
            const contacts = await command(0, 'directory-find', { kind: 'contacts', text: 'release remark' });
            assert.ok(contacts.rows.some(/** 精确匹配本人联系人备注。 */ row => row.id === actors[1].user.uid && row.backname === 'release remark'));
            const search = await command(0, 'search', { chatId, text: 'after joining searchable', before: '0' });
            assert.ok(search.messages.some(/** 精确匹配搜索命中的消息身份。 */ row => row.uuid === textUuid));
        });
        await test(12, 'original creation replay survives changed name and membership', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const result = await command(0, 'group-create', { uuid: createUuid, name: 'release group',
                members: [actors[1].user.uid, actors[2].user.uid] });
            success(result); assert.equal(result.chat_id, chatId);
            denied(await command(0, 'group-create', { uuid: createUuid, name: 'different identity', members: [actors[1].user.uid] }));
        });
        await test(13, 'ownership transfer permits original owner leave', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            success(await manage(0, 'transfer', { target_uid: actors[1].user.uid }));
            assert.equal((await info(1)).owner_uid, actors[1].user.uid);
            denied(await manage(0, 'rename', { name: 'old owner forbidden' }));
            success(await manage(0, 'leave'));
            denied(await command(0, 'group-info', { chatId, uuid: randomUUID() }));
        });
        await test(14, 'dissolution rejects new access but preserves independent resource permissions', /** 执行当前业务步骤并用真实响应校验其合同。 */ async () => {
            const current = await info(1);
            const request = { chatId, uuid: randomUUID(), version: current.group_revision, operation: 'dissolve', params: {} };
            success(await command(1, 'group-manage', request));
            success(await command(1, 'group-manage', request));
            denied(await command(2, 'group-info', { chatId, uuid: randomUUID() }));
            denied(await command(2, 'history', { chatId, cursor: '0' }));
            for (const item of resources) {
                const downloaded = await command(3, 'download', { descriptor: item.descriptor,
                    path: path.join(root, `dissolved-${path.basename(item.source)}`), resourceUrl });
                success(downloaded); assert.equal(downloaded.sha256, item.sha256);
            }
        });
        return identities;
    } finally {
        for (const actor of actors.reverse()) await retire(actor.instance);
    }
}

module.exports = { realAcceptanceEnabled, runFirstReleaseCases };
