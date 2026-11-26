#!/usr/bin/env python3
"""Pull one published GitHub release by tag and activate it under a dedicated user."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request

SERVICES = ('VarifyServer', 'ChatServer', 'ResourceServer', 'GateServer')


def unpack(stream, folder, source, digest):
    """Verify the bounded archive before extracting only directories and ordinary files."""
    size = 0
    checksum = hashlib.sha256()
    with tempfile.TemporaryFile(dir=folder.parent) as archive:
        while chunk := stream.read(1024 * 1024):
            size += len(chunk)
            if size > 256 * 1024 * 1024:
                raise ValueError('deployment archive exceeds limit')
            checksum.update(chunk)
            archive.write(chunk)
        if checksum.hexdigest() != digest:
            raise ValueError('archive checksum mismatch')
        archive.seek(0)
        with tarfile.open(fileobj=archive, mode='r:gz') as contents:
            total = 0
            for entry in contents:
                name = Path(entry.name)
                total += entry.size
                if (name.is_absolute() or '..' in name.parts or not (entry.isdir() or entry.isfile())
                        or total > 1024 ** 3):
                    raise ValueError('invalid archive entry')
                target = folder / name
                if entry.isdir():
                    target.mkdir(exist_ok=True, parents=True)
                else:
                    target.parent.mkdir(exist_ok=True, parents=True)
                    with contents.extractfile(entry) as data, target.open('xb') as result:
                        shutil.copyfileobj(data, result)
                    target.chmod(0o700 if entry.mode & 0o111 else 0o600)
    if (folder / 'SOURCE_SHA').read_text().strip() != source:
        raise ValueError('release source does not match request')
    for name in ('node', 'launch.sh', 'VarifyServer/server.js', 'proto/varify.proto',
                 'GateServer/GateServer', 'ChatServer/ChatServer', 'ResourceServer/ResourceServer'):
        if not (folder / name).is_file():
            raise ValueError('incomplete deployment package')


def probe(shared):
    """Check the public HTTP readiness and the configured local service listeners."""
    def port(file, section):
        ini = configparser.ConfigParser(interpolation=None)
        with (shared / (file + '.ini')).open() as config:
            ini.read_file(config)
        return ini.getint(section, 'Port')

    gate = port('GateServer', 'GateServer')
    resource = port('ResourceServer', 'ResourceServer')
    chat = port('ChatServer', 'SelfServer')
    varify = port('GateServer', 'VarifyServer')
    http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    end = time.monotonic() + 25
    while time.monotonic() < end:
        try:
            with http.open(f'http://127.0.0.1:{gate}/get_test', timeout=2) as response:
                if b'receive get_test req' not in response.read(512):
                    raise OSError('unexpected Gate response')
            with http.open(f'http://127.0.0.1:{resource}/ready', timeout=2) as response:
                if response.status != 200:
                    raise OSError('Resource is not ready')
            for number in (chat, varify):
                with socket.create_connection(('127.0.0.1', number), timeout=2):
                    pass
            return
        except OSError:
            time.sleep(.4)
    raise RuntimeError('service readiness deadline exceeded')


def activate(home, candidate):
    """Switch under the receiver lock; restore the previous release on startup failure."""
    current = home / 'current'
    previous = current.resolve(strict=True) if current.is_symlink() else None
    if previous and previous.parent != home / 'releases':
        raise ValueError('current release outside managed directory')
    config = str(home / 'supervisord.conf')

    def ctl(*args):
        return subprocess.run(['supervisorctl', '-c', config, *args], capture_output=True,
                              text=True, timeout=70)

    def stop():
        for service in reversed(SERVICES):
            result = ctl('stop', 'chat:' + service)
            if result.returncode and 'ERROR (not running)' not in result.stdout:
                raise RuntimeError('cannot stop ' + service)

    def select(path):
        link = home / '.next'
        link.unlink(missing_ok=True)
        link.symlink_to(path)
        os.replace(link, current)

    def start():
        for service in SERVICES:
            if ctl('start', 'chat:' + service).returncode:
                raise RuntimeError('cannot start ' + service)
        probe(home / 'shared')
        state = ctl('status', 'chat:*')
        if state.returncode or sum(' RUNNING ' in line for line in state.stdout.splitlines()) != len(SERVICES):
            raise RuntimeError('service group is not running')

    if ctl('pid').returncode:
        subprocess.run(['supervisord', '-c', config], check=True, timeout=15)
    try:
        stop()
        select(candidate)
        start()
    except Exception:
        stop()
        if previous:
            select(previous)
            start()
        else:
            current.unlink(missing_ok=True)
        raise


TAG = re.compile(r'v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)')
REPO = 'devzyz/Chat'
ASSET = 'Chat-server-linux-x64.tar.gz'


def github_json(path):
    request = urllib.request.Request(f'https://api.github.com/repos/{REPO}/' + path,
                                     headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'Chat-deployment'})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read(2 * 1024 * 1024 + 1)
    if len(data) > 2 * 1024 * 1024:
        raise ValueError('GitHub metadata too large')
    return json.loads(data)


def resolve_tag(requested, get=github_json):
    """Resolve latest once to its numbered release and verify its pinned source and asset."""
    if requested != 'latest' and not TAG.fullmatch(requested):
        raise ValueError('expected latest or vX.Y.Z')
    def record(tag):
        r = get('releases/tags/' + tag)
        if r['draft'] or r['tag_name'] != tag:
            raise ValueError('unpublished or mismatched release')
        m = re.fullmatch(r'Chat unified release\n\nSource release: \x60([^\x60]+)\x60\nSource commit: \x60([0-9a-f]{40})\x60\n?',
                         r['body'].replace('\r\n', '\n'))
        if not m or not TAG.fullmatch(m[1]):
            raise ValueError('invalid source release metadata')
        return r, m[1], m[2]
    r, tag, source = record(requested)
    if requested == 'latest':
        if r['prerelease']:
            raise ValueError('expected stable rolling release')
        r, pinned, commit = record(tag)
        if (tag, source) != (pinned, commit):
            raise ValueError('rolling and numbered release disagree')
    elif requested != tag:
        raise ValueError('numbered version mismatch')
    obj = get('git/ref/tags/' + tag)['object']
    while obj['type'] == 'tag':
        if not re.fullmatch('[0-9a-f]{40}', obj['sha']):
            raise ValueError('invalid annotated tag reference')
        obj = get('git/tags/' + obj['sha'])['object']
    if r['prerelease'] or obj.get('type') != 'commit' or obj.get('sha') != source:
        raise ValueError('formal tag no longer points to published source')
    assets = [a for a in r['assets'] if a['name'] == ASSET]
    if len(assets) != 1 or not re.fullmatch(r'sha256:[0-9a-f]{64}', assets[0].get('digest') or ''):
        raise ValueError('missing verified Linux server archive')
    return tag, source, assets[0]['digest'][7:]


def install(home, tag, source, digest, stream):
    """Install a fully received archive, preserving the previous version during a failed transfer."""
    releases = home / 'releases'
    target = releases / tag
    with tempfile.TemporaryDirectory(dir=releases) as temp:
        staged = Path(temp) / 'release'
        staged.mkdir()
        unpack(stream, staged, source, digest)
        if target.exists():
            if (target / 'SOURCE_SHA').read_text().strip() != source or (target / '.asset-digest').read_text().strip() != digest:
                raise ValueError('refusing to replace an installed formal version')
        else:
            (staged / '.asset-digest').write_text(digest)
            staged.rename(target)
    try:
        activate(home, target)
    except Exception:
        if (home / 'current').resolve() != target:
            shutil.rmtree(target)
        raise
    print('Activated and verified', tag, 'source', source)


def download_asset(tag):
    """Download the fixed asset from the resolved formal release on GitHub."""
    return urllib.request.urlopen(f'https://github.com/{REPO}/releases/download/{tag}/{ASSET}', timeout=60)


def pull(home, requested, get=github_json, download=download_asset):
    """Pin latest once and feed the downloaded bytes to the same tested installer."""
    tag, source, digest = resolve_tag(requested, get)
    with download(tag) as stream:
        install(home, tag, source, digest, stream)


def main():
    """The forced SSH command accepts only a version; repository and asset are fixed locally."""
    import fcntl
    request = re.fullmatch(r'deploy (latest|v[0-9]+\.[0-9]+\.[0-9]+)', os.environ.get('SSH_ORIGINAL_COMMAND', ''))
    if not request:
        raise ValueError('expected deploy TAG')
    home = Path(os.environ['CHAT_DEPLOY_HOME']).resolve(strict=True)
    os.umask(0o077)
    with (home / 'deploy.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        pull(home, request[1])


if __name__ == '__main__':
    main()
