"""Windows package loading/startup smoke. Real dependency E2E remains in the Linux job."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import xml.etree.ElementTree as ET

from package import APPS, sha256, verify


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def wait_until(probe, process, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError('Packaged process exited before readiness')
        try:
            if probe():
                return
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.1)
    raise TimeoutError('Packaged process did not become ready')


def tcp_ready(port):
    with socket.create_connection(('127.0.0.1', port), timeout=0.5):
        return True


def windows(pid):
    from ctypes import wintypes
    result = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user = ctypes.windll.user32
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    @callback_type
    def collect(handle, _):
        process_id = wintypes.DWORD()
        user.GetWindowThreadProcessId(handle, ctypes.byref(process_id))
        name = ctypes.create_unicode_buffer(256)
        title = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(handle, name, len(name))
        user.GetWindowTextW(handle, title, len(title))
        if process_id.value == pid and name.value.startswith('Qt') and title.value == 'Chat':
            result.append(handle)
        return True
    user.EnumWindows(collect, 0)
    return result


def start(command, cwd, environment, log):
    # CI hosts can inherit Ctrl+C-ignore. Do not pass that flag to application children.
    ctypes.windll.kernel32.SetConsoleCtrlHandler(None, False)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return subprocess.Popen(command, cwd=cwd, env=environment, stdout=log, stderr=log,
                            creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)


def stop(process, gui=False):
    if gui:
        from ctypes import wintypes
        ctypes.windll.user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        for handle in windows(process.pid):
            ctypes.windll.user32.PostMessageW(handle, 0x0010, 0, 0)  # WM_CLOSE
    else:
        # Each child owns a hidden console. A short helper sends Ctrl+C only to that console,
        # including ChatServer, which supports SIGINT but does not register SIGBREAK.
        helper = '''import ctypes, sys, time
k = ctypes.windll.kernel32
k.FreeConsole()
if not k.AttachConsole(int(sys.argv[1])): raise OSError('AttachConsole failed')
try:
    if not k.SetConsoleCtrlHandler(None, True): raise OSError('Console handler failed')
    if not k.GenerateConsoleCtrlEvent(0, 0): raise OSError('Ctrl+C failed')
    time.sleep(0.2)
finally:
    k.FreeConsole()
'''
        subprocess.run([sys.executable, '-c', helper, str(process.pid)], check=True,
                       capture_output=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
    if process.wait(timeout=20) != 0:
        raise RuntimeError('Packaged application did not stop cleanly')


def server_config(app, port, rpc_port):
    return f'''[GateServer]
Port={port}
[VarifyServer]
Host=127.0.0.1
Port=1
[StatusServer]
Host=127.0.0.1
Port={port}
[Redis]
Host=127.0.0.1
Port=1
Password=package-smoke
[Mysql]
Host=127.0.0.1
Port=1
Password=package-smoke
User=package-smoke
Schema=package_smoke
[SelfServer]
Name=PackageChat
Host=127.0.0.1
Port={port}
RPCPort={rpc_port}
[PeerServer]
Servers=
[ChatServers]
Name=PackageChat
[PackageChat]
Name=PackageChat
Host=127.0.0.1
Port={port}
[Log]
Name={app}
LogDir=logs
MaxSizeMB=1
MaxTotalFiles=2
Level=info
FlushLevel=warn
'''


def run(directory, source_sha, report):
    suite = ET.Element('testsuite', name='windows-package-smoke')
    failures = 0
    def case(name, action):
        nonlocal failures
        entry = ET.SubElement(suite, 'testcase', name=name)
        try:
            action()
        except Exception:
            failures += 1
            ET.SubElement(entry, 'failure', message='package smoke failed; see job log')
            raise
    try:
        if os.name != 'nt':
            raise RuntimeError('Windows runner required')
        archives = list(directory.glob('Chat-*-windows-x64.zip'))
        if len(archives) != 1:
            raise ValueError('Expected one release package')
        archive = archives[0]
        if (directory / 'SHA256SUMS').read_text(encoding='utf-8') != sha256(archive.read_bytes()) + '  ' + archive.name + '\n':
            raise ValueError('Package checksum mismatch')
        with tempfile.TemporaryDirectory(prefix='chat-smoke-') as temporary:
            root = Path(temporary)
            case('archive and required runtime files', lambda: verify(archive, root, source_sha))
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith(('CHAT_', 'QT_', 'QML_', 'NODE_'))}
            # Do not let the build toolchain or runner's Node/Qt satisfy missing package DLLs.
            environment['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
            for app in APPS[:3]:
                def server(app=app):
                    folder = root / app
                    port, rpc_port = free_port(), free_port()
                    (folder / 'config.ini').write_text(server_config(app, port, rpc_port), encoding='utf-8')
                    with (root / (app + '.log')).open('wb') as log:
                        process = start([str(folder / (app + '.exe')), '--config', str(folder / 'config.ini')],
                                        folder, environment, log)
                        try:
                            if app == 'GateServer':
                                def ready():
                                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/get_test', timeout=1) as response:
                                        return response.status == 200 and b'receive get_test req' in response.read()
                            else:
                                ready = lambda: tcp_ready(port)
                            wait_until(ready, process)
                            stop(process)
                            with socket.socket() as listener:
                                listener.bind(('127.0.0.1', port))
                        finally:
                            if process.poll() is None:
                                process.kill()
                                process.wait(timeout=10)
                case(app + ' startup and shutdown', server)
            def client():
                folder = root / 'chat-client'
                (folder / 'config.ini').write_text('[GateServer]\nhost=127.0.0.1\nport=1\n', encoding='utf-8')
                with (root / 'client.log').open('wb') as log:
                    process = start([str(folder / 'chat.exe')], folder, environment, log)
                    try:
                        wait_until(lambda: bool(windows(process.pid)), process)
                        stop(process, gui=True)
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=10)
            case('Qt window startup and close', client)
            def varify():
                folder = root / 'VarifyServer'
                # Exercise the packaged production module, proto resolution and real gRPC listener.
                # No mail/database request is made in this loader smoke.
                script = """const grpc=require('@grpc/grpc-js');
const app=require('./server');
const server=app.createServer((call, callback)=>callback(null, {error: 0}));
app.startServer({server,address:'127.0.0.1:0',credentials:grpc.ServerCredentials.createInsecure()})
.then(port=>{if(!(port>0))throw Error('bind failed');server.tryShutdown(error=>{if(error)process.exitCode=1;});})
.catch(()=>{process.exitCode=1;});"""
                subprocess.run([str(folder / 'node.exe'), '-e', script], cwd=folder, env=environment,
                               check=True, capture_output=True, timeout=20)
            case('packaged Node proto and gRPC startup', varify)
    except Exception:
        if failures == 0:
            entry = ET.SubElement(suite, 'testcase', name='package setup')
            ET.SubElement(entry, 'failure', message='package setup failed')
            failures = 1
        raise
    finally:
        suite.set('tests', str(len(suite)))
        suite.set('failures', str(failures))
        report.parent.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(suite).write(report, encoding='utf-8', xml_declaration=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    run(args.directory, args.sha, args.report)
