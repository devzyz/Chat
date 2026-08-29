'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { descriptorModel } = require('./protocol-compatibility');

const repositoryRoot = path.resolve(__dirname, '..');
const fixtureRoot = path.join(repositoryRoot, 'tests', 'server', 'protocol', 'fixtures');
const baselinePath = path.join(fixtureRoot, 'initial-release-descriptor.pb');

function encodeVarint(value) {
    const bytes = [];
    let remaining = value;
    do {
        let byte = remaining & 0x7f;
        remaining = Math.floor(remaining / 128);
        if (remaining > 0) {
            byte |= 0x80;
        }
        bytes.push(byte);
    } while (remaining > 0);
    return Buffer.from(bytes);
}

const baseline = descriptorModel(baselinePath);
const request = baseline.messages.get('.message.GetVarifyReq');
assert.ok(request, 'initial descriptor is missing message.GetVarifyReq');
const emailField = request.fieldsByName.get('email');
assert.ok(emailField, 'initial descriptor is missing GetVarifyReq.email');
assert.equal(emailField.type, 9, 'GetVarifyReq.email must remain a string in the initial descriptor');
assert.equal(emailField.label, 1, 'GetVarifyReq.email must remain singular in the initial descriptor');

const textproto = fs.readFileSync(path.join(fixtureRoot, 'varify-request-v1.textproto'), 'utf8');
const emailMatch = /^email:\s*"([^"]+)"\s*$/m.exec(textproto);
assert.ok(emailMatch, 'varify-request-v1.textproto must contain one quoted email field');
const email = Buffer.from(emailMatch[1], 'utf8');
const encoded = Buffer.concat([
    encodeVarint((emailField.number << 3) | 2),
    encodeVarint(email.length),
    email
]);

fs.writeFileSync(path.join(fixtureRoot, 'varify-request-v1.bin'), encoded);
// Field 99 (varint) is absent from the initial descriptor: key=(99 << 3)|0, value=7.
assert.equal(request.fields.has(99), false, 'field 99 must be unknown to the initial request');
fs.writeFileSync(
    path.join(fixtureRoot, 'varify-request-v1-unknown.bin'),
    Buffer.concat([encoded, encodeVarint((99 << 3) | 0), encodeVarint(7)])
);
process.stdout.write('Initial Varify wire fixtures generated from the migration-preexisting descriptor baseline.\n');
