'use strict';
const fs = require('node:fs');

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
        if (!manual || ['refresh_tools', 'cold_linux', 'real_acceptance'].some(/** 拒绝性能模式混用其他专项。 */ key => readBoolean(inputs[key]))) {
            throw new Error('Performance requires an isolated manual request');
        }
        const profile = inputs.performance_profile || 'baseline';
        if (!['smoke', 'baseline', 'stress'].includes(profile)) throw new Error('Invalid performance profile');
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

if (require.main === module) {
    try {
        const event = JSON.parse(fs.readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
        const policy = selectPolicy(process.env.GITHUB_EVENT_NAME, process.env.GITHUB_REF, event);
        const output = Object.entries(policy).map(/** 序列化固定策略字段为 GitHub 输出。 */
            ([key, value]) => `${key}=${value}\n`).join('');
        fs.appendFileSync(process.env.GITHUB_OUTPUT, output);
        const summary = `CI mode: **${policy.mode}**.\n` +
            (policy.promoted ? 'Requested quick mode was promoted to full by an optional acceptance or maintenance flag.\n' : '') +
            `Windows tool refresh: ${policy.refresh_tools}; Linux cold restore: ${policy.cold_linux}; ` +
            `real acceptance: ${policy.real_acceptance}.\n`;
        if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, summary);
        console.log(summary);
    } catch (error) { console.error(error.message); process.exitCode = 1; }
}

module.exports = { selectPolicy };
