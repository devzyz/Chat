"""Build independent Windows client/server archives from the verified CI payload."""
import argparse
import json
from pathlib import Path
import tempfile
import zipfile

from package import assemble, check_layout, sha256, unpack, verify

WINDOWS = {'client': 'Chat-client-windows-x64.zip', 'server': 'Chat-server-windows-x64.zip'}


def build(artifacts, output, source_sha, tag):
    """Split sanitized files once and include a source-bound manifest in each archive."""
    repo = Path(__file__).resolve().parents[2]
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        archive = assemble(repo, artifacts, work / 'combined', source_sha)
        stage = work / 'contents'
        original = verify(archive, stage, source_sha)
        for role, name in WINDOWS.items():
            hashes = {}
            with zipfile.ZipFile(output / name, 'x', zipfile.ZIP_DEFLATED) as product:
                for path, digest in original['files'].items():
                    is_client = path.startswith('chat-client/')
                    if is_client != (role == 'client'):
                        continue
                    dest = path.removeprefix('chat-client/') if is_client else path
                    product.write(stage / path, dest)
                    hashes[dest] = digest
                if role == 'client':
                    config = b'[GateServer]\nhost=127.0.0.1\nport=8080\n[ResourceServer]\nUrl=http://127.0.0.1:8090\n'
                    product.writestr('config.ini', config)
                    hashes['config.ini'] = sha256(config)
                    readme = b'Edit config.ini to select the Gate and Resource endpoints, then start chat.exe.\n'
                    product.writestr('README.md', readme)
                    hashes['README.md'] = sha256(readme)
                product.writestr('release-manifest.json', json.dumps({
                    'sourceSha': source_sha, 'tag': tag, 'role': role, 'files': hashes}, indent=2))


def extract_pair(directory, root, source_sha):
    """Check the exact independent downloads before running their applications."""
    tags = set()
    for role in ('server', 'client'):
        target = root if role == 'server' else root / 'chat-client'
        unpack(directory / WINDOWS[role], target)
        manifest = json.loads((target / 'release-manifest.json').read_text(encoding='utf8'))
        actual = {p.relative_to(target).as_posix(): sha256(p.read_bytes())
                  for p in target.rglob('*') if p.is_file() and p != target / 'release-manifest.json'}
        if (manifest['role'] != role or manifest['sourceSha'] != source_sha or actual != manifest['files']
                or (role == 'server' and (root / 'chat-client').exists())):
            raise ValueError('Windows package identity or contents mismatch')
        tags.add(manifest['tag'])
    if len(tags) != 1:
        raise ValueError('Windows package versions differ')
    check_layout(root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for option in ('artifacts', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    build(args.artifacts, args.output, args.sha, args.tag)
