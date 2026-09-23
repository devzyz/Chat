// GitHub cache hits describe an archive download, not vcpkg ABI reuse.
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');

const [command, directory, argument] = process.argv.slice(2);
const repo = path.resolve(__dirname, '../..');

/** 把缓存测量结果发布为工作流输出及标准输出。 */
function output(values) {
    const text = Object.entries(values).map(/** 将输出键值编码为工作流输出行。 */ ([key, value]) => `${key}=${value}\n`).join('');
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, text);
    process.stdout.write(text);
}

/** 读取必需环境值，缺失或包含换行时拒绝。 */
function required(name) {
    const value = process.env[name];
    if (!value || /[\r\n]/.test(value)) throw new Error(`Missing or invalid ${name}`);
    return value;
}

/** 递归列出缓存 ZIP 路径及大小并排序；拒绝符号链接，忽略临时文件。 */
function inventory(root, prefix = '') {
    return fs.readdirSync(root, { withFileTypes: true }).flatMap(/** 检查目录项，递归目录或记录 ZIP 大小，拒绝符号链接。 */ entry => {
        const relative = `${prefix}${entry.name}`;
        const file = path.join(root, entry.name);
        if (entry.isSymbolicLink()) throw new Error('Binary cache must not contain symbolic links');
        if (entry.isDirectory()) return inventory(file, `${relative}/`);
        // vcpkg archive names contain their ABI identity. Ignore temporary files.
        return entry.isFile() && entry.name.endsWith('.zip') ? [[relative, fs.statSync(file).size]] : [];
    }).sort(/** 按相对路径稳定排列缓存清单。 */ ([a], [b]) => a.localeCompare(b));
}

try {
    if (!directory) throw new Error('Usage: vcpkgBinaryCache.js prepare|snapshot|report DIRECTORY [TRIPLET|LOG]');
    const beforePath = `${path.resolve(directory)}.before.json`;
    if (command === 'prepare') {
        if (!['x64-windows-chat-release', 'x64-linux-chat-release'].includes(argument)) {
            throw new Error('Unsupported CI triplet');
        }
        // Image revisions can change unrelated software. vcpkg checks each archive's ABI itself.
        const environment = ['RUNNER_OS', 'RUNNER_ARCH', 'ImageOS'].map(required);
        const inputs = ['vcpkg.json', `triplets/${argument}.cmake`];
        // A newer runner image must not silently switch the native toolchain.
        // Windows fallbacks stay inside the same approved tool identity.
        let toolIdentity = '';
        if (process.env.RUNNER_OS === 'Windows') {
            const lockPath = process.env.CHAT_WINDOWS_TOOLCHAIN;
            if (!lockPath) throw new Error('Windows cache requires a verified toolchain');
            toolIdentity = require('./toolchain').identity(JSON.parse(fs.readFileSync(lockPath, 'utf8')));
        }
        if (fs.existsSync(path.join(repo, 'vcpkg-configuration.json'))) inputs.push('vcpkg-configuration.json');
        if (argument === 'x64-linux-chat-release') inputs.push('scripts/ci/linux-tool-assets.json');
        const hash = createHash('sha256').update(JSON.stringify(environment));
        for (const input of inputs) hash.update(input).update(fs.readFileSync(path.join(repo, input)));
        // CI uses the same triplet for target and host. Keep both roles explicit in the namespace.
        const fallback = toolIdentity ? `vcpkg-binary-v4-${environment.join('-')}-${argument}-${argument}-${toolIdentity}-` :
            `vcpkg-binary-v3-${environment.join('-')}-${argument}-${argument}-`;
        const prefix = `${fallback}${hash.digest('hex')}-`;
        // One-time bridge to verified PR #6 archives; do not broaden legacy restore across image families.
        const legacyPrefixes = {
            'Windows-X64-win22': 'vcpkg-binary-v2-Windows-X64-6d8874220c41f45f55b7178c2d0e2754ea8a765fab8099a2de1a2fd98aedef9c-',
            'Linux-X64-ubuntu24': 'vcpkg-binary-v2-Linux-X64-c124e21ad37803a7fc7c0fc69921cfb238512222d4d36b98da0c8889a1a08d3f-'
        };
        const runId = required('GITHUB_RUN_ID');
        const attempt = required('GITHUB_RUN_ATTEMPT');
        if (!/^\d+$/.test(runId) || !/^\d+$/.test(attempt)) throw new Error('Invalid workflow run identity');
        fs.mkdirSync(directory, { recursive: true });
        output({ prefix, fallback, legacy: toolIdentity ? '' : legacyPrefixes[environment.join('-')] || '',
            key: `${prefix}${runId}-${attempt}` });
    } else if (command === 'snapshot') {
        fs.writeFileSync(beforePath, JSON.stringify(inventory(directory)));
    } else if (command === 'report') {
        const before = JSON.parse(fs.readFileSync(beforePath, 'utf8'));
        const after = inventory(directory);
        const restoredKey = process.env.CACHE_RESTORED_KEY || '';
        const primaryPrefix = process.env.CACHE_PRIMARY_PREFIX || '';
        const promoted = Boolean(restoredKey && primaryPrefix && !restoredKey.startsWith(primaryPrefix));
        const changed = after.length > 0 && (JSON.stringify(before) !== JSON.stringify(after) || promoted);
        const log = argument && fs.existsSync(argument) ? fs.readFileSync(argument, 'utf8') : '';
        const restored = [...log.matchAll(/Restored (\d+) package\(s\)/g)];
        const built = [...log.matchAll(/Building [^\s:]+:[^\s]+/g)].length;
        const count = restored.length ? restored.reduce(/** 累计日志中匹配到的缓存计数。 */ (sum, match) => sum + Number(match[1]), 0) : 'unavailable';
        const elapsed = [...log.matchAll(/All requested installations completed successfully in: ([^\r\n]+)/g)]
            .map(/** 提取匹配到的 ABI 标识。 */ match => match[1]).join(', ') || 'unavailable';
        const reason = !after.length ? 'empty' : promoted ? 'promote restored namespace' :
            changed ? 'archive inventory changed' : 'unchanged';
        const summary = `### vcpkg binary cache\n\nRestored key: ${restoredKey || 'none'}\n\n` +
            `Image revision: ${process.env.ImageVersion || 'unavailable'}\n\n` +
            `Promote restored cache: ${promoted}\n\nRestored packages: ${count}\n\n` +
            `Built packages: ${log ? built : 'unavailable'}\n\n` +
            `Dependency installation time: ${elapsed}\n\nSave reason: ${reason}\n\n` +
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
