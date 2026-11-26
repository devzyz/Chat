'use strict';
const fs = require('node:fs');
const profiles = require('../../tests/services/performance/profiles.json');

/** 将 GitHub 手动输入规范化为布尔值，拒绝无法识别的值。 */
function readBoolean(value) {
    if (value === undefined || value === false || value === 'false') return false;
    if (value === true || value === 'true') return true;
    throw new Error('Invalid boolean CI input');
}

/** 集中选择快速或全量模式；专项请求提升为全量，刷新仅允许默认分支。 */
function selectPolicy(eventName, ref, event) {
    const manual = eventName === 'workflow_dispatch';
    const inputs = manual ? (event.inputs || {}) : {};
    const requested = inputs.mode === undefined ? 'full' : inputs.mode;
    if (!['quick', 'full', 'performance'].includes(requested)) throw new Error('CI mode must be quick, full or performance');
    if (requested === 'performance') {
        // 拒绝性能模式混用其他专项。
        if (!manual || ['refresh_tools', 'cold_linux', 'real_acceptance'].some(key => readBoolean(inputs[key]))) {
            throw new Error('Performance requires an isolated manual request');
        }
        const profile = inputs.performance_profile || 'baseline';
        if (!Object.hasOwn(profiles, profile)) throw new Error('Invalid performance profile');
        return { mode: 'performance', performance_profile: profile, refresh_tools: false,
            cold_linux: false, real_acceptance: false, promoted: false };
    }
    const refreshTools = eventName === 'schedule' || readBoolean(inputs.refresh_tools);
    const coldLinux = readBoolean(inputs.cold_linux);
    const realAcceptance = readBoolean(inputs.real_acceptance);
    if (refreshTools && (!event.repository?.default_branch ||
        ref !== `refs/heads/${event.repository.default_branch}`)) {
        throw new Error('Tool refresh requires the default branch');
    }
    let mode;
    if (manual) mode = requested;
    else if (eventName === 'schedule') mode = 'full';
    else if (eventName === 'pull_request' && ['develop', 'master'].includes(event.pull_request?.base?.ref)) {
        mode = event.pull_request.base.ref === 'master' ? 'full' : 'quick';
    } else if (eventName === 'push' && ['refs/heads/develop', 'refs/heads/master'].includes(ref)) {
        mode = ref === 'refs/heads/master' ? 'full' : 'quick';
    } else throw new Error('Unsupported CI event or branch');
    const promoted = mode === 'quick' && (refreshTools || coldLinux || realAcceptance);
    if (promoted) mode = 'full';
    return { mode, refresh_tools: refreshTools, cold_linux: coldLinux, real_acceptance: realAcceptance, promoted };
}

/** 在运行开始时固定部署选择，后续开关变化只影响下一次运行。 */
function selectAutodl(eventName, ref, mode, requested = 'skip') {
    if (mode === 'performance') return 'skip';
    const develop = ref === 'refs/heads/develop';
    if (eventName !== 'workflow_dispatch') return 'skip';
    if (requested === 'package' || requested === 'skip') return requested;
    if (requested === 'deploy' && develop) return 'deploy';
    throw new Error('AutoDL deployment requires a develop dispatch with a supported operation');
}

/** 说明所选模式的验证边界；develop 文档范围由后续静态作业根据完整差异确定。 */
function describeCoverage(policy, eventName, ref) {
    if (policy.mode === 'performance') {
        return 'Selected: **09 Performance** (' + policy.performance_profile +
            ') with dedicated 01 framework checks, 02/03 Linux runtime build and 10 evidence validation.';
    }
    const common = 'Windows: 01 static checks, 02 infrastructure, 03 build, 04 component regression and default PostgreSQL storage, ' +
        '05 instance scripts, 08 packaging contracts and 10 report validation.';
    if (policy.mode === 'quick') {
        return common + ' Automatic develop documentation scope is resolved in the static job. ' +
            'Linux integration, business E2E, application ZIPs and performance are outside quick scope.';
    }
    return common + ' Full adds Linux build/lifecycle, 06 real dependencies, 07 two-server E2E and 08 application ZIPs. ' +
        'Four-account acceptance: ' + (policy.real_acceptance ? 'selected' : 'not requested') +
        '. Formal publication is owned by the stable-tag Release workflow.';
}

if (require.main === module) {
    try {
        const event = JSON.parse(fs.readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
        const policy = selectPolicy(process.env.GITHUB_EVENT_NAME, process.env.GITHUB_REF, event);
        policy.autodl = selectAutodl(process.env.GITHUB_EVENT_NAME, process.env.GITHUB_REF, policy.mode,
            event.inputs?.autodl);
        // 序列化固定策略字段为 GitHub 输出。
        const output = Object.entries(policy).map(
            ([key, value]) => `${key}=${value}\n`).join('');
        fs.appendFileSync(process.env.GITHUB_OUTPUT, output);
        const summary = `CI mode: **${policy.mode}**. AutoDL: **${policy.autodl}**.\n` +
            (policy.promoted ? 'Requested quick mode was promoted to full by an optional acceptance or maintenance flag.\n' : '') +
            `Windows tool refresh: ${policy.refresh_tools}; Linux cold restore: ${policy.cold_linux}; ` +
            `real acceptance: ${policy.real_acceptance}.\n\n` +
            describeCoverage(policy, process.env.GITHUB_EVENT_NAME, process.env.GITHUB_REF) + '\n';
        if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, summary);
        console.log(summary);
    } catch (error) { console.error(error.message); process.exitCode = 1; }
}

module.exports = { selectPolicy, selectAutodl };
