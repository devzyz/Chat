'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const { pack, verify } = require('./messageRuntime');
const entries = { driver: 'FourProcessDriver', GateServer: 'GateServer', StatusServer: 'StatusServer', ChatServer: 'ChatServer' };
const [action, binaries, destination, installed] = process.argv.slice(2);
assert.ok(action === 'pack' || action === 'verify');
for (const [directory, name] of Object.entries(entries)) {
    if (action === 'pack') pack(path.join(binaries, name), path.join(destination, directory), installed, name, directory === 'driver' ? ['validation'] : null);
    else verify(path.join(binaries, directory), name, directory === 'driver' ? ['validation'] : null);
}
