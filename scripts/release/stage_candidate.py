"""Stage only declared build outputs. No restore, application build, or GitHub publication here."""
import json
from pathlib import Path
import subprocess

from release_gate import digest, file_inventory, json_bytes, pack, regular_files, require, TARGETS


def source_locks(source):
    names = ['vcpkg.json', 'VarifyServer/package-lock.json', 'VarifyServer/package.json',
             'triplets/x64-windows-chat-release.cmake', 'schema/manifest.json', 'scripts/release/toolchain-lock.json']
    names += ['proto/' + name + '.proto' for name in ('varify', 'status', 'chat')]
    migration = json.loads((source / 'schema/manifest.json').read_text(encoding='utf-8'))
    names += ['schema/' + entry['file'] for entry in migration['migrations']]
    return {name: digest((source / name).read_bytes()) for name in names}


def blank_config(file):
    if file.suffix == '.json':
        def blank(value):
            return {key: blank(child) for key, child in value.items()} if isinstance(value, dict) else None
        return json_bytes(blank(json.loads(file.read_text(encoding='utf-8'))))
    lines = ['; Fill every value for the target installation. Never package runtime credentials.']
    for line in file.read_text(encoding='utf-8-sig').splitlines():
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            lines.append(stripped)
        elif '=' in stripped and not stripped.startswith((';', '#')):
            lines.append(stripped.split('=', 1)[0].strip() + '=')
    return ('\n'.join(lines) + '\n').encode('utf-8')


def stage(source, owned, receipt, tools, installed, node, qt, crt):
    source, owned, installed = Path(source), Path(owned), Path(installed)
    require(source_locks(source) == receipt['lockHashes'], 'source-lock-changed')
    stage_root = owned / 'stage'
    stage_root.mkdir()
    allowed = {}

    def put(relative, data, role):
        from release_gate import safe_path
        safe_path(relative)
        require(relative not in allowed, 'duplicate-stage-file')
        target = stage_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
        allowed[relative] = role

    def copy(file, relative, role):
        require(not file.is_symlink() and file.is_file(), 'missing-or-linked-build-output')
        put(relative, file.read_bytes(), role)

    for app in TARGETS[:3]:
        folder = source / 'build/windows-servers/Release' / app
        for name in regular_files(folder):
            if '/' not in name and (name.endswith('.dll') or name == app + '.exe'):
                copy(folder / name, app + '/' + name, 'runtime')
        for file in sorted(Path(crt).glob('*.dll')):
            name = app + '/' + file.name.lower()
            if name in allowed:
                require((stage_root / name).read_bytes() == file.read_bytes(), 'conflicting-msvc-runtime')
            else:
                copy(file, name, 'runtime')
        config = source / app / app / 'config.ini'
        put(app + '/config.ini.template', blank_config(config), 'config')
    for name in ('chat-01.ini', 'chat-02.ini'):
        config = source / 'ChatServer/ChatServer/configs' / name
        put('ChatServer/configs/' + name + '.template', blank_config(config), 'config')

    client = source / 'build/windows-client/Release'
    copy(client / 'chat.exe', 'chat-client/chat.exe', 'runtime')
    for name in regular_files(client / 'static'):
        copy(client / 'static' / name, 'chat-client/static/' + name, 'runtime')
    put('chat-client/config.ini.template', blank_config(source / 'chat/config.ini'), 'config')
    deploy = subprocess.run([str(Path(qt) / 'bin/windeployqt.exe'), '--compiler-runtime', '--no-translations',
                             str(stage_root / 'chat-client/chat.exe')], capture_output=True, timeout=300, check=False)
    require(deploy.returncode == 0, 'qt-runtime-deployment-failed')
    for name in regular_files(stage_root / 'chat-client'):
        relative = 'chat-client/' + name
        if relative not in allowed:
            require(name.endswith('.dll'), 'unexpected-qt-runtime-output')
            allowed[relative] = 'runtime'

    varify = source / 'VarifyServer'
    for file in sorted(varify.glob('*.js')):
        copy(file, 'VarifyServer/' + file.name, 'runtime')
    for name in ('package.json', 'package-lock.json'):
        copy(varify / name, 'VarifyServer/' + name, 'runtime')
    put('VarifyServer/config.json.template', blank_config(varify / 'config.json'), 'config')
    copy(Path(node), 'VarifyServer/node.exe', 'runtime')
    npm = json.loads((varify / 'package-lock.json').read_text(encoding='utf-8'))
    packages = []
    for name, package in sorted(npm['packages'].items()):
        if not name or package.get('dev'):
            continue
        require(name.startswith('node_modules/'), 'unexpected-npm-lock-path')
        actual = json.loads((varify / name / 'package.json').read_text(encoding='utf-8'))
        require(actual['version'] == package['version'], 'npm-package-version-drift')
        packages.append({'path': name, 'version': package['version'], 'integrity': package.get('integrity', ''),
                         'license': package.get('license', 'UNDECLARED')})
    for name in regular_files(varify / 'node_modules'):
        copy(varify / 'node_modules' / name, 'VarifyServer/node_modules/' + name, 'dependency')
    for name in ('varify', 'status', 'chat'):
        copy(source / 'proto' / (name + '.proto'), 'proto/' + name + '.proto', 'protocol')
    migration = json.loads((source / 'schema/manifest.json').read_text(encoding='utf-8'))
    copy(source / 'schema/manifest.json', 'migrations/manifest.json', 'migration')
    for entry in migration['migrations']:
        file = source / 'schema' / entry['file']
        require(digest(file.read_bytes()) == entry['checksum'], 'migration-source-drift')
        copy(file, 'migrations/' + entry['file'], 'migration')
    for name in ('release_gate.py', 'release_cli.py', 'release.ps1', 'ReleaseGate.psm1', 'payload-layout.json',
                 'toolchain-lock.json'):
        copy(source / 'scripts/release' / name, 'release-tools/' + name, 'tool')
    status = installed / 'vcpkg/status'
    require(status.is_file(), 'vcpkg-status-missing')
    for file in sorted((installed / 'x64-windows-chat-release/share').glob('*/copyright')):
        copy(file, 'licenses/vcpkg/' + file.parent.name + '.txt', 'license')
    require(any(role == 'license' for role in allowed.values()), 'dependency-license-inventory-missing')
    inventory = {'format': 1, 'vcpkgStatusSha256': digest(status.read_bytes()),
                 'vcpkgManifest': json.loads((source / 'vcpkg.json').read_text(encoding='utf-8')),
                 'vcpkgStatus': status.read_bytes().decode('utf-8'), 'npmPackages': packages,
                 'runtimeFiles': {name: info for name, info in file_inventory(stage_root, allowed).items()
                                  if name.endswith(('.exe', '.dll'))},
                 'tools': tools, 'lockHashes': receipt['lockHashes'],
                 'licenseFiles': sorted(name for name, role in allowed.items() if role == 'license'),
                 'provenance': {'qt': 'https://doc.qt.io/qt-6/licensing.html',
                                'node': 'https://github.com/nodejs/node/blob/main/LICENSE'}}
    put('dependency-inventory.json', json_bytes(inventory), 'inventory')
    context = {key: receipt[key] for key in ('identity', 'deploymentId', 'sourceTreeSha', 'lockHashes',
                                            'upstream', 'compatibility')}
    context.update(tools=tools, buildInvocations={target: 1 for target in TARGETS})
    require(source_locks(source) == receipt['lockHashes'], 'source-lock-changed')
    candidate = owned / 'candidate'
    candidate.mkdir()
    payload = candidate / ('Chat-' + receipt['identity']['version'] + '-windows-x64.zip')
    hashes = pack(stage_root, allowed, context, payload)
    (candidate / 'payload-hashes.json').write_bytes(json_bytes(hashes))
    return hashes
