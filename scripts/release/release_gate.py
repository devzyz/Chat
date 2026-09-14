"""Release candidate identity and byte verification. No application build or secrets here."""
import hashlib
import json
from pathlib import Path
import re
import stat
import zipfile
from datetime import datetime
import tempfile

SHA = r'[a-f0-9]{40}'
DIGEST = r'[a-f0-9]{64}'
VERSION = r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.[1-9][0-9]*)?'
CHECKS = (
    'Static configuration checks', 'Server Release build', 'Qt client Release',
    'VarifyServer dependency and package check', 'Linux POSIX process lifecycle',
    'Linux configure compile link and startup preflight', 'Linux two-server foundation contract',
    'Phase 3C disposable services', 'Phase 3C downstream hard gate',
    'Phase 3D current-N release admission',
)
TARGETS = ('GateServer', 'StatusServer', 'ChatServer', 'chat-client', 'VarifyServer')
TOOLS = ('runnerImage', 'visualStudio', 'msbuild', 'cmake', 'ninja', 'qt', 'mingw', 'node', 'npm',
         'vcpkgBaseline', 'vcpkgTriplet', 'checkoutAction', 'uploadAction', 'qtAction', 'nodeAction')
FALLBACK = 'CONTROLLED_DEPENDENCY_INVENTORY_FALLBACK'
PRIVATE_BYTES = re.compile(rb'gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{20,}|'
                           rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----')


def require(condition, category):
    if not condition:
        raise ValueError(category)


def exact(value, keys):
    require(isinstance(value, dict) and set(value) == set(keys.split()), 'unexpected-fields')


def validate_identity(value):
    exact(value, 'version sourceSha runId runAttempt repository workflowSha')
    for field in ('version', 'sourceSha', 'workflowSha', 'repository'):
        require(isinstance(value[field], str), 'invalid-identity-type')
    require(re.fullmatch(VERSION, value['version']) is not None, 'invalid-version')
    require(re.fullmatch(SHA, value['sourceSha']) is not None, 'invalid-source')
    require(re.fullmatch(SHA, value['workflowSha']) is not None, 'invalid-workflow-source')
    require(type(value['runId']) is int and value['runId'] > 0, 'invalid-run')
    require(type(value['runAttempt']) is int and value['runAttempt'] == 1, 'retry-forbidden')
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', value['repository']) is not None, 'invalid-repository')


def check_unused(identity, deployments):
    validate_identity(identity)
    require(isinstance(deployments, list), 'invalid-deployment-history')
    for record in deployments:
        require(record.get('environment') != 'candidate-' + identity['version'], 'version-already-reserved')


def admit(identity, admission, checks, deployments):
    validate_identity(identity)
    require(admission.get('sourceSha') == identity['sourceSha'], 'admission-source-mismatch')
    require(admission.get('currentNPass') is True, 'current-n-not-accepted')
    require(admission.get('compatibility') in ('BOOTSTRAP_NO_PROMOTED_N_MINUS_1', 'PASS'),
            'compatibility-not-admitted')
    require(type(admission.get('caseCount')) is int and admission['caseCount'] > 0, 'empty-admission')
    for phase in ('phase3c', 'phase3d'):
        require(re.fullmatch(DIGEST, admission.get('manifests', {}).get(phase, '')) is not None,
                'missing-upstream-digest')
    for name in CHECKS:
        matching = [c for c in checks if c.get('name') == name and c.get('head_sha') == identity['sourceSha']]
        require(bool(matching), 'required-check-missing')
        latest = max(matching, key=lambda c: c['id'])
        require(latest.get('status') == 'completed' and latest.get('conclusion') == 'success',
                'required-check-not-successful')
    check_unused(identity, deployments)


def safe_path(name):
    require(isinstance(name, str) and 0 < len(name) <= 240, 'unsafe-path')
    require(all(32 <= ord(c) < 127 for c in name) and not any(c in name for c in '\\:*?"<>|'), 'unsafe-path')
    for part in name.split('/'):
        require(part not in ('', '.', '..') and not part.endswith((' ', '.')), 'unsafe-path')
        require(not re.fullmatch(r'(?i)(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])', part.split('.')[0]),
                'reserved-windows-path')
    return name


def public_metadata(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if '/' in key:
                safe_path(key)
            else:
                require(not re.search(r'(?i)password|secret|token|credential|connectionstring|environment', key),
                        'private-metadata-key')
            public_metadata(child)
    elif isinstance(value, list):
        for child in value:
            public_metadata(child)
    elif isinstance(value, str):
        require(not re.search(r'(?i)[a-z]:[\\/]|\\\\|gh[pousr]_[a-z0-9]{20}|github_pat_|'
                              r'-----BEGIN .*PRIVATE KEY|[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2}', value),
                'private-metadata-value')
        require(not value.startswith(('/home/', '/Users/', '/tmp/')), 'private-metadata-value')
    else:
        require(value is None or type(value) in (int, bool), 'unsupported-metadata-value')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + '\n').encode('utf-8')


def unique_paths(names):
    seen = set()
    for name in names:
        safe_path(name)
        require(name.lower() not in seen, 'case-colliding-path')
        seen.add(name.lower())
    for name in seen:
        parts = name.split('/')
        require(not any('/'.join(parts[:n]) in seen for n in range(1, len(parts))), 'file-directory-collision')


def regular_files(root):
    root = Path(root)
    require(root.is_dir() and not root.is_symlink() and not getattr(root, 'is_junction', lambda: False)(),
            'invalid-stage-root')
    names = []
    for file in root.rglob('*'):
        info = file.lstat()
        require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
                'linked-payload-file')
        if file.is_dir():
            safe_path(file.relative_to(root).as_posix())
            continue
        require(stat.S_ISREG(info.st_mode), 'non-regular-payload-file')
        names.append(file.relative_to(root).as_posix())
    unique_paths(names)
    return sorted(names)


def file_inventory(root, allowed):
    require(regular_files(root) == sorted(allowed), 'undeclared-or-missing-file')
    result = {}
    for name, role in sorted(allowed.items()):
        data = (Path(root) / name).read_bytes()
        result[name] = {'role': role, 'size': len(data), 'sha256': digest(data)}
    return result


def verify_files(root, files):
    require(file_inventory(root, {name: info['role'] for name, info in files.items()}) == files,
            'payload-byte-mismatch')


def read_archive(path, max_bytes=4 * 1024**3, max_files=100000):
    # Inspect the entire central directory before reading any payload or creating a destination.
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        require(0 < len(entries) <= max_files, 'archive-file-limit')
        names = [entry.filename for entry in entries if not entry.is_dir()]
        unique_paths(names)
        total = 0
        for entry in entries:
            safe_path(entry.filename.rstrip('/') if entry.is_dir() else entry.filename)
            mode = entry.external_attr >> 16
            require(not stat.S_ISLNK(mode) and not entry.flag_bits & 1 and not entry.external_attr & 0x400,
                    'linked-or-encrypted-archive')
            require(stat.S_IFMT(mode) in (0, stat.S_IFREG, stat.S_IFDIR), 'special-archive-file')
            total += entry.file_size
            require(total <= max_bytes and entry.file_size <= max(1, entry.compress_size) * 1000,
                    'archive-size-limit')
        return {entry.filename: archive.read(entry) for entry in entries if not entry.is_dir()}


def validate_context(context):
    exact(context, 'identity sourceTreeSha deploymentId lockHashes tools upstream compatibility buildInvocations')
    validate_identity(context['identity'])
    public_metadata(context)
    require(re.fullmatch(SHA, context['sourceTreeSha']) is not None, 'invalid-source-tree')
    require(type(context['deploymentId']) is int and context['deploymentId'] > 0, 'missing-build-record')
    require(context['buildInvocations'] == {target: 1 for target in TARGETS} and
            all(type(count) is int for count in context['buildInvocations'].values()), 'build-count-mismatch')
    exact(context['tools'], ' '.join(TOOLS))
    require(all(isinstance(v, str) and v.strip() for v in context['tools'].values()), 'missing-tool-identity')
    approved_tools = json.loads(Path(__file__).with_name('toolchain-lock.json').read_text(encoding='utf-8'))['tools']
    require(context['tools'] == approved_tools, 'accepted-toolchain-drift')
    require({'vcpkg.json', 'VarifyServer/package-lock.json'} <= set(context['lockHashes']), 'missing-lock')
    for name, value in context['lockHashes'].items():
        safe_path(name)
        require(re.fullmatch(DIGEST, value) is not None, 'invalid-lock-digest')
    exact(context['upstream'], 'phase3c phase3d')
    require(all(re.fullmatch(DIGEST, v) for v in context['upstream'].values()), 'invalid-upstream-digest')
    require(context['compatibility'] in ('BOOTSTRAP_NO_PROMOTED_N_MINUS_1', 'PASS'), 'invalid-compatibility')


def validate_layout(allowed):
    layout = json.loads(Path(__file__).with_name('payload-layout.json').read_text(encoding='utf-8'))
    unique_paths(allowed)
    require(all(allowed.get(name) == role for name, role in layout['required'].items()), 'required-role-missing')
    for name, role in allowed.items():
        require(layout['required'].get(name) == role or
                any(re.fullmatch(pattern, name) for pattern in layout['patterns'].get(role, [])),
                'unapproved-payload-path')


def validate_sbom(value):
    exact(value, 'status reason policy')
    require(value['status'] == FALLBACK and isinstance(value['reason'], str) and value['reason'].strip() and
            isinstance(value['policy'], str) and value['policy'].strip(), 'false-sbom-claim')
    public_metadata(value)


def validate_config(name, data):
    if name.endswith('.json.template'):
        def blank(value):
            if isinstance(value, dict):
                return all(blank(child) for child in value.values())
            return value is None or value == ''
        require(blank(json.loads(data)), 'nonblank-config-template')
    elif name.endswith('.ini.template'):
        for line in data.decode('utf-8-sig').splitlines():
            line = line.strip()
            if not line or line.startswith((';', '#')) or (line.startswith('[') and line.endswith(']')):
                continue
            require('=' in line and not line.split('=', 1)[1].strip(), 'nonblank-config-template')
    else:
        raise ValueError('unknown-config-template')


def validate_inventory(data, files, context):
    inventory = json.loads(data)
    exact(inventory, 'format vcpkgStatusSha256 vcpkgManifest vcpkgStatus npmPackages runtimeFiles tools '
                     'lockHashes licenseFiles provenance')
    require(type(inventory['format']) is int and inventory['format'] == 1 and inventory['tools'] == context['tools'] and
            inventory['lockHashes'] == context['lockHashes'], 'inventory-provenance-mismatch')
    require(isinstance(inventory['vcpkgStatus'], str) and 'Status: install ok installed' in inventory['vcpkgStatus'] and
            digest(inventory['vcpkgStatus'].encode('utf-8')) == inventory['vcpkgStatusSha256'], 'invalid-vcpkg-inventory')
    require(isinstance(inventory['vcpkgManifest'], dict) and inventory['vcpkgManifest'].get('dependencies'),
            'empty-vcpkg-manifest')
    require(inventory['runtimeFiles'] == {name: info for name, info in files.items() if name.endswith(('.exe', '.dll'))},
            'runtime-inventory-mismatch')
    require(inventory['licenseFiles'] == sorted(name for name, info in files.items() if info['role'] == 'license') and
            isinstance(inventory['provenance'], dict) and inventory['provenance'], 'license-provenance-mismatch')
    require(isinstance(inventory['npmPackages'], list) and inventory['npmPackages'], 'empty-npm-inventory')
    packages = []
    for package in inventory['npmPackages']:
        exact(package, 'path version integrity license')
        safe_path(package['path'])
        require(all(isinstance(value, str) and value for value in package.values()), 'invalid-npm-inventory')
        require('VarifyServer/' + package['path'] + '/package.json' in files, 'npm-inventory-file-missing')
        packages.append(package['path'])
    require(len(set(packages)) == len(packages), 'duplicate-npm-inventory')


def pack(stage, allowed, context, output):
    validate_context(context)
    validate_layout(allowed)
    files = file_inventory(stage, allowed)
    validate_inventory((Path(stage) / 'dependency-inventory.json').read_bytes(), files, context)
    for name, role in allowed.items():
        require(PRIVATE_BYTES.search((Path(stage) / name).read_bytes()) is None, 'private-payload-bytes')
        if role == 'config':
            validate_config(name, (Path(stage) / name).read_bytes())
    sbom = {'status': FALLBACK, 'reason': 'No repository-approved pinned SBOM generator',
            'policy': 'tests/plans/PHASE-RELEASE-PLAN.md#53-sbom--controlled-fallback'}
    manifest = {'format': 1, 'context': context, 'files': files, 'sbom': sbom}
    public_metadata(manifest)
    manifest_bytes = json_bytes(manifest)
    output = Path(output)
    expected = 'Chat-' + context['identity']['version'] + '-windows-x64.zip'
    require(output.name == expected, 'noncanonical-payload-name')
    # Exclusive creation reserves the local output even if compression fails. Never overwrite or repair it.
    with output.open('xb') as destination:
        with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, info in files.items():
                data = (Path(stage) / name).read_bytes()
                require(len(data) == info['size'] and digest(data) == info['sha256'], 'stage-changed-during-pack')
                archive.writestr(name, data)
            archive.writestr('release-manifest.json', manifest_bytes)
    result = {'payloadSha256': digest(output.read_bytes()), 'manifestSha256': digest(manifest_bytes),
              'payloadSize': output.stat().st_size, 'payloadName': output.name}
    verify_candidate(output, result['payloadSha256'], result['manifestSha256'])
    return result


def verify_candidate(payload, payload_sha, manifest_sha):
    require(digest(Path(payload).read_bytes()) == payload_sha, 'external-payload-digest-mismatch')
    contents = read_archive(payload)
    manifest_bytes = contents.pop('release-manifest.json', None)
    require(manifest_bytes is not None and digest(manifest_bytes) == manifest_sha, 'manifest-digest-mismatch')
    manifest = json.loads(manifest_bytes)
    exact(manifest, 'format context files sbom')
    require(type(manifest['format']) is int and manifest['format'] == 1, 'unknown-manifest-format')
    validate_context(manifest['context'])
    validate_sbom(manifest['sbom'])
    public_metadata(manifest)
    validate_layout({name: info['role'] for name, info in manifest['files'].items()})
    require('dependency-inventory.json' in contents, 'dependency-inventory-missing')
    validate_inventory(contents['dependency-inventory.json'], manifest['files'], manifest['context'])
    require(set(contents) == set(manifest['files']), 'undeclared-or-missing-file')
    for name, data in contents.items():
        require(PRIVATE_BYTES.search(data) is None, 'private-payload-bytes')
        info = manifest['files'][name]
        exact(info, 'role size sha256')
        require(type(info['size']) is int and len(data) == info['size'] and digest(data) == info['sha256'],
                'payload-file-mismatch')
        if info['role'] == 'config':
            validate_config(name, data)
    return manifest


def select_artifact(artifacts, identity):
    validate_identity(identity)
    name = 'release-candidate-' + identity['version'] + '-' + identity['sourceSha']
    matching = [artifact for artifact in artifacts if artifact.get('name') == name]
    require(len(matching) == 1, 'candidate-artifact-missing-or-colliding')
    artifact = matching[0]
    require(type(artifact.get('id')) is int and artifact['id'] > 0 and artifact.get('expired') is False,
            'candidate-artifact-unavailable')
    require(re.fullmatch('sha256:' + DIGEST, artifact.get('digest', '')) is not None, 'missing-api-digest')
    require(artifact.get('workflow_run', {}).get('id') == identity['runId'] and
            artifact['workflow_run'].get('head_sha') == identity['sourceSha'], 'artifact-run-mismatch')
    created = datetime.fromisoformat(artifact['created_at'].replace('Z', '+00:00'))
    expires = datetime.fromisoformat(artifact['expires_at'].replace('Z', '+00:00'))
    require(created.tzinfo is not None and expires.tzinfo is not None and
            (expires - created).total_seconds() >= 30 * 86400 - 60, 'retention-too-short')
    return artifact


def verify_download(path, artifact):
    require(re.fullmatch('sha256:' + DIGEST, artifact.get('digest', '')) is not None, 'missing-api-digest')
    require('sha256:' + digest(Path(path).read_bytes()) == artifact['digest'], 'download-digest-mismatch')


def seal(identity, artifact, archive_path):
    select_artifact([artifact], identity)
    verify_download(archive_path, artifact)
    outer = read_archive(archive_path)
    payload_name = 'Chat-' + identity['version'] + '-windows-x64.zip'
    require(set(outer) == {payload_name, 'payload-hashes.json'}, 'unexpected-upload-content')
    hashes = json.loads(outer['payload-hashes.json'])
    exact(hashes, 'payloadSha256 manifestSha256 payloadSize payloadName')
    require(hashes['payloadName'] == payload_name and hashes['payloadSize'] == len(outer[payload_name]),
            'payload-identity-mismatch')
    with tempfile.TemporaryDirectory(prefix='chat-release-verify-') as root:
        payload = Path(root) / payload_name
        payload.write_bytes(outer[payload_name])
        manifest = verify_candidate(payload, hashes['payloadSha256'], hashes['manifestSha256'])
    require(manifest['context']['identity'] == identity, 'manifest-run-mismatch')
    evidence = {'format': 1, 'identity': identity, 'deploymentId': manifest['context']['deploymentId'],
                'artifact': {key: artifact[key] for key in ('id', 'name', 'digest', 'created_at', 'expires_at')},
                'payload': hashes, 'retentionDays': 30, 'redownloadVerified': True,
                'attestation': 'UNAVAILABLE_NO_APPROVED_PINNED_GENERATOR'}
    validate_evidence(evidence)
    return evidence


def validate_evidence(evidence):
    exact(evidence, 'format identity deploymentId artifact payload retentionDays redownloadVerified attestation')
    require(type(evidence['format']) is int and evidence['format'] == 1 and
            evidence['retentionDays'] == 30 and evidence['redownloadVerified'] is True,
            'incomplete-upload-evidence')
    validate_identity(evidence['identity'])
    require(type(evidence['deploymentId']) is int and evidence['deploymentId'] > 0, 'missing-build-record')
    require(evidence['attestation'] == 'UNAVAILABLE_NO_APPROVED_PINNED_GENERATOR', 'false-attestation-claim')
    exact(evidence['artifact'], 'id name digest created_at expires_at')
    select_artifact([{**evidence['artifact'], 'expired': False,
                      'workflow_run': {'id': evidence['identity']['runId'],
                                       'head_sha': evidence['identity']['sourceSha']}}], evidence['identity'])
    exact(evidence['payload'], 'payloadSha256 manifestSha256 payloadSize payloadName')
    require(all(re.fullmatch(DIGEST, evidence['payload'][name]) for name in ('payloadSha256', 'manifestSha256')),
            'invalid-payload-digest')
    require(type(evidence['payload']['payloadSize']) is int and evidence['payload']['payloadSize'] > 0,
            'invalid-payload-size')
    require(evidence['payload']['payloadName'] == 'Chat-' + evidence['identity']['version'] + '-windows-x64.zip',
            'noncanonical-payload-name')
    public_metadata(evidence)
