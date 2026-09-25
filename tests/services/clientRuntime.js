'use strict';
const assert = require('node:assert/strict');
const { pack, verify } = require('./messageRuntime');
const [action, source, destination, qtRoot, installedRoot] = process.argv.slice(2);
assert.ok(action === 'pack' || action === 'verify');
if (action === 'pack') {
    assert.ok(qtRoot && installedRoot, 'Qt and vcpkg roots are required for the client bundle');
    pack(source, destination, [qtRoot, installedRoot], 'chat_e2e_client', null);
}
else verify(source, 'chat_e2e_client', null);
