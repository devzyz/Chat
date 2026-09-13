'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { runCommand } = require('./dependencyCoordinator');
const lock = require('./services.lock.json');
const { reportGroups } = require('./serviceReports');

// Also called by an always() workflow step if npm/configure/CTest never started.
// Missing evidence is a failure, never a synthetic successful test run.
const root = process.argv[2];
fs.mkdirSync(root, { recursive: true });
function read(name) {
    try { return JSON.parse(fs.readFileSync(path.join(root, name), 'utf8')); }
    catch { return { complete: false }; }
}
async function finalize() {
    const phase3d = ['3D-00', '3D-01'].includes(process.env.CHAT_SERVICE_SELECTOR);
    const teardown = read('teardown.json');
    const processTeardown = read('process-teardown.json');
    const junit = path.join(root, phase3d ? 'linux_phase3d_contract.xml' : 'linux_services.xml');
    const reports = phase3d ? require('./phase3dEvidence').reportGroups(process.env.CHAT_SERVICE_SELECTOR) : reportGroups(process.env.CHAT_SERVICE_SELECTOR || '3C-02');
    let reportsComplete = reports.every((group) => {
        try {
            const report = fs.readFileSync(path.join(root, group.file), 'utf8');
            return report.includes('<testsuite ') && !/<(?:failure|error)\b/.test(report) &&
                !/\b(?:failures|errors)="[1-9][0-9]*"/.test(report);
        } catch { return false; }
    });
    if (phase3d) {
        try { require('./phase3dEvidence').validate(root, process.env.CHAT_CANDIDATE_SHA, process.env.CHAT_SERVICE_SELECTOR); }
        catch { reportsComplete = false; }
    }
    teardown.processComplete = processTeardown.complete === true;
    teardown.complete = teardown.complete === true && teardown.processComplete && reportsComplete &&
        fs.existsSync(path.join(root, phase3d ? 'topology.json' : 'service-endpoints.json')) &&
        (!phase3d || (read('application-teardown.json').complete === true && read('redaction.json').complete === true));
    if (!teardown.complete && process.env.GITHUB_ACTIONS === 'true') {
        teardown.fallbackStopped = [];
        teardown.fallbackFailures = [];
        for (const service of ['mailpit', 'mysql', 'redis']) {
            const id = process.env[`CHAT_${service.toUpperCase()}_CONTAINER`] || '';
            try {
                if (!/^[a-f0-9]{64}$/.test(id)) throw new Error('missing job-owned identity');
                const [container] = JSON.parse(await runCommand('docker', ['inspect', id]));
                if (container.Id !== id || container.Config.Image !== lock.services[service].image) {
                    throw new Error('identity mismatch');
                }
                if (container.State.Running) {
                    await runCommand('docker', ['stop', '--time', '10', id], { timeout: 15000 });
                }
                const [stopped] = JSON.parse(await runCommand('docker', ['inspect', id]));
                if (stopped.State.Running) throw new Error('stop incomplete');
                teardown.fallbackStopped.push(service);
            } catch { teardown.fallbackFailures.push(service); }
        }
    }
    fs.writeFileSync(path.join(root, 'teardown.json'), JSON.stringify(teardown, null, 2));
    if (!phase3d && !fs.existsSync(path.join(root, 'service-endpoints.json'))) {
        fs.writeFileSync(path.join(root, 'service-endpoints.json'), '{"status":"not-started"}\n');
    }
    if (!fs.existsSync(junit) || !teardown.complete) {
        // Preserve the coordinator's original failure report alongside outer failure.
        if (fs.existsSync(junit) && !fs.existsSync(path.join(root, 'coordinator-services.xml'))) {
            fs.copyFileSync(junit, path.join(root, 'coordinator-services.xml'));
        }
        fs.writeFileSync(junit, `<testsuite name="${phase3d ? 'phase3d-contract' : 'phase3c-services'}" tests="1" failures="1">` +
            '<testcase name="outer-run-and-teardown"><failure message="service evidence incomplete"/>' +
            '</testcase></testsuite>\n');
    }
    if (!teardown.complete) process.exitCode = 1;
}
finalize().catch(() => { process.stderr.write('outer service cleanup failed\n'); process.exitCode = 1; });
