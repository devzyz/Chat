'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const test = require('node:test');
const { spawnSync } = require('node:child_process');
const { selectPolicy } = require('../../scripts/ci/ciPolicy');

/** 构造独立事件数据，不访问 GitHub 或业务依赖。 */
function event(inputs = {}, base = 'develop') {
    return { inputs, repository: { default_branch: 'develop' }, pull_request: { base: { ref: base } } };
}

test('automatic events select quick or full and ignore manual flags', /** 验证自动事件矩阵与手动输入隔离。 */ () => {
    for (const [name, ref, base, mode, refresh] of [
        ['pull_request', 'refs/pull/7/merge', 'develop', 'quick', false],
        ['pull_request', 'refs/pull/7/merge', 'master', 'full', false],
        ['push', 'refs/heads/develop', '', 'quick', false],
        ['push', 'refs/heads/master', '', 'full', false],
        ['schedule', 'refs/heads/develop', '', 'full', true]
    ]) {
        assert.deepEqual(selectPolicy(name, ref, event({ mode: 'invalid', cold_linux: true, real_acceptance: true }, base)),
            { mode, refresh_tools: refresh, cold_linux: false, real_acceptance: false, promoted: false });
    }
});

test('manual modes default to full and optional flags promote quick to full', /** 验证各手动模式、布尔格式及全部专项组合。 */ () => {
    for (const ref of ['refs/heads/develop', 'refs/heads/master', 'refs/heads/feature']) {
        assert.equal(selectPolicy('workflow_dispatch', ref, event()).mode, 'full');
        for (const mode of ['quick', 'full']) {
            for (let mask = 0; mask < 8; mask++) {
                for (const strings of [false, true]) {
                    const flags = { refresh_tools: !!(mask & 1), cold_linux: !!(mask & 2), real_acceptance: !!(mask & 4) };
                    const inputs = { mode, ...Object.fromEntries(Object.entries(flags).map(
                        /** 同时覆盖事件 JSON 字符串与规范化布尔输入。 */ ([key, value]) => [key, strings ? String(value) : value])) };
                    if (flags.refresh_tools && ref !== 'refs/heads/develop') {
                        assert.throws(/** 检查其他分支刷新被拒绝。 */ () => selectPolicy('workflow_dispatch', ref, event(inputs)),
                            /default branch/);
                    } else {
                        assert.deepEqual(selectPolicy('workflow_dispatch', ref, event(inputs)), {
                            mode: mask ? 'full' : mode, ...flags, promoted: mode === 'quick' && mask !== 0
                        });
                    }
                }
            }
        }
    }
});

test('invalid or unsupported policy inputs fail closed', /** 验证非法事件、模式、布尔及缺失默认分支不会放行。 */ () => {
    for (const mode of ['', 'fast', 'FULL', null]) {
        assert.throws(/** 拒绝非法模式。 */ () => selectPolicy('workflow_dispatch', 'refs/heads/develop', event({ mode })), /mode/);
    }
    for (const flag of ['refresh_tools', 'cold_linux', 'real_acceptance']) {
        assert.throws(/** 拒绝非布尔专项输入。 */ () => selectPolicy('workflow_dispatch', 'refs/heads/develop',
            event({ [flag]: 'yes' })), /boolean/);
    }
    assert.throws(/** 拒绝不受支持的事件。 */ () => selectPolicy('workflow_run', '', event()), /Unsupported/);
    assert.throws(/** 拒绝未配置的推送分支。 */ () => selectPolicy('push', 'refs/heads/feature', event()), /Unsupported/);
    assert.throws(/** 拒绝无法确认默认分支的定时刷新。 */ () => selectPolicy('schedule', 'refs/heads/develop', {}), /default branch/);
});

test('policy CLI writes effective mode and promotion reason without external tools', /** 使用真实 CLI 校验输出、摘要与错误退出。 */ () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-ci-policy-'));
    try {
        const input = path.join(root, 'event.json');
        const output = path.join(root, 'output');
        const summary = path.join(root, 'summary');
        for (const [inputs, ok] of [[{ mode: 'quick', cold_linux: 'true' }, true], [{ mode: 'invalid' }, false]]) {
            fs.writeFileSync(input, JSON.stringify(event(inputs)));
            fs.writeFileSync(output, '');
            fs.writeFileSync(summary, '');
            const result = spawnSync(process.execPath, [path.resolve(__dirname, '../../scripts/ci/ciPolicy.js')], {
                env: { ...process.env, PATH: '', GITHUB_EVENT_NAME: 'workflow_dispatch', GITHUB_REF: 'refs/heads/develop',
                    GITHUB_EVENT_PATH: input, GITHUB_OUTPUT: output, GITHUB_STEP_SUMMARY: summary },
                encoding: 'utf8', timeout: 10000
            });
            assert.ifError(result.error);
            assert.equal(result.status === 0, ok, result.stderr);
            if (ok) {
                assert.match(fs.readFileSync(output, 'utf8'), /^mode=full\nrefresh_tools=false\ncold_linux=true\nreal_acceptance=false\npromoted=true\n$/);
                assert.match(fs.readFileSync(summary, 'utf8'), /quick mode was promoted to full/);
            } else {
                assert.equal(fs.readFileSync(output, 'utf8'), '');
                assert.equal(fs.readFileSync(summary, 'utf8'), '');
            }
        }
    } finally { fs.rmSync(root, { recursive: true, force: true }); }
});
