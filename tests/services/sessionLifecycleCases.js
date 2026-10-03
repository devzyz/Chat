'use strict';
const assert = require('node:assert/strict');
const { randomUUID } = require('node:crypto');
const { Client } = require('./performance/client');

/** 在真实服务组合中核对用途隔离、改密撤销、迟到登录栅栏、活跃续期和条件退出；凭据只留在内存。 */
async function verifySessionLifecycle({ coordinator, http, rpcReady, user, port }) {
    const redis = await coordinator.redis();
    const peers = [];
    /** 建立本用例拥有的真实聊天连接，并在失败时也纳入统一清理。 */
    async function connect(auth) {
        const peer = new Client(port, auth.uid, auth.token, 4000); peers.push(peer);
        await peer.connect(); return peer;
    }
    try {
        const original = { uid: user.uid, token:
            user.token };
        const active = await connect(original);
        await redis.expire(String(user.uid), 2);
        await active.request(1020, 1021, { uid: user.uid });
        assert.ok(await redis.ttl(String(user.uid)) > 80000, 'authenticated-heartbeat-renews-token');
        const version = await redis.get(`auth_version_${user.uid}`) || '0';
        assert.equal((await http('/get_varifycode', { email:
            user.email, purpose: 'register' })).error, 0);
        assert.equal((await http('/get_varifycode', { email:
            user.email, purpose: 'reset_password' })).error, 0);
        const registrationCode = await redis.get(`code_${user.email}`);
        const resetCode = await redis.get(`code_reset_${user.email}`);
        assert.ok(registrationCode && resetCode && registrationCode !== resetCode, 'separate-purpose-codes');
        const replacement = randomUUID();
        const reset = { user: 'four_1', email:
            user.email, password:
            replacement, varify: registrationCode };
        assert.equal((await http('/reset_pwd', reset)).error, 1004);
        reset.varify = resetCode;
        assert.equal((await http('/reset_pwd', reset)).error, 0);
        assert.equal(await redis.exists(`code_reset_${user.email}`), 0);
        assert.equal(await redis.exists(`code_${user.email}`), 1);
        assert.notEqual((await rpcReady('StatusService', 'Login', original)).error, 0);
        await assert.rejects(active.request(1020, 1021, { uid: user.uid }), /closed|replaced|write-failed/);
        assert.notEqual((await rpcReady('StatusService', 'GetChatServer', { uid: user.uid, authVersion: version })).error, 0);
        assert.notEqual((await http('/user_login', { email:
            user.email, password:
            user.password })).error, 0);
        user.password =
            replacement;
        const login = await http('/user_login', { email:
            user.email, password:
            user.password });
        assert.equal(login.error, 0);
        const current = await connect(login);
        assert.equal((await http('/logout', original)).error, 1011);
        assert.equal((await rpcReady('StatusService', 'Login', login)).error, 0);
        assert.equal((await http('/logout', { uid: user.uid, token:
            login.token })).error, 0);
        assert.equal((await http('/logout', { uid: user.uid, token:
            login.token })).error, 0);
        assert.notEqual((await rpcReady('StatusService', 'Login', login)).error, 0);
        await assert.rejects(current.request(1020, 1021, { uid: user.uid }), /closed|replaced|write-failed/);
        const resumed = await http('/user_login', { email:
            user.email, password:
            user.password });
        assert.equal(resumed.error, 0); user.token =
            resumed.token;
    } finally {
        for (const peer of peers) peer.close();
        redis.disconnect();
    }
}
module.exports = { verifySessionLifecycle };
