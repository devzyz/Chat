// GitHub cache hits describe an archive download, not vcpkg ABI reuse.
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');

const [command, directory, argument] = process.argv.slice(2);
const repo = path.resolve(__dirname, '../..');

function output(values) {
    const text = Object.entries(values).map(([key, value]) => `${key}=${value}\n`).join('');
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, text);
    process.stdout.write(text);
}

function required(name) {
    const value = process.env[name];
    if (!value || /[\r\n]/.test(value)) throw new Error(`Missing or invalid ${name}`);
    return value;
}

function inventory(root, prefix = '') {
    return fs.readdirSync(root, { withFileTypes: true }).flatMap(entry => {
        const relative = `${prefix}${entry.name}`;
        const file = path.join(root, entry.name);
        if (entry.isSymbolicLink()) throw new Error('Binary cache must not contain symbolic links');
        if (entry.isDirectory()) return inventory(file, `${relative}/`);
        // vcpkg archive names contain their ABI identity. Ignore temporary files.
        return entry.isFile() && entry.name.endsWith('.zip') ? [[relative, fs.statSync(file).size]] : [];
    }).sort(([a], [b]) => a.localeCompare(b));
}

try {
    if (!directory) throw new Error('Usage: vcpkgBinaryCache.js prepare|snapshot|report DIRECTORY [TRIPLET|LOG]');
    const beforePath = `${path.resolve(directory)}.before.json`;
    if (command === 'prepare') {
        if (!['x64-windows-chat-release', 'x64-linux-chat-release'].includes(argument)) {
            throw new Error('Unsupported CI triplet');
        }
        const environment = ['RUNNER_OS', 'RUNNER_ARCH', 'ImageOS', 'ImageVersion'].map(required);
        const inputs = ['vcpkg.json', `triplets/${argument}.cmake`];
        if (fs.existsSync(path.join(repo, 'vcpkg-configuration.json'))) inputs.push('vcpkg-configuration.json');
        if (argument === 'x64-linux-chat-release') inputs.push('scripts/ci/linux-tool-assets.json');
        const hash = createHash('sha256').update(JSON.stringify(environment));
        for (const input of inputs) hash.update(input).update(fs.readFileSync(path.join(repo, input)));
        const prefix = `vcpkg-binary-v2-${environment[0]}-${environment[1]}-${hash.digest('hex')}-`;
        const runId = required('GITHUB_RUN_ID');
        const attempt = required('GITHUB_RUN_ATTEMPT');
        if (!/^\d+$/.test(runId) || !/^\d+$/.test(attempt)) throw new Error('Invalid workflow run identity');
        fs.mkdirSync(directory, { recursive: true });
        output({ prefix, key: `${prefix}${runId}-${attempt}` });
    } else if (command === 'snapshot') {
        fs.writeFileSync(beforePath, JSON.stringify(inventory(directory)));
    } else if (command === 'report') {
        const before = JSON.parse(fs.readFileSync(beforePath, 'utf8'));
        const after = inventory(directory);
        const changed = after.length > 0 && JSON.stringify(before) !== JSON.stringify(after);
        const log = argument && fs.existsSync(argument) ? fs.readFileSync(argument, 'utf8') : '';
        const restored = [...log.matchAll(/Restored (\d+) package\(s\)/g)];
        const built = [...log.matchAll(/Building [^\s:]+:[^\s]+/g)].length;
        const count = restored.length ? restored.reduce((sum, match) => sum + Number(match[1]), 0) : 'unavailable';
        const summary = `### vcpkg binary cache\n\nRestored packages: ${count}\n\n` +
            `Built packages: ${log ? built : 'unavailable'}\n\n` +
            `Archives: ${before.length} -> ${after.length}; save updated cache: ${changed}\n\n`;
        process.stdout.write(summary);
        if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, summary);
        output({ changed, archives: after.length });
    } else {
        throw new Error(`Unknown binary cache command: ${command}`);
    }
} catch (error) {
    console.error(error.message);
    process.exitCode = 1;
}
