'use strict';

const assert = require('node:assert/strict');
const { spawn, execFileSync } = require('node:child_process');
const { randomBytes, randomUUID } = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { setTimeout: delay } = require('node:timers/promises');
const { createTopology, nativeConfig } = require('../../services/twoServerTopology');
const { Client, post } = require('../../services/performance/client');
const { measure, validate } = require('../../services/performance/metrics');
const { sync } = require('../../services/performance/scenarios');

// 在已认证的跨实例连接上按固定速率测量提交 ACK，再从接收端按游标逐条核对消息。
async function samplePrivateText(output, driver, sender, receiver, chat, cursor) {
    assert.ok(path.isAbsolute(output));
    const expected = new Map();
    // 每次提交使用新 UUID 并且只接受与请求匹配的回复。
    async function submit() {
        const uuid=randomUUID(), content='p'.repeat(256);
        const fields={from_uid:sender.uid,to_uid:receiver.uid,chat_id:chat,text_array:[{msg_uuid:uuid,msg_content:content}]};
        // 匹配此消息的 ACK，使同时在途的请求互不串用。
        const ack=await sender.request(1016,1017,fields,value=>value.uuid_msgId?.some(
            // 一个批次中按客户端业务身份定位条目。
            item=>item.msg_uuid===uuid));
        expected.set(uuid,ack.uuid_msgId[0].message_id);
    }
    const result=await measure({seconds:10,rate:20,maxPending:8,action:submit});
    const {rows}=await sync(receiver,chat,cursor);
    for (const row of rows) assert.equal(row.message_id,expected.get(row.msg_uuid));
    assert.equal(rows.length,expected.size);
    fs.mkdirSync(output,{recursive:true});
    fs.writeFileSync(path.join(output,driver+'-private-text.json'),JSON.stringify({
        driver,bytesPerMessage:256,targetPerSecond:20,verifiedMessages:rows.length,...result
    },null,2)+'\n');
    validate(result);
}

// 在同一场景分别运行两个数据库的正式服务程序；Redis、资源文件和用户均属于本次运行。
async function runWire(database, reserve, smtp) {
    const bin = process.env.CHAT_DB_SERVICE_BIN;
    if (!bin || !path.isAbsolute(bin)) throw new Error('AbsoluteServiceBinRequired');
    const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()),'chat-db-wire-'));
    const run = randomBytes(16).toString('hex');
    const password = randomBytes(32).toString('hex');
    const children = [], clients = [];
    const env = { ...process.env, CHAT_DB_PASSWORD: database.password, REDISCLI_AUTH: password };
    // 使用独立文件记录子进程诊断，避免管道阻塞；退出后统一回收。
    function start(file,args,extra = {}) {
        const output = fs.openSync(path.join(root,'child-' + children.length + '.log'),'w');
        try {
            const child = spawn(file,args,{ cwd: root, env:{ ...env,...extra }, windowsHide: true, stdio:['ignore',output,output] });
            const entry = { child, name:path.basename(file), ended: false };
            // 记录子进程完成，供清理路径等待。
            entry.closed = new Promise(
                // 连接启动失败或进程退出时结束等待。
                resolve => {
                // 启动失败也结束该子进程的等待。
                child.once('error',() => { entry.ended=true; resolve(); });
                // 记录退出并释放等待者。
                child.once('exit',() => { entry.ended=true; resolve(); });
            });
            children.push(entry);
        } finally { fs.closeSync(output); }
    }
    // 只重试就绪探测，注册和发送请求均显式断言。
    async function until(check, timeoutMs=30000, intervalMs=100) {
        const end = Date.now()+timeoutMs;
        // 存活探测只针对本次启动的程序。
        while (Date.now()<end && children.every(
                // 任一必需进程退出时提前结束探测。
                entry => !entry.ended)) {
            try { if (await check()) return; } catch { /* 服务仍在初始化。 */ }
            await delay(intervalMs);
        }
        const exited = children.filter(
            // 仅报告已退出的进程和退出码，不回显配置。
            entry => entry.ended).map(
            // 启动诊断区分驱动故障和进程装载失败。
            entry => entry.name + '=' + entry.child.exitCode);
        throw new Error('BackendWireReadinessFailed:' + (exited.join(',') || 'timeout'));
    }
    try {
        const ports = {};
        for (const key of ['gate','varify','chatA','rpcA','chatB','rpcB','redis','resource']) {
            do { ports[key] = await reserve(); }
            // 避免动态端口与已分配的服务冲突。
            while (Object.keys(ports).some(
                // 端口号在当前拓扑内必须唯一。
                other => other!==key && ports[key]===ports[other]) || ports[key]===database.port);
        }
        const { redis,resource,...appPorts } = ports;
        const topology = createTopology(run,appPorts);
        topology.database = database.name;
        const redisConfig = path.join(root,'redis.conf');
        fs.writeFileSync(redisConfig,'bind 127.0.0.1\nport '+redis+'\nsave ""\nappendonly no\nrequirepass '+password+'\n',
            { mode:0o600 });
        start(process.env.CHAT_REDIS_SERVER || 'redis-server',[path.basename(redisConfig)]);
        // 在夹具的 Redis 中执行有界命令。
        const redisCommand = (...args) => execFileSync(process.env.CHAT_REDIS_CLI || 'redis-cli',
            ['--raw','-h','127.0.0.1','-p',String(redis),...args],
            { env, encoding:'utf8', windowsHide:true, timeout:2000, stdio:['ignore','pipe','pipe'] }).trim();
        // 等待所属 Redis 接受带凭据的请求。
        await until(() => redisCommand('PING')==='PONG');
        const varify = path.join(root,'varify.json');
        fs.writeFileSync(varify,JSON.stringify({email:{host:'127.0.0.1',port:smtp,secure:false,auth:'none',deadlineMs:10000},
            redis:{host:'127.0.0.1',port:redis}}),{mode:0o600});
        start(process.execPath,[path.resolve(__dirname,'../../../VarifyServer/server.js'),'--config',varify],
            {CHAT_VARIFY_BIND_ADDRESS:'127.0.0.1:'+ports.varify,CHAT_VARIFY_EMAIL_USER:'fixture@example.invalid',
                CHAT_VARIFY_REDIS_PASSWORD:password});
        // 在发出验证码请求前等待真实 gRPC 端口可连接。
        async function verificationReady() {
            const socket = net.createConnection({host:'127.0.0.1',port:ports.varify});
            try {
                const { once } = require('node:events');
                await once(socket,'connect',{signal:AbortSignal.timeout(1500)});
                return true;
            } finally { socket.destroy(); }
        }
        await until(verificationReady);
        // 选择本平台的原生程序路径。
        const executable = name => process.platform === 'win32' ?
            path.join(bin,name,name+'.exe') : path.join(bin,name);
        for (const role of ['ChatA','ChatB','GateServer']) {
            const file = path.join(root,role+'.ini');
            fs.writeFileSync(file,nativeConfig(topology,role,{ password,redis,mysql:database.port,driver:database.driver,
                user:database.user },path.join(root,'logs')),{mode:0o600});
            start(executable(role==='GateServer'?role:'ChatServer'),['--config',file]);
        }
        const file = path.join(root,'resource.ini');
        fs.writeFileSync(file,'[ResourceServer]\nHost=127.0.0.1\nPort='+resource+'\nStorageRoot=files\n'+
            '[Database]\nDriver='+database.driver+'\nHost=127.0.0.1\nPort='+database.port+'\nUser='+database.user+
            '\nSchema='+database.name+'\n[Redis]\nHost=127.0.0.1\nPort='+redis+'\nPassword='+password+'\n',
            { mode:0o600 });
        start(executable('ResourceServer'),['--config',file]);
        const base='http://127.0.0.1:'+ports.gate;
        // 等待网关、资源和两个实例全部就绪。
        await until(async () => (await fetch(base+'/get_test',{signal:AbortSignal.timeout(1500)})).ok &&
            // 要求两个实例均发布租约。
            topology.servers.every(server => redisCommand('EXISTS','chatlease_'+server.name)==='1') &&
            (await fetch('http://127.0.0.1:'+resource+'/ready',{signal:AbortSignal.timeout(1500)})).ok);
        // 通过真实验证码服务和所属 SMTP 对端取码，注册、选服和 TCP 登录均经过正式协议。
        for (let index=0;index<2;++index) {
            const email=run+'-'+index+'@example.invalid', passwd=randomUUID();
            await post(base+'/get_varifycode',{email});
            const code=redisCommand('GET','code_'+email);
            assert.ok(code);
            await post(base+'/user_register',{user:'wire'+index,email,passwd,confirm:passwd,varifycode:code});
            const login=await post(base+'/user_login',{email,password:passwd});
            const client=new Client(Number(login.port),login.uid,login.token);
            clients.push(client);
            await client.connect();
            // 通过客户端连接端口定位真实实例。
            const instance=topology.servers.find(server => server.port===client.port);
            assert.ok(instance);
            if (index === 0) {
                // Chat 每 60 秒发布计数，第二次登录前等待一次完整的正常发布周期。
                await until(() => Number(redisCommand('HGET','logincount',instance.name))>=1,75000,500);
            }
        }
        const [first,second]=clients;
        assert.notEqual(first.port,second.port);
        await first.request(1009,1010,{fromuid:first.uid,touid:second.uid,applyname:'sender',applydescription:'hello',
            applyicon:'',applysex:0,description:'hello',backname:'peer'});
        const accepted=await second.request(1013,1014,{applyuid:first.uid,authuid:second.uid,
            applyinfo:{applyuid:first.uid,touid:second.uid,backname:'peer',description:'hello'},
            authinfo:{authuid:second.uid,touid:first.uid,backname:'peer',description:'reply'}});
        assert.ok(accepted.chatid>0);
        const uuid=randomUUID();
        const message={from_uid:first.uid,to_uid:second.uid,chat_id:accepted.chatid,text_array:[{msg_uuid:uuid,msg_content:'wire'}]};
        // 按消息 UUID 将响应关联到本次发送。
        const send=() => first.request(1016,1017,message,
            // 仅接受包含这次发送 UUID 的确认。
            item=>item.uuid_msgId?.some(
                // 批次中的记录使用消息身份关联。
                entry=>entry.msg_uuid===uuid));
        const original=await send();
        assert.deepEqual((await send()).uuid_msgId,original.uuid_msgId);
        const page=await second.correlated(1027,{mode:'sync_v1',uid:second.uid,chat_id:accepted.chatid,after_id:0});
        // 要求同步中只出现一次原消息。
        assert.equal(page.msgs.filter(
            // 按服务端 ID 和客户端 UUID 同时核验同步结果。
            row=>row.msg_uuid===uuid && row.message_id===original.uuid_msgId[0].message_id).length,1);
        if (process.env.CHAT_DB_SAMPLE_DIR) {
            await samplePrivateText(process.env.CHAT_DB_SAMPLE_DIR,database.driver,first,second,
                accepted.chatid,original.uuid_msgId[0].message_id);
        }
    } catch (error) {
        if (process.env.CHAT_DB_WIRE_DIAGNOSTICS) {
            const output=path.join(process.env.CHAT_DB_WIRE_DIAGNOSTICS,database.driver);
            fs.mkdirSync(output,{recursive:true});
            for (const name of fs.readdirSync(root)) {
                if (/^child-\d+\.log$/.test(name)) fs.copyFileSync(path.join(root,name),path.join(output,name));
            }
        }
        throw error;
    } finally {
        for (const client of clients) client.close();
        for (const entry of children.reverse()) {
            if (!entry.ended) entry.child.kill();
            await Promise.race([entry.closed,delay(3000,undefined,{ref:false})]);
            if (!entry.ended) { entry.child.kill('SIGKILL'); await entry.closed; }
        }
        if (path.dirname(fs.realpathSync(root))!==fs.realpathSync(os.tmpdir())) throw new Error('UnsafeFixturePath');
        fs.rmSync(root,{recursive:true,force:true});
    }
}

// 每个后端使用独立的回环邮件对端，清理后再返回。
async function backendWire(database,reserve) {
    const { withPeer } = require('../../../VarifyServer/test/smtp/smtpFaults');
    // 端口只提供给本次启动的验证码服务。
    await withPeer('accept',port=>runWire(database,reserve,port));
}

module.exports = { backendWire };
