'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { validate } = require('./phase3dEvidence');
const { combinations } = require('../compatibility/bootstrap');

const requiredChecks = ['Static configuration checks', 'Server Release build', 'Qt client Release',
    'VarifyServer dependency and package check', 'Linux POSIX process lifecycle',
    'Linux configure compile link and startup preflight', 'Linux two-server foundation contract',
    'Phase 3C disposable services', 'Phase 3C downstream hard gate'];
const bootstrap = 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1';
const hash = bytes => createHash('sha256').update(bytes).digest('hex');

function selectCheck(checks, name, candidateSha) {
    return checks.filter(check => check.name === name && check.head_sha === candidateSha)
        .sort((left, right) => right.id - left.id)[0];
}

function aggregate({ services, phase3c, output, sourceSha, candidateSha, commit, checkPages }) {
    fs.mkdirSync(output, { recursive: true });
    fs.mkdirSync(path.join(output, 'junit'), { recursive: true });
    const result = { format: 1, sourceSha, candidateSha, currentNPass: false, releaseEligible: false,
        compatibility: 'UNAVAILABLE', next_route: null };
    const read = (root, name) => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
    try {
        assert.match(candidateSha, /^[a-f0-9]{40}$/);
        assert.equal(commit.sha, sourceSha);
        assert.ok(sourceSha === candidateSha || commit.parents?.some(parent => parent.sha === candidateSha));
        validate(services, sourceSha, '3D');
        const teardown = read(services, 'teardown.json');
        assert.equal(teardown.complete, true);
        assert.equal(teardown.processComplete, true);
        assert.ok(teardown.primaryFailure == null);
        assert.ok(Array.isArray(checkPages) && checkPages.length > 0);
        const checks = checkPages.flatMap(page => page.check_runs);
        result.checks = requiredChecks.map(name => {
            const check = selectCheck(checks, name, candidateSha);
            assert.ok(check && check.status === 'completed' && check.conclusion === 'success');
            assert.match(check.html_url, /^https:\/\/github\.com\//);
            return { name, id: check.id, url: check.html_url };
        });
        const prior = read(phase3c, 'gate.json');
        assert.equal(prior.sourceSha, sourceSha);
        assert.equal(prior.currentNPass, true);
        assert.equal(prior.compatibility, bootstrap);
        const matrix = read(phase3c, 'compatibility.json');
        assert.equal(matrix.sourceSha, sourceSha);
        assert.equal(matrix.status, bootstrap);
        assert.equal(matrix.releaseEligible, false);
        assert.deepEqual(matrix.publishedReleaseIds, []);
        assert.equal(matrix.schemaManifestSha256, hash(fs.readFileSync(path.join(__dirname, '../../schema/manifest.json'))));
        assert.deepEqual(matrix.entries.map(entry => entry.combination), combinations);
        assert.deepEqual(matrix.entries.map(entry => entry.id), [1, 2, 3, 4, 5].map(id => `T10-COMPAT-0${id}`));
        assert.equal(read(phase3c, 'phase3c-reports.json').sourceSha, sourceSha);
        assert.ok(matrix.entries.every(entry => entry.status === bootstrap && entry.executed === false));
        const compatibility = { ...matrix, requirement: 'G-017', source: 'same-run Phase 3C promotion inventory',
            entries: matrix.entries.map((entry, index) => ({ ...entry,
                id: `E03-COMPAT-${String(index + 1).padStart(2, '0')}`, sourceId: entry.id })) };
        fs.writeFileSync(path.join(output, 'compatibility.json'), JSON.stringify(compatibility, null, 2));
        fs.writeFileSync(path.join(output, 'junit/linux_phase3d_compatibility.xml'),
            `<testsuite name="phase3d-compatibility" tests="5" failures="0" skipped="5">${compatibility.entries.map(entry =>
                `<testcase name="${entry.id}"><skipped message="${bootstrap}"/></testcase>`).join('')}</testsuite>\n`);
        const manifest = read(services, 'phase3d-reports.json');
        for (const report of manifest.reports) fs.copyFileSync(path.join(services, report.file), path.join(output, 'junit', report.file));
        for (const name of ['phase3d-reports.json', 'topology.json', 'teardown.json', 'redaction.json',
            'application-teardown.json', 'process-teardown.json', 'fault-relay.json']) {
            fs.copyFileSync(path.join(services, name), path.join(output, name));
        }
        result.manifests = { phase3d: hash(fs.readFileSync(path.join(services, 'phase3d-reports.json'))),
            phase3c: hash(fs.readFileSync(path.join(phase3c, 'phase3c-reports.json'))) };
        result.currentNPass = true;
        result.caseCount = manifest.reports.reduce((sum, report) => sum + report.expected, 0);
        result.compatibility = bootstrap;
        result.next_route = 'R-00';
        result.openRequirements = ['G-017'];
    } catch {
        result.failure = 'required-evidence-invalid';
    }
    fs.writeFileSync(path.join(output, 'release-admission.json'), JSON.stringify(result, null, 2));
    fs.writeFileSync(path.join(output, 'junit/linux_phase3d_gate.xml'),
        `<testsuite name="phase3d-current-n-admission" tests="1" failures="${result.currentNPass ? 0 : 1}">` +
        `<testcase name="E03-CLOSE-01 same-candidate current-N evidence admission">${result.currentNPass ? '' :
            '<failure message="required-evidence-invalid"/>'}</testcase></testsuite>\n`);
    return result;
}
module.exports = { aggregate, requiredChecks, selectCheck };
if (require.main === module) {
    try {
        const [services, phase3c, output, sourceSha, candidateSha, commitFile, checksFile] = process.argv.slice(2);
        const readInput = file => { try { return JSON.parse(fs.readFileSync(file)); } catch { return null; } };
        const result = aggregate({ services, phase3c, output, sourceSha, candidateSha,
            commit: readInput(commitFile), checkPages: readInput(checksFile) });
        process.exitCode = result.currentNPass ? 0 : 1;
    } catch { process.stderr.write('Phase 3D admission inputs unavailable\n'); process.exitCode = 1; }
}
