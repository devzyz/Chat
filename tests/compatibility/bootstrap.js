'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');

const combinations = ['N-1 client -> N server', 'N client -> N-1 server',
    'N-1 service -> N service', 'N service -> N-1 service', 'N-1 schema -> N schema'];

// Until a release is published there is no legitimate historical runtime to
// resolve. Any published release requires an explicit promotion/digest review;
// neither a source checkout nor an Actions cache is a historical artifact.
function assess(pages, sourceSha) {
    if (!/^[a-f0-9]{40}$/.test(sourceSha) || !Array.isArray(pages) || pages.length === 0 ||
        !pages.every(page => Array.isArray(page) && page.every(release =>
            Number.isSafeInteger(release?.id) && release.id > 0 &&
            typeof release.draft === 'boolean' && typeof release.prerelease === 'boolean'))) {
        throw new Error('release inventory or candidate identity unavailable');
    }
    const published = pages.flat().filter(release => !release.draft);
    const status = published.length ? 'BASELINE_REVIEW_REQUIRED' : 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1';
    return { format: 1, sourceSha, status, releaseEligible: false, requirement: 'G-017',
        publishedReleaseIds: published.map(release => release.id),
        entries: combinations.map((combination, index) => ({
            id: `T10-COMPAT-${String(index + 1).padStart(2, '0')}`, combination, status, executed: false
        })) };
}

function write(root, result) {
    fs.mkdirSync(root, { recursive: true });
    result.schemaManifestSha256 = createHash('sha256').update(
        fs.readFileSync(path.resolve(__dirname, '../../schema/manifest.json'))).digest('hex');
    fs.writeFileSync(path.join(root, 'compatibility.json'), JSON.stringify(result, null, 2));
    const rows = result.entries.map(entry => `<testcase name="${entry.id}"><skipped message="${result.status}"/></testcase>`);
    fs.writeFileSync(path.join(root, 'linux_compatibility.xml'),
        `<testsuite name="runtime-compatibility" tests="5" failures="0" skipped="5">\n${rows.join('\n')}\n</testsuite>\n`);
}

module.exports = { assess, write };
if (require.main === module) {
    try {
        const result = assess(JSON.parse(fs.readFileSync(process.argv[2], 'utf8')), process.argv[4]);
        write(process.argv[3], result);
        // Standalone compatibility never exits successfully for an unexecuted matrix.
        process.exitCode = result.status === 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1' ? 2 : 1;
    } catch {
        process.stderr.write('compatibility inventory invalid or unavailable\n');
        process.exitCode = 1;
    }
}
