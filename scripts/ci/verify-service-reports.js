'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { validate } = require('../../tests/services/phase3dEvidence');

const [root, sha] = process.argv.slice(2);
validate(root, sha, '3D');
const cleanup = JSON.parse(fs.readFileSync(path.join(root, 'teardown.json'), 'utf8'));
assert.equal(cleanup.complete, true);
assert.equal(cleanup.processComplete, true);
assert.ok(cleanup.primaryFailure == null);
