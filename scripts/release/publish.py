"""Publish a smoke-tested archive; drafts may be retried, published versions may not."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from package import read_version, sha256, verify


def gh(*arguments):
    result = subprocess.run(['gh', *arguments], capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError('GitHub release operation failed: ' + ' '.join(arguments[:2]))
    return result.stdout


def check_version():
    version = read_version(Path(__file__).resolve().parents[2])
    pages = json.loads(gh('api', '--paginate', '--slurp',
                          'repos/' + os.environ['GITHUB_REPOSITORY'] + '/releases?per_page=100'))
    if any(release['tag_name'] == 'v' + version and not release['draft']
           for page in pages for release in page):
        raise ValueError('Version already published; update VERSION before merging master')


def publish(directory, source_sha):
    if os.environ.get('GITHUB_EVENT_NAME') != 'push' or os.environ.get('GITHUB_REF') != 'refs/heads/master':
        raise ValueError('Publication is restricted to master push')
    source = Path(__file__).resolve().parents[2]
    version = read_version(source)
    tag = 'v' + version
    archive = directory / f'Chat-{version}-windows-x64.zip'
    with tempfile.TemporaryDirectory(prefix='chat-publish-') as temporary:
        metadata = verify(archive, Path(temporary) / 'contents', source_sha)
        if metadata['version'] != version:
            raise ValueError('Release version mismatch')
        expected = sha256(archive.read_bytes()) + '  ' + archive.name + '\n'
        if (directory / 'SHA256SUMS').read_text(encoding='utf-8') != expected:
            raise ValueError('Release ZIP checksum mismatch')
        pages = json.loads(gh('api', '--paginate', '--slurp',
                              'repos/' + os.environ['GITHUB_REPOSITORY'] + '/releases?per_page=100'))
        matches = [release for page in pages for release in page if release['tag_name'] == tag]
        if matches:
            release = matches[0]
            if not release['draft']:
                raise ValueError('Version already published; update VERSION before merging master')
            if release['target_commitish'] != source_sha:
                raise ValueError('Draft belongs to another commit; choose a new version')
        else:
            notes = Path(temporary) / 'notes.md'
            notes.write_text(f'Source: `{source_sha}`\n\nWindows x64 package. See the included README for configuration.\n',
                             encoding='utf-8')
            gh('release', 'create', tag, '--draft', '--target', source_sha,
               '--title', version, '--notes-file', str(notes))
        gh('release', 'upload', tag, str(archive), str(directory / 'SHA256SUMS'), '--clobber')
        download = Path(temporary) / 'download'
        gh('release', 'download', tag, '--dir', str(download))
        if {file.name for file in download.iterdir()} != {archive.name, 'SHA256SUMS'}:
            raise ValueError('Unexpected draft assets')
        for name in (archive.name, 'SHA256SUMS'):
            if (download / name).read_bytes() != (directory / name).read_bytes():
                raise ValueError('Uploaded bytes differ from tested package')
        gh('release', 'edit', tag, '--draft=false')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--sha')
    parser.add_argument('--check-version', action='store_true')
    args = parser.parse_args()
    if args.check_version:
        check_version()
    elif args.directory and args.sha:
        publish(args.directory, args.sha)
    else:
        parser.error('--directory and --sha are required for publication')
