"""Assemble the tested CI outputs; never compile or reserve a release version."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tempfile
import zipfile

SERVERS = ('GateServer', 'StatusServer', 'ChatServer', 'ResourceServer')
APPS = (*SERVERS, 'chat-client', 'VarifyServer')
REQUIRED = [*(f'{app}/{app}.exe' for app in SERVERS),
            *(f'{app}/{dll}' for app in SERVERS
              for dll in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')),
            'chat-client/chat.exe', 'chat-client/Qt6Core.dll', 'chat-client/Qt6Widgets.dll',
            'chat-client/platforms/qwindows.dll', 'VarifyServer/node.exe', 'VarifyServer/server.js',
            'VarifyServer/node_modules/@grpc/grpc-js/package.json',
            'VarifyServer/node_modules/@grpc/proto-loader/package.json',
            'VarifyServer/node_modules/ioredis/package.json',
            'VarifyServer/node_modules/nodemailer/package.json',
            'VarifyServer/node_modules/uuid/package.json',
            'proto/varify.proto', 'migrations/manifest.json', 'README.md']


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_version(source):
    version = (source / 'VERSION').read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('VERSION must contain x.x.x')
    return version


def unpack(archive, destination):
    """Reject misplaced/duplicate entries before extracting a CI package."""
    with zipfile.ZipFile(archive) as zip_file:
        seen = set()
        for info in zip_file.infolist():
            name = info.filename.rstrip('/')
            parts = PurePosixPath(name).parts
            if (not parts or name.startswith('/') or '\\' in name or ':' in name or
                    '..' in parts or name.casefold() in seen or (info.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError('Unsafe or duplicate ZIP entry')
            seen.add(name.casefold())
        zip_file.extractall(destination)


def blank_config(file):
    if file.suffix == '.json':
        def blank(value):
            return {key: blank(child) for key, child in value.items()} if isinstance(value, dict) else None
        return json.dumps(blank(json.loads(file.read_text(encoding='utf-8'))), indent=2) + '\n'
    lines = ['; Fill values for your installation before starting.']
    for line in file.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            lines.append(line)
        elif '=' in line and not line.startswith((';', '#')):
            lines.append(line.split('=', 1)[0].strip() + '=')
    return '\n'.join(lines) + '\n'


def check_layout(root):
    for name in REQUIRED:
        if not (root / name).is_file():
            raise ValueError('Missing runtime file: ' + name)
    for app in APPS:
        name = 'config.json.template' if app == 'VarifyServer' else 'config.ini.template'
        if not (root / app / name).is_file():
            raise ValueError('Missing configuration template: ' + app)
    migration = json.loads((root / 'migrations/manifest.json').read_text(encoding='utf-8'))
    for item in migration['migrations']:
        path = root / 'migrations' / item['file']
        if not path.resolve().is_relative_to((root / 'migrations').resolve()):
            raise ValueError('Invalid migration path')
        if sha256(path.read_bytes()) != item['checksum']:
            raise ValueError('Migration checksum mismatch')


def assemble(source, artifacts, output, source_sha):
    version = read_version(source)
    if not re.fullmatch(r'[a-f0-9]{40}', source_sha):
        raise ValueError('Full source SHA required')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='chat-package-') as temporary:
        stage = Path(temporary)
        for app in APPS:
            archives = list(artifacts.rglob(app + '.zip'))
            if len(archives) != 1:
                raise ValueError('Expected one CI ZIP for ' + app)
            unpack(archives[0], stage / app)
        # Production Varify resolves ../proto/varify.proto, shared with the servers.
        for file in (source / 'proto').glob('*.proto'):
            target = stage / 'proto' / file.name
            target.parent.mkdir(exist_ok=True)
            target.write_bytes(file.read_bytes())
        manifest = json.loads((source / 'schema/manifest.json').read_text(encoding='utf-8'))
        for name in ['manifest.json'] + [item['file'] for item in manifest['migrations']]:
            target = stage / 'migrations' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((source / 'schema' / name).read_bytes())
        for app in APPS:
            files = list((stage / app).glob('config.ini')) + list((stage / app).glob('config.json'))
            files += list((stage / app / 'configs').glob('*.ini'))
            for file in files:
                file.with_suffix(file.suffix + '.template').write_text(blank_config(file), encoding='utf-8')
                file.unlink()
        (stage / 'README.md').write_bytes((source / 'scripts/release/INSTALL.md').read_bytes())
        check_layout(stage)
        files = {file.relative_to(stage).as_posix(): sha256(file.read_bytes())
                 for file in sorted(stage.rglob('*')) if file.is_file()}
        metadata = {'version': version, 'sourceSha': source_sha, 'files': files}
        (stage / 'release-manifest.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        archive = output / f'Chat-{version}-windows-x64.zip'
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as package:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    package.write(file, file.relative_to(stage).as_posix())
        (output / 'SHA256SUMS').write_text(sha256(archive.read_bytes()) + '  ' + archive.name + '\n', encoding='utf-8')
    return archive


def verify(archive, destination, source_sha):
    unpack(archive, destination)
    metadata = json.loads((destination / 'release-manifest.json').read_text(encoding='utf-8'))
    if metadata['sourceSha'] != source_sha:
        raise ValueError('Package is from another commit')
    actual = {file.relative_to(destination).as_posix(): sha256(file.read_bytes())
              for file in destination.rglob('*')
              if file.is_file() and file.relative_to(destination).as_posix() != 'release-manifest.json'}
    if actual != metadata['files']:
        raise ValueError('Package file checksum mismatch')
    check_layout(destination)
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sha', required=True)
    args = parser.parse_args()
    assemble(Path(__file__).resolve().parents[2], args.artifacts, args.output, args.sha)
