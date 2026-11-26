"""Use loopback HTTP for GitHub metadata and asset downloads in deployment rehearsals."""
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import sys
import threading
import urllib.request
from unittest.mock import patch


@contextmanager
def release_http(lookup, asset):
    """Replace only the two GitHub origins; exercise metadata reads and actual streamed downloads."""
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            try:
                if self.path.startswith('/api/'):
                    data = json.dumps(lookup(self.path[5:])).encode()
                    self.send_response(200)
                    self.send_header('Content-Length', str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                else:
                    tag, name = self.path.removeprefix('/files/').split('/')
                    file = asset(tag, name)
                    self.send_response(200)
                    self.send_header('Content-Length', str(file.stat().st_size))
                    self.end_headers()
                    with file.open('rb') as stream:
                        shutil.copyfileobj(stream, self.wfile)
            except (KeyError, ValueError, FileNotFoundError):
                self.send_error(404)

    with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
        base = f'http://127.0.0.1:{server.server_port}'
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def local(request, timeout):
            url = request.full_url if isinstance(request, urllib.request.Request) else request
            for origin, route in [('https://api.github.com/repos/devzyz/Chat/', '/api/'),
                                  ('https://github.com/devzyz/Chat/releases/download/', '/files/')]:
                if url.startswith(origin):
                    return opener.open(base + route + url[len(origin):], timeout=timeout)
            raise ValueError('Unexpected rehearsal URL')
        try:
            with patch.object(urllib.request, 'urlopen', side_effect=local):
                yield
        finally:
            server.shutdown()
            thread.join()


def exercise(tag, sha, digest, archive, requested):
    """Run the installed receiver with its actual SSH command and real supervisor."""
    sys.path.insert(0, os.environ['CHAT_DEPLOY_HOME'])
    import receive
    def lookup(path):
        if path == 'git/ref/tags/' + tag:
            return {'object': {'type': 'commit', 'sha': sha}}
        name = path.removeprefix('releases/tags/')
        if path != 'releases/tags/' + name or name not in (tag, 'latest'):
            raise KeyError(path)
        return {'tag_name': name, 'draft': False, 'prerelease': False,
                'body': f'Chat unified release\n\nSource release: \x60{tag}\x60\nSource commit: \x60{sha}\x60\n',
                'assets': [{'name': receive.ASSET, 'digest': 'sha256:' + digest}]}
    def file_for(version, name):
        if (version, name) != (tag, receive.ASSET):
            raise KeyError(name)
        return Path(archive)
    with release_http(lookup, file_for), patch.dict(os.environ, SSH_ORIGINAL_COMMAND='deploy ' + requested):
        receive.main()


if __name__ == '__main__':
    exercise(*sys.argv[1:])
