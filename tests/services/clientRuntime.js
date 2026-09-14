'use strict';
const assert = require('node:assert/strict');
const { pack, verify } = require('./messageRuntime');
const [action, source, destination, qtRoot] = process.argv.slice(2);
assert.ok(action === 'pack' || action === 'verify');
if (action === 'pack') pack(source, destination, qtRoot, 'chat_e2e_client', null);
else verify(source, 'chat_e2e_client', null);
