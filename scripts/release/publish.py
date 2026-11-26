"""Publish a four-asset stable release, then reconcile the separate rolling release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

FORMAL = ('Chat-client-windows-x64.zip', 'Chat-server-windows-x64.zip',
          'Chat-client-linux-x64.tar.gz', 'Chat-server-linux-x64.tar.gz')
ROLLING = (FORMAL[0], FORMAL[3])
TAG = re.compile(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)')
SHA = re.compile(r'[0-9a-f]{40}')
MARKER = 'Chat unified release'


def checksum(path):
    """Compute the digest without loading a release archive into memory."""
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return 'sha256:' + digest.hexdigest()


def notes(tag, sha):
    return f'{MARKER}\n\nSource release: \x60{tag}\x60\nSource commit: \x60{sha}\x60\n'


def identity(release):
    """Accept only release records created by this publisher."""
    body = release['body'].replace('\r\n', '\n')
    match = re.fullmatch(MARKER + r'\n\nSource release: \x60([^\x60]+)\x60\nSource commit: \x60([a-f0-9]{40})\x60\n?', body)
    if not match or not TAG.fullmatch(match[1]):
        raise ValueError('Release metadata is not managed by the unified publisher')
    return match[1], match[2]


def contents(release):
    return {a['name']: a.get('digest') for a in release['assets']}


class GitHub:
    """Perform GitHub operations for the workflow repository using its scoped token."""
    def __init__(self, repo):
        self.repo = repo

    def gh(self, *args, payload=None):
        p = subprocess.run(['gh', *args], input=None if payload is None else json.dumps(payload),
                           text=True, encoding='utf8', capture_output=True, timeout=600)
        if p.returncode:
            raise RuntimeError(f'gh {args[0]} failed: {p.stderr.strip()[:350]}')
        return p.stdout

    def api(self, suffix, method='GET', data=None):
        args = ['api', '--method', method, 'repos/' + self.repo + '/' + suffix]
        if data is not None:
            args += ['--input', '-']
        raw = self.gh(*args, payload=data)
        return json.loads(raw) if raw.strip() else None

    def releases(self):
        pages = self.gh('api', '--paginate', '--slurp', f'repos/{self.repo}/releases?per_page=100')
        return {r['tag_name']: r for page in json.loads(pages) for r in page}

    def ref(self, tag):
        refs = self.api('git/matching-refs/tags/' + tag)
        for ref in refs:
            if ref['ref'] == 'refs/tags/' + tag:
                obj = ref['object']
                while obj['type'] == 'tag':
                    obj = self.api('git/tags/' + obj['sha'])['object']
                if obj['type'] != 'commit':
                    raise ValueError('release tag must resolve to a commit')
                return obj['sha']
        return None

    def move_latest(self, sha):
        self.api('git/refs/tags/latest', 'PATCH', {'sha': sha, 'force': True})

    def create(self, tag, sha, body):
        return self.api('releases', 'POST', {'tag_name': tag, 'target_commitish': sha, 'name': tag,
                        'body': body, 'draft': True, 'prerelease': False})

    def edit(self, release, **fields):
        return self.api('releases/' + str(release['id']), 'PATCH', fields)

    def upload(self, tag, file):
        self.gh('release', 'upload', tag, str(file), '--repo', self.repo, '--clobber')

    def download(self, tag, name, folder):
        self.gh('release', 'download', tag, '--repo', self.repo, '--pattern', name, '--dir', str(folder))


def upload_set(hub, release, files):
    """Resume a partial upload and verify exactly the expected bytes before exposure."""
    actual = contents(release)
    if set(actual) - files.keys():
        raise ValueError('Unexpected existing assets; refusing to remove them')
    expected = {name: checksum(path) for name, path in files.items()}
    for name, path in files.items():
        if actual.get(name) != expected[name]:
            hub.upload(release['tag_name'], path)
    if contents(hub.releases()[release['tag_name']]) != expected:
        raise ValueError('GitHub asset digest or membership mismatch')


def publish_formal(hub, tag, sha, directory):
    if not TAG.fullmatch(tag) or not SHA.fullmatch(sha) or hub.ref(tag) != sha:
        raise ValueError('Stable tag must point to the tested commit')
    files = {name: directory / name for name in FORMAL}
    if set(directory.iterdir()) != set(files.values()) or not all(p.is_file() for p in files.values()):
        raise ValueError('Expected exactly four tested application packages')
    existing = hub.releases().get(tag)
    if existing:
        if identity(existing) != (tag, sha) or existing['prerelease']:
            raise ValueError('Existing release belongs to another source or publisher')
        if not existing['draft']:
            verified_assets(existing, FORMAL)
            return
    release = existing or hub.create(tag, sha, notes(tag, sha))
    upload_set(hub, release, files)
    if hub.ref(tag) != sha:
        raise ValueError('Tag changed during publication')
    hub.edit(release, draft=False, make_latest='legacy')


def verified_assets(release, names):
    assets = contents(release)
    if set(assets) != set(names) or any(not re.fullmatch(r'sha256:[0-9a-f]{64}', v or '') for v in assets.values()):
        raise ValueError('Incomplete published assets')
    return assets


def obtain(hub, release, folder):
    """Download the selected original release assets and verify GitHub's recorded digests."""
    folder.mkdir()
    expected = verified_assets(release, FORMAL)
    files = {}
    for name in ROLLING:
        hub.download(release['tag_name'], name, folder)
        files[name] = folder / name
        if checksum(files[name]) != expected[name]:
            raise ValueError('Downloaded formal asset differs')
    return files


def sync_latest(hub):
    """Promote the highest published stable version; an older run cannot move it backwards."""
    releases = hub.releases()
    candidates = [r for tag, r in releases.items() if TAG.fullmatch(tag) and not r['draft']
                  and not r['prerelease'] and (r.get('body') or '').splitlines()[:1] == [MARKER]]
    if not candidates:
        raise ValueError('No published unified release')
    new = max(candidates, key=lambda r: tuple(map(int, TAG.fullmatch(r['tag_name']).groups())))
    tag, sha = identity(new)
    if new['tag_name'] != tag or hub.ref(tag) != sha:
        raise ValueError('Published formal tag moved')
    assets = verified_assets(new, FORMAL)
    rolling = releases.get('latest')
    ref = hub.ref('latest')
    if not rolling and ref is not None:
        raise ValueError('Unmanaged latest tag already exists')
    previous = None
    if rolling:
        old_tag, old_sha = identity(rolling)
        if rolling.get('immutable') or rolling['prerelease']:
            raise ValueError('Rolling release must be managed and mutable')
        if not rolling['draft']:
            previous = releases[old_tag]
            if identity(previous) != (old_tag, old_sha) or hub.ref(old_tag) != old_sha:
                raise ValueError('Previous formal version is unavailable')
            if (old_tag == tag and ref == sha and
                    contents(rolling) == {name: assets[name] for name in ROLLING}):
                return
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        new_files = obtain(hub, new, root / 'new')
        old_files = obtain(hub, previous, root / 'previous') if previous else None
        rolling = rolling or hub.create('latest', sha, notes(tag, sha))
        try:
            upload_set(hub, rolling, new_files)
            if ref is not None and ref != sha:
                hub.move_latest(sha)
            hub.edit(rolling, draft=False, prerelease=False, target_commitish=sha,
                     body=notes(tag, sha), name='latest · ' + tag, make_latest='false')
            if hub.ref('latest') != sha or identity(hub.releases()['latest']) != (tag, sha):
                raise ValueError('Rolling ref and release metadata did not converge')
        except Exception:
            if previous:
                upload_set(hub, hub.releases()['latest'], old_files)
                if hub.ref('latest') != old_sha:
                    hub.move_latest(old_sha)
                hub.edit(rolling, body=notes(old_tag, old_sha), name='latest · ' + old_tag,
                         prerelease=False, make_latest='false')
            raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['formal', 'rolling'])
    parser.add_argument('--tag', required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--directory', type=Path)
    args = parser.parse_args()
    if (os.environ.get('GITHUB_EVENT_NAME') != 'push' or not TAG.fullmatch(args.tag)
            or os.environ.get('GITHUB_REF') != 'refs/tags/' + args.tag):
        parser.error('Publication requires the matching stable tag push')
    hub = GitHub(os.environ['GITHUB_REPOSITORY'])
    if args.operation == 'formal':
        if not args.directory:
            parser.error('Formal release requires all four packages')
        publish_formal(hub, args.tag, args.sha, args.directory)
    else:
        sync_latest(hub)
