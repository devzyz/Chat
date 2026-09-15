"""Single hosted build invocation, using the existing Windows build entry point."""
import json
import os
import re
from pathlib import Path
import subprocess

from github_release import GitHub, preflight, reserve
from release_gate import json_bytes, require, validate_identity
from stage_candidate import source_locks, stage


BASELINE = 'fc3be1ebea7eaeb3071fe716ac65713af1f3a146'


def hosted_root():
    require(os.environ.get('GITHUB_ACTIONS') == 'true' and
            os.environ.get('RUNNER_ENVIRONMENT') == 'github-hosted' and
            os.environ.get('RUNNER_OS') == 'Windows', 'hosted-windows-only')
    require(os.environ.get('GITHUB_REF') == 'refs/heads/master', 'release-source-must-be-master')
    run_id = os.environ.get('GITHUB_RUN_ID', '')
    require(run_id.isdigit(), 'missing-run-identity')
    return Path(os.environ['RUNNER_TEMP']).resolve() / ('chat-release-' + run_id)


def invoke(command, *, cwd=None, log=None, timeout=120):
    if log:
        with Path(log).open('xb') as stream:
            result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=timeout, check=False)
    else:
        result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=timeout, check=False)
    require(result.returncode == 0, 'release-command-failed')
    return '' if log else result.stdout.decode('utf-8', errors='replace').strip()


def register(version, admission_run):
    owned = hosted_root()
    identity = {'version': version, 'sourceSha': os.environ['GITHUB_SHA'],
                'workflowSha': os.environ['GITHUB_WORKFLOW_SHA'], 'repository': os.environ['GITHUB_REPOSITORY'],
                'runId': int(os.environ['GITHUB_RUN_ID']), 'runAttempt': int(os.environ['GITHUB_RUN_ATTEMPT'])}
    validate_identity(identity)
    source = Path(os.environ['GITHUB_WORKSPACE'])
    tool_lock = json.loads((source / 'scripts/release/toolchain-lock.json').read_text())
    require(os.environ['ImageVersion'] == tool_lock['tools']['runnerImage'], 'upstream-runner-image-drift')
    require(invoke(['git', 'rev-parse', 'HEAD'], cwd=source) == identity['sourceSha'], 'checkout-source-mismatch')
    api = GitHub(identity['repository'])
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    settings = json.loads(event.get('inputs', {}).get('settings_receipt', 'null'))
    require(isinstance(settings, dict), 'hosted-settings-receipt-required')
    receipt = preflight(api, identity, admission_run, settings_receipt=settings, event=event)
    receipt['sourceTreeSha'] = invoke(['git', 'rev-parse', 'HEAD^{tree}'], cwd=source)
    receipt['lockHashes'] = source_locks(source)
    # A failed POST or any later failure cannot authorize deleting the durable reservation.
    receipt['deploymentId'] = reserve(api, identity, settings_receipt=settings)
    receipt['settingsReceipt'] = settings
    owned.mkdir()
    (owned / 'receipt.json').write_bytes(json_bytes(receipt))
    return {'deploymentId': receipt['deploymentId'], 'status': 'RESERVED'}


def build():
    owned = hosted_root()
    receipt = json.loads((owned / 'receipt.json').read_text(encoding='utf-8'))
    identity = receipt['identity']
    require(identity['runId'] == int(os.environ['GITHUB_RUN_ID']) and
            identity['runAttempt'] == int(os.environ['GITHUB_RUN_ATTEMPT']) == 1 and
            identity['sourceSha'] == os.environ['GITHUB_SHA'], 'build-run-mismatch')
    record = GitHub(identity['repository']).request('deployments/' + str(receipt['deploymentId']))
    payload = record.get('payload', {})
    if isinstance(payload, str):
        payload = json.loads(payload)
    require(record.get('sha') == identity['sourceSha'] and payload.get('identity') == identity,
            'build-record-mismatch')
    with (owned / 'build-started').open('x') as stream:
        stream.write('one invocation; never retry\n')
    source = Path(os.environ['GITHUB_WORKSPACE'])
    require(source_locks(source) == receipt['lockHashes'], 'source-lock-changed')
    require(invoke(['git', 'rev-parse', 'HEAD'], cwd=source) == identity['sourceSha'], 'checkout-source-mismatch')
    vcpkg = source / '.ci/vcpkg'
    require(invoke(['git', 'rev-parse', 'HEAD'], cwd=vcpkg) == BASELINE, 'vcpkg-source-mismatch')
    installed = source / '.ci/vcpkg_installed'
    qt = Path(os.environ['QT_ROOT_DIR'])
    mingw = list((qt.parent.parent / 'Tools').glob('mingw*_64'))
    require(len(mingw) == 1, 'ambiguous-mingw-toolchain')
    bundled_ninja = qt.parent.parent / 'Tools/Ninja/ninja.exe'
    ninja = str(bundled_ninja) if bundled_ninja.is_file() else 'ninja.exe'
    tools = {'runnerImage': os.environ['ImageVersion'],
             'qt': invoke([str(qt / 'bin/qmake.exe'), '-query', 'QT_VERSION']),
             'mingw': invoke([str(mingw[0] / 'bin/g++.exe'), '-dumpfullversion', '-dumpversion']),
             'cmake': invoke(['cmake.exe', '--version']).splitlines()[0],
             'ninja': invoke([ninja, '--version']), 'node': invoke(['node.exe', '--version']),
             'npm': invoke(['npm.cmd', '--version']), 'vcpkgBaseline': BASELINE,
             'vcpkgTriplet': 'x64-windows-chat-release',
             'checkoutAction': '11bd71901bbe5b1630ceea73d27597364c9af683',
             'uploadAction': 'ea165f8d65b6e75b540449e92b4886f43607fa02',
             'qtAction': '48d3ad6db93f3627c8ee7a0454bc6f3744f7e730',
             'nodeAction': '49933ea5288caeca8642d1e84afbd3f7d6820020'}
    require(tools['qt'] == '6.5.3' and tools['mingw'] == '11.2.0' and tools['node'].startswith('v22.'),
            'upstream-toolchain-mismatch')
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    tools['visualStudio'] = invoke([str(vswhere), '-latest', '-products', '*', '-property', 'installationVersion'])
    visual_studio = Path(invoke([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath']))
    crt_roots = list((visual_studio / 'VC/Redist/MSVC').glob('*/x64/Microsoft.VC143.CRT'))
    require(bool(crt_roots), 'msvc-redistributable-unavailable')
    crt = max(crt_roots, key=lambda path: tuple(int(part) for part in path.parents[1].name.split('.')))
    msbuild = invoke([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild',
                      '-find', 'MSBuild\\**\\Bin\\MSBuild.exe']).splitlines()
    require(bool(msbuild), 'msbuild-unavailable')
    msbuild_version = re.search(r'MSBuild version ([0-9a-z.+]+)', invoke([msbuild[0], '-version']))
    require(msbuild_version is not None, 'msbuild-version-unavailable')
    tools['msbuild'] = msbuild_version.group(1)
    tool_lock = json.loads((source / 'scripts/release/toolchain-lock.json').read_text())
    require(tools == tool_lock['tools'], 'accepted-toolchain-drift')
    entry = ['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
             '-File', str(source / 'scripts/windows-local.ps1')]
    server = ['-Configuration', 'Release', '-VcpkgRoot', str(vcpkg), '-VcpkgInstalledRoot', str(installed),
              '-VcpkgBuildtreesRoot', str(owned / 'buildtrees'), '-VcpkgPackagesRoot', str(owned / 'packages'),
              '-ServerIntermediateRoot', str(owned / 'server-msbuild'),
              '-ServerTriplet', 'x64-windows-chat-release', '-ServerHostTriplet', 'x64-windows-chat-release']
    invoke(entry + ['-Task', 'RestoreServers'] + server, cwd=source, log=owned / 'restore.log', timeout=10800)
    invoke(entry + ['-Task', 'BuildServers'] + server, cwd=source, log=owned / 'servers.log', timeout=3600)
    invoke(entry + ['-Task', 'BuildClient', '-Configuration', 'Release', '-QtRoot', str(qt),
                    '-MinGwRoot', str(mingw[0])], cwd=source, log=owned / 'client.log', timeout=3600)
    invoke(['npm.cmd', 'ci', '--ignore-scripts', '--omit=dev'], cwd=source / 'VarifyServer',
           log=owned / 'npm-ci.log', timeout=600)
    invoke(['npm.cmd', 'ls', '--omit=dev', '--all', '--json'], cwd=source / 'VarifyServer',
           log=owned / 'npm-tree.log', timeout=120)
    node = invoke(['node.exe', '-p', 'process.execPath'])
    hashes = stage(source, owned, receipt, tools, installed, node, qt, crt)
    invoke(['git', 'diff', '--exit-code'], cwd=source)
    result = {'identity': identity, 'payload': hashes, 'buildInvocations': 1, 'status': 'BUILT'}
    invoke(['python', str(source / 'tests/release/contracts/run_contracts.py')], cwd=source,
           log=owned / 'contracts.log', timeout=120)
    evidence = owned / 'build-evidence'
    report = evidence / 'build/test-results/release_candidate_build.xml'
    report.parent.mkdir(parents=True)
    import shutil
    from release_gate import read_archive
    shutil.copyfile(source / 'build/test-results/release_candidate_contracts.xml', report)
    (evidence / 'candidate-build.json').write_bytes(json_bytes(result))
    payload = owned / 'candidate' / hashes['payloadName']
    (evidence / 'release-manifest.json').write_bytes(read_archive(payload)['release-manifest.json'])
    return result
