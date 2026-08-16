'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

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
