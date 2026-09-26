// Windows toolchain approval follows all Windows lanes, independently of Linux results.
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');

const repositories = { cmake: 'Kitware/CMake', ninja: 'ninja-build/ninja', 'powershell-core': 'PowerShell/PowerShell' };
/** 校验编译器与三项工具的锁定格式、来源和摘要；不合法时拒绝使用。 */
function validate(lock) {
    if (lock.schema !== 1 || !/^14\.\d+\.\d+$/.test(lock.msvc?.toolset) ||
        !/^19\.\d+\.\d+\.\d+$/.test(lock.msvc?.compilerVersion) ||
        !/^10\.0\.\d+\.0$/.test(lock.msvc?.sdk)) throw new Error('Invalid compiler lock');
    if (lock.msvc.compilerSha256 && !/^[a-f0-9]{64}$/.test(lock.msvc.compilerSha256)) throw new Error('Invalid compiler digest');
    if (lock.nativeSnapshotSha256 && !/^[a-f0-9]{64}$/.test(lock.nativeSnapshotSha256)) throw new Error('Invalid native snapshot digest');
    if (!Array.isArray(lock.tools) || lock.tools.length !== 3) throw new Error('Expected three locked tools');
    for (const name of Object.keys(repositories)) {
        const tools = lock.tools.filter(/** 按工具名称筛出对应锁定项以核对唯一性。 */ tool => tool.name === name);
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
/** 对工具版本和编译器摘要生成 SHA-256 身份，不把归档传输元数据计入依赖 ABI。 */
function identity(lock) {
    const { nativeSnapshotSha256, ...versions } = validate(lock);
    return createHash('sha256').update(JSON.stringify(versions)).digest('hex');
}
/** 只允许默认分支的定时任务或明确请求的手动任务刷新工具。 */
function canRefresh(event, ref, defaultBranch, requested) {
    return ref === `refs/heads/${defaultBranch}` &&
        (event === 'schedule' || (event === 'workflow_dispatch' && requested === 'true'));
}
/** 核对记录来自指定工作流和默认分支的已结束刷新；作业成功由 windowsPassed 单独验证。 */
function trustedRun(run, workflowId, branch) {
    return run.workflow_id === workflowId && run.head_branch === branch && ['success', 'failure'].includes(run.conclusion) &&
        ['schedule', 'workflow_dispatch'].includes(run.event);
}
/** Windows 工具链必须通过全部四个 Windows 门禁；Linux 失败不否定 Windows 编译器验证。 */
function windowsPassed(jobs) {
    return ['Static configuration checks', 'Server Release build', 'Qt client Release',
        'VarifyServer dependency and package check'].every(/** 每个必需作业必须唯一且执行成功。 */ name => {
        const matches = jobs.filter(/** 仅匹配当前 Windows 工作流的精确作业名。 */ job => job.name === `windows / ${name}`);
        return matches.length === 1 && matches[0].conclusion === 'success';
    });
}
/** 通过有界 gh API 请求读取 JSON，失败直接传播。 */
function api(endpoint) {
    return JSON.parse(execFileSync('gh', ['api', endpoint], { encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024 }));
}
/** 把工具选择结果发布到 GitHub 输出文件及标准输出。 */
function output(values) {
    const text = Object.entries(values).map(/** 把一个输出键值转换为工作流输出行。 */ ([key, value]) => `${key}=${value}\n`).join('');
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, text);
    process.stdout.write(text);
}
/** 下载指定运行中的已批准工具链工件，限制等待时间。 */
function download(repo, runId, directory, artifactName) {
    execFileSync('gh', ['run', 'download', String(runId), '--repo', repo,
        '--name', artifactName, '--dir', directory], { timeout: 120000, stdio: 'inherit' });
}
/** 选择受信工具链锁，验证工件身份后发布路径与来源；刷新只在授权事件执行。 */
function select(directory, { request = api, acquire = download, env = process.env, publish = output } = {}) {
    const repo = env.GITHUB_REPOSITORY;
    if (!/^[\w.-]+\/[\w.-]+$/.test(repo || '')) throw new Error('Invalid repository');
    const branch = request(`repos/${repo}`).default_branch;
    const refresh = canRefresh(env.GITHUB_EVENT_NAME, env.GITHUB_REF, branch, env.REFRESH_TOOLS);
    if (env.REFRESH_TOOLS === 'true' && !refresh) throw new Error('Tool refresh requires the default branch');
    const workflow = request(`repos/${repo}/actions/workflows/ci.yml`);
    let lock;
    let runId = '';
    let source = 'committed bootstrap';
    // Inspect runs as well as artifact names: a PR cannot promote its own toolchain.
    for (let page = 1; !refresh && !lock; page++) {
        if (page > 100) throw new Error('Approved toolchain lookup exceeded pagination limit');
        // Candidate records are usable only after independently verifying every Windows job.
        // This also recovers old refresh runs whose unrelated Linux failure blocked promotion.
        const result = request(`repos/${repo}/actions/artifacts?name=windows-toolchain-candidate&per_page=100&page=${page}`);
        for (const artifact of result.artifacts) {
            const run = request(`repos/${repo}/actions/runs/${artifact.workflow_run.id}`);
            if (!trustedRun(run, workflow.id, branch)) continue;
            const jobs = request(`repos/${repo}/actions/runs/${run.id}/jobs?per_page=100`);
            if (jobs.total_count > 100) throw new Error('Windows approval job list exceeds limit');
            if (!windowsPassed(jobs.jobs)) continue;
            if (artifact.expired) throw new Error('Approved toolchain expired; run a default-branch tool refresh');
            acquire(repo, run.id, directory, 'windows-toolchain-candidate');
            lock = validate(JSON.parse(fs.readFileSync(path.join(directory, 'windows-toolchain.json'), 'utf8')));
            source = `Windows-validated run ${run.id}`;
            runId = String(run.id);
            break;
        }
        if (result.artifacts.length < 100) break;
    }
    lock ||= validate(JSON.parse(fs.readFileSync(path.join(__dirname, 'windows-toolchain.json'), 'utf8')));
    if (refresh) source = 'weekly candidate; previous approved versions remain active until Windows success';
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'windows-toolchain.json'), JSON.stringify(lock, null, 2) + '\n');
    const result = { lock: JSON.stringify(lock), refresh, source, run_id: runId };
    publish(result);
    return result;
}
module.exports = { validate, identity, canRefresh, trustedRun, windowsPassed, select };
if (require.main === module) {
    try {
        if (!process.argv[3]) throw new Error('Usage: toolchain.js select DIRECTORY | validate FILE');
        if (process.argv[2] === 'select') select(process.argv[3]);
        else if (process.argv[2] === 'validate') validate(JSON.parse(fs.readFileSync(process.argv[3], 'utf8')));
        else throw new Error('Unknown toolchain command');
    } catch (error) { console.error(error.message); process.exitCode = 1; }
}
