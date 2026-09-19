// The approved artifact is published only after the default branch's full lane succeeds.
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');

const repositories = { cmake: 'Kitware/CMake', ninja: 'ninja-build/ninja', 'powershell-core': 'PowerShell/PowerShell' };
function validate(lock) {
    if (lock.schema !== 1 || !/^14\.\d+\.\d+$/.test(lock.msvc?.toolset) ||
        !/^19\.\d+\.\d+\.\d+$/.test(lock.msvc?.compilerVersion) ||
        !/^10\.0\.\d+\.0$/.test(lock.msvc?.sdk)) throw new Error('Invalid compiler lock');
    if (lock.msvc.compilerSha256 && !/^[a-f0-9]{64}$/.test(lock.msvc.compilerSha256)) throw new Error('Invalid compiler digest');
    if (!Array.isArray(lock.tools) || lock.tools.length !== 3) throw new Error('Expected three locked tools');
    for (const name of Object.keys(repositories)) {
        const tools = lock.tools.filter(tool => tool.name === name);
        const tool = tools[0];
        if (tools.length !== 1 || tool.os !== 'windows' || tool.arch !== (name === 'cmake' ? 'amd64' : 'x64') ||
            !/^\d+\.\d+\.\d+$/.test(tool.version) || !/^[a-f0-9]{128}$/.test(tool.sha512) ||
            !tool.url.startsWith(`https://github.com/${repositories[name]}/releases/download/`) ||
            !/^[A-Za-z0-9_.+-]+\.zip$/.test(tool.archive) ||
            !/^[A-Za-z0-9_./+-]+\.exe$/.test(tool.executable) || tool.executable.includes('..')) {
            throw new Error(`Invalid ${name} lock`);
        }
    }
    return lock;
}
function identity(lock) { return createHash('sha256').update(JSON.stringify(validate(lock))).digest('hex'); }
function canRefresh(event, ref, defaultBranch, requested) {
    return ref === `refs/heads/${defaultBranch}` &&
        (event === 'schedule' || (event === 'workflow_dispatch' && requested === 'true'));
}
function trustedRun(run, workflowId, branch) {
    return run.workflow_id === workflowId && run.head_branch === branch && run.conclusion === 'success' &&
        ['schedule', 'workflow_dispatch'].includes(run.event);
}
function api(endpoint) {
    return JSON.parse(execFileSync('gh', ['api', endpoint], { encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024 }));
}
function output(values) {
    const text = Object.entries(values).map(([key, value]) => `${key}=${value}\n`).join('');
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, text);
    process.stdout.write(text);
}
function download(repo, runId, directory) {
    execFileSync('gh', ['run', 'download', String(runId), '--repo', repo,
        '--name', 'ci-toolchain-approved', '--dir', directory], { timeout: 120000, stdio: 'inherit' });
}
function select(directory, { request = api, acquire = download, env = process.env, publish = output } = {}) {
    const repo = env.GITHUB_REPOSITORY;
    if (!/^[\w.-]+\/[\w.-]+$/.test(repo || '')) throw new Error('Invalid repository');
    const branch = request(`repos/${repo}`).default_branch;
    const refresh = canRefresh(env.GITHUB_EVENT_NAME, env.GITHUB_REF, branch, env.REFRESH_TOOLS);
    if (env.REFRESH_TOOLS === 'true' && !refresh) throw new Error('Tool refresh requires the default branch');
    const workflow = request(`repos/${repo}/actions/workflows/ci.yml`);
    let lock;
    let source = 'committed bootstrap';
    // Inspect runs as well as artifact names: a PR cannot promote its own toolchain.
    for (let page = 1; !refresh && !lock; page++) {
        if (page > 100) throw new Error('Approved toolchain lookup exceeded pagination limit');
        const result = request(`repos/${repo}/actions/artifacts?name=ci-toolchain-approved&per_page=100&page=${page}`);
        for (const artifact of result.artifacts) {
            const run = request(`repos/${repo}/actions/runs/${artifact.workflow_run.id}`);
            if (!trustedRun(run, workflow.id, branch)) continue;
            if (artifact.expired) throw new Error('Approved toolchain expired; run a default-branch tool refresh');
            acquire(repo, run.id, directory);
            lock = validate(JSON.parse(fs.readFileSync(path.join(directory, 'windows-toolchain.json'), 'utf8')));
            source = `approved run ${run.id}`;
            break;
        }
        if (result.artifacts.length < 100) break;
    }
    lock ||= validate(JSON.parse(fs.readFileSync(path.join(__dirname, 'windows-toolchain.json'), 'utf8')));
    if (refresh) source = 'weekly candidate; previous approved versions remain active until full success';
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'windows-toolchain.json'), JSON.stringify(lock, null, 2) + '\n');
    const result = { lock: JSON.stringify(lock), refresh, source };
    publish(result);
    return result;
}
module.exports = { validate, identity, canRefresh, trustedRun, select };
if (require.main === module) {
    try {
        if (!process.argv[3]) throw new Error('Usage: toolchain.js select DIRECTORY | validate FILE');
        if (process.argv[2] === 'select') select(process.argv[3]);
        else if (process.argv[2] === 'validate') validate(JSON.parse(fs.readFileSync(process.argv[3], 'utf8')));
        else throw new Error('Unknown toolchain command');
    } catch (error) { console.error(error.message); process.exitCode = 1; }
}
