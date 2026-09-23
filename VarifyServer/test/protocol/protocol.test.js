'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const repositoryRoot = path.resolve(__dirname, '..', '..', '..');

test('shared verification constants keep their wire-visible values', /** 验证验证码键前缀及业务错误码的公开取值稳定。 */ () => {
    const { code_prefix, Errors } = require('../../const');

    assert.equal(code_prefix, 'code_');
    assert.deepEqual(Errors, { Success: 0, RedisErr: 1, Exception: 2 });
});

test('loaded protobuf exposes the verification RPC request and response types', /** 验证生成的验证码服务、方法路径及请求响应类型存在。 */ () => {
    const messageProto = require('../../proto');

    assert.equal(typeof messageProto.VarifyService, 'function');
    assert.equal(typeof messageProto.VarifyService.service.GetVarifyCode, 'object');
    assert.equal(messageProto.VarifyService.service.GetVarifyCode.path, '/message.VarifyService/GetVarifyCode');
    assert.equal(typeof messageProto.GetVarifyReq, 'object');
    assert.equal(typeof messageProto.GetVarifyRsp, 'object');
});

// V02-PROTO-03
test('repository canonical proto module is the sole editable wire authority', /** 验证协议源集中在 proto 目录且没有遗留可编辑副本。 */ () => {
    const canonicalFiles = ['varify.proto', 'status.proto', 'chat.proto'];
    for (const file of canonicalFiles) {
        assert.equal(
            fs.existsSync(path.join(repositoryRoot, 'proto', file)),
            true,
            `missing canonical proto/${file}`
        );
    }

    const legacyEditableCopies = [
        path.join('GateServer', 'GateServer', 'message.proto'),
        path.join('StatusServer', 'StatusServer', 'message.proto'),
        path.join('ChatServer', 'ChatServer', 'message.proto'),
        path.join('VarifyServer', 'message.proto')
    ];
    assert.deepEqual(
        legacyEditableCopies.filter(/** 筛出仍存在的旧协议副本路径。 */ (relative) => fs.existsSync(path.join(repositoryRoot, relative))),
        [],
        'service-local message.proto files must not remain editable authorities'
    );
});

// V02-PROTO-04
test('protocol compatibility command accepts the canonical contract and generated consumers', /** 验证独立协议合同检查命令成功并输出完成标记。 */ () => {
    const command = spawnSync(
        process.execPath,
        [path.join(repositoryRoot, 'scripts', 'protocol-compatibility.js'), 'check-contract'],
        { cwd: repositoryRoot, encoding: 'utf8', timeout: 15000 }
    );

    assert.equal(command.status, 0, `${command.stdout}\n${command.stderr}`);
    assert.match(command.stdout, /Protocol contract and generated consumer registration checks passed/);
});

// V02-PROTO-05
test('compatibility command rejects isolated descriptor drift and unregistered authorities', /** 通过临时协议变异验证不兼容更改被拒绝。 */ (t) => {
    const temporaryRoot = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'chat-proto-mutation-'));
    t.after(/** 清理本用例创建的临时协议目录。 */ () => fs.rmSync(temporaryRoot, { recursive: true, force: true }));

    for (const file of ['varify.proto', 'status.proto', 'chat.proto']) {
        fs.copyFileSync(
            path.join(repositoryRoot, 'proto', file),
            path.join(temporaryRoot, file)
        );
    }

    const runCompatibility = /** 对临时协议副本执行有界兼容性检查并返回子进程结果。 */ () => spawnSync(
        process.execPath,
        [
            path.join(repositoryRoot, 'scripts', 'protocol-compatibility.js'),
            'check-compatibility',
            temporaryRoot
        ],
        { cwd: repositoryRoot, encoding: 'utf8', timeout: 15000 }
    );

    const varifyPath = path.join(temporaryRoot, 'varify.proto');
    const originalVarify = fs.readFileSync(varifyPath, 'utf8');
    fs.writeFileSync(varifyPath, originalVarify.replace('string email = 1;', 'string email = 9;'));
    const fieldDrift = runCompatibility();
    assert.notEqual(fieldDrift.status, 0);
    assert.match(`${fieldDrift.stdout}\n${fieldDrift.stderr}`, /field number|reserve number/i);

    fs.writeFileSync(varifyPath, originalVarify.replace('GetVarifyCode', 'GetVerificationCode'));
    const rpcDrift = runCompatibility();
    assert.notEqual(rpcDrift.status, 0);
    assert.match(`${rpcDrift.stdout}\n${rpcDrift.stderr}`, /RPC removed or renamed/i);

    fs.writeFileSync(varifyPath, originalVarify);
    fs.writeFileSync(
        path.join(temporaryRoot, 'unregistered.proto'),
        'syntax = "proto3";\npackage audit;\n'
    );
    const unregisteredAuthority = runCompatibility();
    assert.notEqual(unregisteredAuthority.status, 0);
    assert.match(
        `${unregisteredAuthority.stdout}\n${unregisteredAuthority.stderr}`,
        /unregistered canonical proto source: unregistered\.proto/i
    );
});

// V02-PROTO-06
test('current Node consumer parses the initial wire fixture with unknown fields', /** 验证固定二进制请求及附带未知字段的请求均可反序列化。 */ () => {
    const fixtureRoot = path.join(
        repositoryRoot,
        'tests',
        'server',
        'protocol',
        'fixtures'
    );
    const deserialize = require('../../proto').VarifyService.service.GetVarifyCode.requestDeserialize;

    for (const fixture of ['varify-request-v1.bin', 'varify-request-v1-unknown.bin']) {
        const request = deserialize(fs.readFileSync(path.join(fixtureRoot, fixture)));
        assert.equal(request.email, 'compat-user@example.test');
    }
});
