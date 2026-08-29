'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const repositoryRoot = path.resolve(__dirname, '..', '..', '..');

test('shared verification constants keep their wire-visible values', () => {
    const { code_prefix, Errors } = require('../../const');

    assert.equal(code_prefix, 'code_');
    assert.deepEqual(Errors, { Success: 0, RedisErr: 1, Exception: 2 });
});

test('loaded protobuf exposes the verification RPC request and response types', () => {
    const messageProto = require('../../proto');

    assert.equal(typeof messageProto.VarifyService, 'function');
    assert.equal(typeof messageProto.VarifyService.service.GetVarifyCode, 'object');
    assert.equal(messageProto.VarifyService.service.GetVarifyCode.path, '/message.VarifyService/GetVarifyCode');
    assert.equal(typeof messageProto.GetVarifyReq, 'object');
    assert.equal(typeof messageProto.GetVarifyRsp, 'object');
});

// V02-PROTO-03
test('repository canonical proto module is the sole editable wire authority', () => {
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
        legacyEditableCopies.filter((relative) => fs.existsSync(path.join(repositoryRoot, relative))),
        [],
        'service-local message.proto files must not remain editable authorities'
    );
});

// V02-PROTO-04
test('protocol compatibility command accepts the canonical contract and generated consumers', () => {
    const command = spawnSync(
        process.execPath,
        [path.join(repositoryRoot, 'scripts', 'protocol-compatibility.js'), 'check'],
        { cwd: repositoryRoot, encoding: 'utf8', timeout: 15000 }
    );

    assert.equal(command.status, 0, `${command.stdout}\n${command.stderr}`);
    assert.match(command.stdout, /Protocol compatibility and generated-source drift checks passed/);
});

// V02-PROTO-05
test('compatibility command rejects isolated descriptor drift and unregistered authorities', (t) => {
    const temporaryRoot = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'chat-proto-mutation-'));
    t.after(() => fs.rmSync(temporaryRoot, { recursive: true, force: true }));

    for (const file of ['varify.proto', 'status.proto', 'chat.proto']) {
        fs.copyFileSync(
            path.join(repositoryRoot, 'proto', file),
            path.join(temporaryRoot, file)
        );
    }

    const runCompatibility = () => spawnSync(
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
test('current Node consumer parses the initial wire fixture with unknown fields', () => {
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
