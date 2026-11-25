#!/usr/bin/env python3
"""Receive one checksummed release on stdin and activate it under a dedicated user."""
import configparser
import fcntl
import hashlib
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


def main():
    """Accept only the fixed forced-command protocol and serialize changes to this deployment."""
    request = re.fullmatch(r'activate ([0-9a-f]{40}) ([0-9a-f]{64})', os.environ.get('SSH_ORIGINAL_COMMAND', ''))
    if not request:
        raise ValueError('expected activate SOURCE_SHA SHA256')
    home = Path(os.environ['CHAT_DEPLOY_HOME']).resolve(strict=True)
    os.umask(0o077)
    source, digest = request.groups()
    releases = home / 'releases'
    target = releases / (source + '-' + digest)
    with (home / 'deploy.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with tempfile.TemporaryDirectory(dir=releases) as temp:
            staged = Path(temp) / 'release'
            staged.mkdir()
            unpack(sys.stdin.buffer, staged, source, digest)
            if not target.exists():
                staged.rename(target)
        try:
            activate(home, target)
        except Exception:
            if (home / 'current').resolve() != target:
                shutil.rmtree(target)
            raise
        print('Activated and verified develop source', source, 'archive', digest)


if __name__ == '__main__':
    main()
