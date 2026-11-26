"""Exercise the production publisher and tag resolution against a local release store."""
import copy
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'scripts/release'), str(ROOT / 'scripts/deploy'), str(ROOT / 'tests/deploy')]
import publish
import receive
from pull_fixture import release_http


class LocalReleases:
    """Filesystem substitute for the GitHub boundary; has no token or network operations."""
    def __init__(self, root):
        self.root = root
        self.rows = {}
        self.refs = {}
        self.fail_once = None

    def releases(self):
        return copy.deepcopy(self.rows)

    def ref(self, tag):
        return self.refs.get(tag)

    def move_latest(self, sha):
        self.refs['latest'] = sha

    def create(self, tag, sha, body):
        assert tag not in self.rows
        self.rows[tag] = {'id': tag, 'tag_name': tag, 'body': body, 'target_commitish': sha,
                          'draft': True, 'prerelease': False, 'assets': []}
        (self.root / tag).mkdir()
        return copy.deepcopy(self.rows[tag])

    def edit(self, release, **fields):
        row = self.rows[release['tag_name']]
        row.update(fields)
        if not row['draft']:
            self.refs.setdefault(row['tag_name'], row['target_commitish'])
        return copy.deepcopy(row)

    def upload(self, tag, file):
        row = self.rows[tag]
        assert tag == 'latest' or row['draft'], 'public numbered release was modified'
        row['assets'] = [a for a in row['assets'] if a['name'] != file.name]
        if self.fail_once == (tag, file.name):
            self.fail_once = None
            raise IOError('injected upload failure')
        shutil.copyfile(file, self.root / tag / file.name)
        row['assets'].append({'name': file.name, 'digest': publish.checksum(file)})

    def download(self, tag, name, folder):
        assert not self.rows[tag]['draft']
        shutil.copyfile(self.root / tag / name, folder / name)

    def get(self, path):
        if path.startswith('releases/tags/'):
            return self.releases()[path.rsplit('/', 1)[-1]]
        assert path.startswith('git/ref/tags/')
        return {'object': {'type': 'commit', 'sha': self.ref(path.rsplit('/', 1)[-1])}}


def rehearse(directory, tag, sha):
    """Exercise the exact archives, then resolve both deploy commands to the formal server."""
    for name in publish.FORMAL:
        if name.endswith('.zip'):
            with zipfile.ZipFile(directory / name) as archive:
                meta = json.loads(archive.read('release-manifest.json'))
                if (meta['sourceSha'], meta['tag']) != (sha, tag) or archive.testzip() is not None:
                    raise ValueError('Windows candidate identity mismatch')
        else:
            with tarfile.open(directory / name, 'r:gz') as archive:
                if archive.extractfile('./SOURCE_SHA').read().decode().strip() != sha:
                    raise ValueError('Linux candidate identity mismatch')
    with tempfile.TemporaryDirectory() as temp:
        home = Path(temp)
        hub = LocalReleases(home)
        hub.refs[tag] = sha
        publish.publish_formal(hub, tag, sha, directory)
        assert 'latest' not in hub.rows and not hub.rows[tag]['draft']
        publish.sync_latest(hub)
        assert set(publish.contents(hub.rows[tag])) == set(publish.FORMAL)
        assert set(publish.contents(hub.rows['latest'])) == set(publish.ROLLING)
        for name in publish.ROLLING:
            assert publish.checksum(home / 'latest' / name) == publish.checksum(directory / name)
        def asset(version, name):
            if version != tag or name != receive.ASSET:
                raise KeyError(name)
            return home / tag / name
        deployment = home / 'host'
        (deployment / 'releases').mkdir(parents=True)
        with release_http(hub.get, asset), patch.object(receive, 'activate') as activation:
            for requested in (tag, 'latest'):
                receive.pull(deployment, requested)
                activation.assert_called_with(deployment, deployment / 'releases' / tag)
        assert (deployment / 'releases' / tag / 'SOURCE_SHA').read_text().strip() == sha
        print('Rehearsal passed; formal:', tag, *publish.FORMAL, '; rolling latest:', *publish.ROLLING,
              '; both AutoDL pulls verified:', sha)


if __name__ == '__main__':
    rehearse(Path(sys.argv[1]), sys.argv[2], sys.argv[3])
