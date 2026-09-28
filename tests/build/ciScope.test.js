'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const test = require('node:test');
const { isDocumentationPath, selectScope } = require('../../scripts/ci/ciScope');

test('documentation scope accepts only known Markdown locations', /** 验证构建输入、未知路径不会归为纯文档。 */ () => {
    for (const file of ['README.md', 'WINDOWS_BUILD.md', 'docs/Status.md', 'docs/plans/Plan.md']) {
        assert.equal(isDocumentationPath(file), true, file);
    }
    for (const file of ['docs/tool.js', 'tests/README.md', 'AGENTS.md', 'CMakeLists.txt', '.github/workflows/ci.yml']) {
        assert.equal(isDocumentationPath(file), false, file);
    }
});

test('full lanes and unavailable comparison never skip builds', /** 验证 master、周检、手动及无法确定差异的事件保留完整构建。 */ () => {
    const sha = 'a'.repeat(40);
    const event = { before: sha, after: 'b'.repeat(40), pull_request: { base: { ref: 'master', sha }, head: { sha } } };
    const docs = /** 提供只改文档的合成 Git 输出。 */ () => 'README.md\0';
    for (const name of ['schedule', 'workflow_dispatch', 'pull_request']) {
        assert.equal(selectScope(name, 'refs/heads/develop', event, docs), false);
    }
    assert.equal(selectScope('push', 'refs/heads/master', event, docs), false);
    assert.equal(selectScope('pull_request', '', { pull_request: { base: { ref: 'develop', sha } } }, docs), false);
    assert.equal(selectScope('push', 'refs/heads/develop', { ...event, before: '0'.repeat(40) }, docs), false);
    assert.equal(selectScope('push', 'refs/heads/develop', event,
        /** 模拟提交对象不可用，要求保留构建。 */ () => { throw new Error('missing object'); }), false);
    assert.equal(selectScope('push', 'refs/heads/develop', event, /** 模拟空差异。 */ () => ''), false);
    assert.equal(selectScope('push', 'refs/heads/develop', event,
        /** 模拟文档和代码混合提交。 */ () => 'README.md\0chat/main.cpp\0'), false);
});

test('real Git history checks the whole PR and both sides of renames', /** 用隔离 Git 历史验证跨提交、目标分支前进、重命名与公开 CLI。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-ci-scope-'));
    const git = /** 在测试自建仓库中有界执行 Git。 */ args => execFileSync('git', args, {
        cwd: root, encoding: 'utf8', timeout: 10000
    });
    const commit = /** 提交测试自建文件并返回精确 SHA。 */ () => {
        git(['add', '.']); git(['commit', '-qm', 'fixture']); return git(['rev-parse', 'HEAD']).trim();
    };
    const event = /** 为指定两端提交生成 develop PR 事件。 */ (base, head) => ({ pull_request: {
        base: { ref: 'develop', sha: base }, head: { sha: head }
    } });
    try {
        git(['init', '-q']); git(['config', 'core.autocrlf', 'false']);
        git(['config', 'user.email', 'ci@example.invalid']); git(['config', 'user.name', 'CI fixture']);
        fs.writeFileSync(path.join(root, 'README.md'), 'baseline\n');
        const base = commit();
        fs.writeFileSync(path.join(root, 'README.md'), 'documentation\n');
        const docs = commit();
        assert.equal(selectScope('pull_request', '', event(base, docs), git), true);
        const eventPath = path.join(root, 'event.json');
        const output = path.join(root, 'output');
        fs.writeFileSync(eventPath, JSON.stringify(event(base, docs)));
        execFileSync(process.execPath, [path.resolve(__dirname, '../../scripts/ci/ciScope.js')], {
            cwd: root, timeout: 10000, env: { ...process.env, GITHUB_EVENT_NAME: 'pull_request',
                GITHUB_REF: 'refs/pull/1/merge', GITHUB_EVENT_PATH: eventPath, GITHUB_OUTPUT: output }
        });
        assert.equal(fs.readFileSync(output, 'utf8'), 'docs_only=true\n');
        fs.unlinkSync(eventPath); fs.unlinkSync(output);
        git(['checkout', '-q', '--detach', base]);
        fs.writeFileSync(path.join(root, 'target.cpp'), 'target change\n');
        const target = commit();
        assert.equal(selectScope('pull_request', '', event(target, docs), git), true);
        git(['checkout', '-q', '--detach', docs]);
        git(['mv', 'README.md', 'program.cpp']);
        const renamed = commit();
        assert.equal(selectScope('pull_request', '', event(base, renamed), git), false);
        fs.writeFileSync(path.join(root, 'README.md'), 'last commit is documentation\n');
        const mixed = commit();
        assert.equal(selectScope('pull_request', '', event(base, mixed), git), false);
        assert.equal(selectScope('push', 'refs/heads/develop', { before: renamed, after: mixed }, git), true);
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});
