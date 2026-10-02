'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const assert = require('node:assert/strict');

/** 通过参数数组调用 GitHub CLI，不使用 shell 拼接输入。 */
function gh(args) { return execFileSync('gh', args, { encoding: 'utf8', timeout: 30000, windowsHide: true }).trim(); }
/** 核对运行身份，拒绝其他提交、事件或工作流的证据。 */
function validateRun(run, receipt) {
    assert.equal(run.headSha, receipt.sha, 'run-source-mismatch');
    assert.equal(run.event, 'workflow_dispatch', 'run-event-mismatch');
    assert.equal(run.workflowName, 'CI', 'run-workflow-mismatch');
    if (receipt.runId) assert.equal(String(run.databaseId), String(receipt.runId), 'run-id-mismatch');
}
/** 读取指定运行的状态和源码身份。 */
function readRun(repository, runId) {
    return JSON.parse(gh(['run', 'view', String(runId), '--repo', repository, '--json',
        'databaseId,headSha,event,workflowName,status,conclusion,url,displayTitle']));
}
/** 解析有限参数，拒绝遗漏值、重复值和未知开关。 */
function options(args) {
    const result = {};
    for (let index = 0; index < args.length; index += 2) {
        const key = args[index];
        assert.ok(['--ref', '--pr', '--profile', '--receipt', '--run'].includes(key), 'unknown-option');
        assert.ok(args[index + 1] && !Object.hasOwn(result, key), 'missing-or-duplicate-option');
        result[key] = args[index + 1];
    }
    return result;
}
/** 触发、观察或下载一场性能运行；请求结果不明时仅恢复查询，不重复 dispatch。 */
function main(args) {
    const [action, ...rest] = args; const opts = options(rest);
    assert.ok(['start', 'status', 'download', 'recover'].includes(action), 'unknown-action');
    if (action === 'start') {
        const repo = JSON.parse(gh(['repo', 'view', '--json', 'nameWithOwner,defaultBranchRef']));
        const profile = opts['--profile'] || 'baseline'; assert.ok(['smoke', 'baseline', 'stress'].includes(profile));
        assert.ok(!(opts['--ref'] && opts['--pr']), 'ambiguous-source');
        let ref = opts['--ref'] || repo.defaultBranchRef.name; let sha;
        if (opts['--pr']) {
            const pr = JSON.parse(gh(['pr', 'view', opts['--pr'], '--repo', repo.nameWithOwner, '--json',
                'headRefName,headRefOid,isCrossRepository']));
            assert.equal(pr.isCrossRepository, false, 'fork-requires-reviewed-local-branch'); ref = pr.headRefName; sha = pr.headRefOid;
        } else sha = JSON.parse(gh(['api', `repos/${repo.nameWithOwner}/commits/${encodeURIComponent(ref)}`])).sha;
        assert.match(sha, /^[a-f0-9]{40}$/);
        const requestId = crypto.randomUUID();
        const root = path.resolve('build/performance-requests'); fs.mkdirSync(root, { recursive: true });
        const file = path.join(root, `${requestId}.json`);
        const receipt = { repository: repo.nameWithOwner, ref, sha, profile, requestId, state: 'dispatch-unknown', createdAt: new Date().toISOString() };
        fs.writeFileSync(file, JSON.stringify(receipt, null, 2));
        console.log(`Receipt: ${file}`);
        // Save before the network call: an ambiguous timeout must never trigger an automatic duplicate.
        const output = gh(['workflow', 'run', 'ci.yml', '--repo', repo.nameWithOwner, '--ref', ref,
            '-f', 'mode=performance', '-f', `performance_profile=${profile}`,
            '-f', `performance_request_id=${requestId}`, '-f', `performance_source_sha=${sha}`]);
        const match = /\/actions\/runs\/(\d+)/.exec(output);
        if (match) receipt.runId = match[1];
        receipt.state = 'dispatched'; fs.writeFileSync(file, JSON.stringify(receipt, null, 2));
        console.log(output || `Submitted ${requestId}; recover with this receipt.`); return;
    }
    assert.ok(opts['--receipt'], 'receipt-required');
    const file = path.resolve(opts['--receipt']); const receipt = JSON.parse(fs.readFileSync(file));
    if (!receipt.runId) {
        const runs = JSON.parse(gh(['run', 'list', '--repo', receipt.repository, '--workflow', 'ci.yml',
            '--event', 'workflow_dispatch', '--limit', '100', '--json', 'databaseId,displayTitle,headSha']));
        const matches = runs.filter(/** 同时匹配唯一请求标识和源码提交。 */ run =>
            run.displayTitle.includes(receipt.requestId) && run.headSha === receipt.sha);
        if (opts['--run']) {
            const candidate = readRun(receipt.repository, opts['--run']);
            assert.ok(candidate.displayTitle.includes(receipt.requestId), 'request-id-mismatch');
            matches.push(candidate);
        }
        const unique = [...new Map(matches.map(/** 按远端运行 ID 去重。 */ run => [run.databaseId, run])).values()];
        assert.equal(unique.length, 1, 'dispatch-not-uniquely-resolved-do-not-resubmit'); receipt.runId = unique[0].databaseId;
        fs.writeFileSync(file, JSON.stringify(receipt, null, 2));
    }
    const run = readRun(receipt.repository, receipt.runId); validateRun(run, receipt);
    console.log(JSON.stringify(run, null, 2));
    if (action !== 'download') return;
    assert.equal(run.status, 'completed', 'run-not-complete');
    const output = path.resolve('build/performance-results', String(receipt.runId));
    fs.mkdirSync(output, { recursive: true });
    gh(['run', 'download', String(receipt.runId), '--repo', receipt.repository, '--name', 'performance-results', '--dir', output]);
    const report = JSON.parse(fs.readFileSync(path.join(output, 'metrics.json')));
    assert.equal(report.sha, receipt.sha, 'artifact-source-mismatch');
    assert.equal(report.requestId, receipt.requestId, 'artifact-request-mismatch');
    assert.equal(report.profile, receipt.profile, 'artifact-profile-mismatch');
    console.log(`Report: ${path.join(output, '测试.md')}`);
    if (run.conclusion === 'success') require('../../tests/services/performance/report').verify(report, receipt.sha);
    else process.exitCode = 1;
}
if (require.main === module) {
    try { main(process.argv.slice(2)); }
    catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = { options, validateRun };
