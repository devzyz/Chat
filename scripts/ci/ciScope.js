'use strict';
const fs = require('node:fs');
const { execFileSync } = require('node:child_process');

/** 仅将明确的文档路径归为轻量检查；未知文件仍要求构建。 */
function isDocumentationPath(file) {
    return /^(?:README\.md|WINDOWS_BUILD\.md|docs\/.+\.md)$/.test(file);
}

/** 比较完整 PR 或 push 范围；无法确定范围时保留全部回归。 */
function selectScope(eventName, ref, event, git) {
    const pullRequest = eventName === 'pull_request' && event.pull_request?.base?.ref === 'develop';
    const push = eventName === 'push' && ref === 'refs/heads/develop';
    if (!pullRequest && !push) return false;
    let base = pullRequest ? event.pull_request.base.sha : event.before;
    const head = pullRequest ? event.pull_request.head?.sha : event.after;
    if (![base, head].every(/** 拒绝缺失、零值或非提交标识的事件输入。 */ sha =>
        typeof sha === 'string' && /^[a-f0-9]{40}$/.test(sha) && !/^0+$/.test(sha))) return false;
    try {
        if (pullRequest) base = git(['merge-base', base, head]).trim();
        const files = git(['diff', '--name-only', '--no-renames', '-z', base, head, '--']).split('\0').filter(Boolean);
        return files.length > 0 && files.every(isDocumentationPath);
    } catch {
        return false;
    }
}

if (require.main === module) {
    const event = JSON.parse(fs.readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
    const docsOnly = selectScope(process.env.GITHUB_EVENT_NAME, process.env.GITHUB_REF, event,
        /** 有界读取 Git 提交差异，参数不经过 shell。 */ args => execFileSync('git', args, {
            encoding: 'utf8', timeout: 10000, maxBuffer: 8 * 1024 * 1024
        }));
    fs.appendFileSync(process.env.GITHUB_OUTPUT, `docs_only=${docsOnly}\n`);
    console.log(docsOnly ? 'Documentation-only develop change: static checks remain required.' : 'Build and regression required.');
}

module.exports = { isDocumentationPath, selectScope };
