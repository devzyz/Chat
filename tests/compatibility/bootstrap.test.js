'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { assess } = require('./bootstrap');
const sha = 'a'.repeat(40);

test('an empty successful release inventory blocks all five runtime combinations', () => {
    const result = assess([[]], sha);
    assert.equal(result.status, 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1');
    assert.equal(result.releaseEligible, false);
    assert.equal(result.entries.length, 5);
    assert.ok(result.entries.every(entry => entry.status === result.status && entry.executed === false));
});

test('published releases require baseline review instead of a fabricated bootstrap or PASS', () => {
    const result = assess([[{ id: 12, draft: false, prerelease: false }]], sha);
    assert.equal(result.status, 'BASELINE_REVIEW_REQUIRED');
    assert.equal(result.releaseEligible, false);
    assert.ok(result.entries.every(entry => !entry.executed));
});

test('missing, malformed and failed inventories cannot become bootstrap evidence', () => {
    for (const value of [null, [], {}, [null], [[{}]], [{ message: 'Not Found' }]]) {
        assert.throws(() => assess(value, sha));
    }
    assert.throws(() => assess([[]], 'branch-name'));
});
