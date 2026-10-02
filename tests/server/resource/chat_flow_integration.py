"""Production Chat/Resource processes + real MySQL; explicit Redis/Status fixtures.

This verifies resource wiring and cross-Chat gRPC, not real Redis/Status adapters.
"""
import hashlib
import http.client
import json
import pathlib
import socket
import socketserver
import struct
import subprocess
import threading
import time
import sys
from stream_integration import process_line, avatar_png, create_video_fixture

ROOT = pathlib.Path(__file__).resolve().parents[3]


class RedisFixture(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), RedisHandler)
        self.values, self.hashes = {}, {}
        self.lock = threading.Lock()
        for uid in (7, 8, 9):
            self.values[f"ubaseinfo_{uid}".encode()] = json.dumps(dict(uid=uid, name=f"user{uid}",
                password="fixture", email=f"user{uid}@example.invalid", description="", icon="", sex=0)).encode()


class RedisHandler(socketserver.StreamRequestHandler):
    def handle(self):
        while True:
            try:
                header = self.rfile.readline()
            except ConnectionResetError:
                return
            if not header:
                return
            if not header.startswith(b"*"):
                return
            values = []
            for _ in range(int(header[1:])):
                size = int(self.rfile.readline()[1:]); values.append(self.rfile.read(size)); self.rfile.read(2)
            command, *args = values
            with self.server.lock:
                data, hashes = self.server.values, self.server.hashes
                result = b"+OK\r\n"
                if command == b"PING": result = b"+PONG\r\n"
                elif command == b"GET": result = self.bulk(data.get(args[0]))
                elif command == b"SET":
                    if b"NX" in args[2:] and args[0] in data: result = b"$-1\r\n"
                    else: data[args[0]] = args[1]
                elif command == b"DEL": result = b":1\r\n" if data.pop(args[0], None) is not None else b":0\r\n"
                elif command == b"HSET": hashes.setdefault(args[0], {})[args[1]] = args[2]; result = b":1\r\n"
                elif command == b"HGET": result = self.bulk(hashes.get(args[0], {}).get(args[1]))
                elif command == b"HDEL": hashes.get(args[0], {}).pop(args[1], None); result = b":1\r\n"
                elif command == b"EVAL":
                    script, count, *parameters = args
                    keys, argv = parameters[:int(count)], parameters[int(count):]
                    if b"chatlease_" in b" ".join(keys):
                        if b"HDEL" in script:
                            if data.get(keys[1]) == argv[1]:
                                data.pop(keys[1], None); hashes.get(keys[0], {}).pop(argv[0], None)
                            result = b"*0\r\n"
                        elif b"registered" in script:
                            if keys[1] in data and not data[keys[1]].startswith(argv[2]): result = b"*0\r\n"
                            else:
                                data[keys[1]] = argv[1]; hashes.setdefault(keys[0], {})[argv[0]] = b"0"
                                result = b"*1\r\n" + self.bulk(b"registered")
                        else:
                            data[keys[1]] = argv[2]; hashes.setdefault(keys[0], {})[argv[0]] = argv[1]
                            result = b"*1\r\n" + self.bulk(b"renewed")
                    elif len(keys) == 2:
                        # Production RedisUserPresenceStore uses atomic two-key presence operations.
                        previous = [data.get(key, b"") for key in keys]
                        if b"return {'current'}" in script:
                            result = b"*1\r\n" + self.bulk(b"current") if previous[1] == argv[0] and previous[0] else b"*0\r\n"
                        elif b"MSET" in script:
                            data.update(zip(keys, argv))
                            result = b"*2\r\n" + b"".join(self.bulk(value) for value in previous)
                        elif b"DEL" in script:
                            if previous == argv:
                                for key in keys: data.pop(key, None)
                            result = b"*0\r\n"
                        else:
                            result = b"*2\r\n" + b"".join(self.bulk(value) for value in previous)
                    elif len(keys) == 1 and len(argv) == 1:
                        if data.get(keys[0]) == argv[0]:
                            data.pop(keys[0]); result = b":1\r\n"
                        else: result = b":0\r\n"
                    else:
                        result = b"-ERR unsupported fixture script\r\n"
                elif command != b"AUTH": result = b"-ERR unsupported fixture command\r\n"
            self.wfile.write(result)

    @staticmethod
    def bulk(value):
        return b"$-1\r\n" if value is None else b"$" + str(len(value)).encode() + b"\r\n" + value + b"\r\n"


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0)); return sock.getsockname()[1]


def wait_port(value, process):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process.poll() is not None: raise RuntimeError("production test process exited")
        try:
            with socket.create_connection(("127.0.0.1", value), timeout=0.2): return
        except OSError: time.sleep(0.05)
    raise TimeoutError("production process not listening")


def send(sock, message_id, value):
    body = json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode()
    sock.sendall(struct.pack("!HH", message_id, len(body)) + body)


def receive(sock, expected, success=True):
    def exact(count):
        result = b""
        while len(result) < count:
            part = sock.recv(count - len(result))
            if not part: raise ConnectionError("chat socket closed")
            result += part
        return result
    message_id, length = struct.unpack("!HH", exact(4))
    assert length <= (65535 if message_id == 1028 else 8192 if message_id in (1006, 1017, 1047) else 2048), (message_id, length)
    body = json.loads(exact(length))
    assert message_id == expected, (message_id, expected, body)
    assert (body.get("error", 0) == 0) == success, body
    return body


def run_flow(directory, mysql_command, mysql_port):
    seed = """USE resource_test;
    INSERT INTO user(uid,name,email,password,description,icon,sex) VALUES
        (7,'user7','user7@example.invalid','fixture','','',0),
        (8,'user8','user8@example.invalid','fixture','','',0),
        (9,'user9','user9@example.invalid','fixture','','',0);
    UPDATE user_id SET id=9;
    INSERT INTO chat(chat_id,type) VALUES(1,'private'),(2,'private');
    """
    subprocess.run(mysql_command, input=seed, text=True, check=True, timeout=10)
    processes, sockets, logs = [], [], []
    redis = RedisFixture()
    redis_thread = threading.Thread(target=redis.serve_forever, daemon=True); redis_thread.start()
    def launch(executable, config, name):
        log = open(directory / (name + ".stdout"), "wb"); logs.append(log)
        process = subprocess.Popen([str(executable), "--config", str(config)], stdout=log, stderr=log,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        processes.append(process); return process
    try:
        status = subprocess.Popen([str(ROOT / "build/resource/ResourceTests.exe"), "--serve-status-fixture"],
                                  stdout=subprocess.PIPE, text=True,
                                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        processes.append(status); status_port = int(process_line(status))
        resource_port, chat_a, rpc_a, chat_b, rpc_b = [port() for _ in range(5)]
        database = f"[Mysql]\nHost=127.0.0.1\nPort={mysql_port}\nUser=root\nPassword=\nSchema=resource_test\n"
        resource_config = directory / "resource.ini"
        resource_config.write_text(f"[ResourceServer]\nHost=127.0.0.1\nPort={resource_port}\nStorageRoot={directory / 'resources'}\n"
            f"[StatusServer]\nHost=127.0.0.1\nPort={status_port}\n[Log]\nLogDir={directory / 'logs'}\n" + database)
        resource = launch(ROOT / "build/windows-servers/Release/ResourceServer/ResourceServer.exe", resource_config, "resource")
        wait_port(resource_port, resource)
        for name, tcp, rpc, peer, peer_rpc in (("resource-a", chat_a, rpc_a, "resource-b", rpc_b),
                                               ("resource-b", chat_b, rpc_b, "resource-a", rpc_a)):
            config = directory / (name + ".ini")
            config.write_text(f"[SelfServer]\nName={name}\nHost=127.0.0.1\nPort={tcp}\nRPCPort={rpc}\n"
                f"[StatusServer]\nHost=127.0.0.1\nPort={status_port}\n"
                f"[Redis]\nHost=127.0.0.1\nPort={redis.server_address[1]}\nPassword=fixture\n"
                f"[PeerServer]\nServers=Peer\n[Peer]\nName={peer}\nHost=127.0.0.1\nPort={peer_rpc}\n"
                f"[Log]\nName={name}\nLogDir={directory / 'logs'}\nMaxSizeMB=2\nMaxTotalFiles=2\nLevel=debug\nFlushLevel=debug\n" + database)
            process = launch(ROOT / "build/windows-servers/Release/ChatServer/ChatServer.exe", config, name)
            wait_port(tcp, process)
        def login(tcp, uid):
            sock = socket.create_connection(("127.0.0.1", tcp), timeout=10); sockets.append(sock)
            send(sock, 1005, dict(capabilities=["group_membership_v1"], uid=uid, token="fixture-token")); receive(sock, 1006); return sock
        sender, receiver = login(chat_a, 7), login(chat_b, 8)
        def request_resource(method, route, data=b"", uid=7, extra=None):
            conn = http.client.HTTPConnection("127.0.0.1", resource_port, timeout=15)
            headers = {"X-User-Id": str(uid), "Authorization": "Bearer fixture-token"}; headers.update(extra or {})
            conn.request(method, route, data, headers); response = conn.getresponse()
            code, body = response.status, response.read(); conn.close(); return code, body
        avatar_data = avatar_png()
        code, body = request_resource("POST", "/uploads", json.dumps(dict(name="avatar.png", media_type="image/png",
            size=str(len(avatar_data)), sha256=hashlib.sha256(avatar_data).hexdigest())).encode())
        assert code == 200, body
        avatar_id = json.loads(body)["upload_id"]
        assert request_resource("PATCH", "/uploads/" + avatar_id, avatar_data, extra={"Upload-Offset": "0"})[0] == 200
        assert request_resource("POST", "/uploads/" + avatar_id + "/complete")[0] == 200
        avatar_body = json.dumps(dict(resource_id=avatar_id)).encode()
        assert request_resource("PUT", "/avatars/7", avatar_body, uid=8)[0] == 403
        assert request_resource("PUT", "/avatars/7", avatar_body)[0] == 200
        assert json.loads(request_resource("GET", "/avatars/7", uid=8)[1])["resource_id"] == avatar_id
        assert request_resource("GET", "/resources/" + avatar_id, uid=8) == (200, avatar_data)
        resource.terminate(); resource.wait(timeout=10); processes.remove(resource)
        resource = launch(ROOT / "build/windows-servers/Release/ResourceServer/ResourceServer.exe", resource_config, "resource-restarted")
        wait_port(resource_port, resource)
        assert json.loads(request_resource("GET", "/avatars/7", uid=8)[1])["resource_id"] == avatar_id
        data = b"\x89PNG\r\n\x1a\n" + b"resource-flow" * 10000
        code, body = request_resource("POST", "/uploads", json.dumps(dict(name="image.png", media_type="image/png",
            size=str(len(data)), sha256=hashlib.sha256(data).hexdigest())).encode()); assert code == 200, body
        resource_id = json.loads(body)["upload_id"]
        code, body = request_resource("PATCH", "/uploads/" + resource_id, data, extra={"Upload-Offset": "0"}); assert code == 200, body
        code, body = request_resource("POST", "/uploads/" + resource_id + "/complete"); assert code == 200, body
        assert request_resource("GET", "/resources/" + resource_id, uid=8)[0] == 403
        payload = dict(from_uid=7, to_uid=8, chat_id=1, resource_id=resource_id,
                       text_array=[dict(msg_uuid="11111111-1111-4111-8111-111111111111", msg_content="")])
        forged = dict(payload, from_uid=9)
        send(sender, 1016, forged)
        assert sender.recv(1) == b"", "forged sender must be disconnected"
        sender = login(chat_a,7)
        invalid = dict(payload, text_array=[dict(msg_uuid="invalid", msg_content="")])
        send(sender, 1016, invalid); receive(sender, 1017, success=False)
        send(sender, 1016, payload); acknowledgement = receive(sender, 1017)
        notification = receive(receiver, 1018)
        assert resource_id in notification["notify_msgs"][0]["msg_content"]
        assert request_resource("GET", "/resources/" + resource_id, uid=8) == (200, data)
        assert request_resource("GET", "/resources/" + resource_id, uid=9)[0] == 403
        send(sender, 1016, payload); retry = receive(sender, 1017)
        send(receiver, 1020, dict(uid=8)); receive(receiver, 1021)  # No duplicate push before this ordered response.
        assert retry["uuid_msgId"] == acknowledgement["uuid_msgId"]
        text = dict(from_uid=7, to_uid=8, chat_id=1, text_array=[dict(
            msg_uuid="33333333-3333-4333-8333-333333333333", msg_content="text after resource integration")])
        send(sender, 1016, text); text_ack = receive(sender, 1017); receive(receiver, 1018)
        send(sender, 1016, text); text_retry = receive(sender, 1017)
        send(receiver, 1020, dict(uid=8)); receive(receiver, 1021)
        assert text_ack["uuid_msgId"] == text_retry["uuid_msgId"]
        forged_text = dict(text, text_array=[dict(msg_uuid="44444444-4444-4444-8444-444444444444",
                                                msg_content="@resource:v1:{}")])
        send(sender, 1016, forged_text)
        assert receive(sender, 1017, success=False)["commit_error"] == "Conflict"
        receiver.close()
        receiver = login(chat_a, 8)
        send(receiver, 1027, dict(chat_id=1, current_msg_id=0)); history = receive(receiver, 1028)
        assert any(resource_id in message["content"] for message in history["msgs"])
        assert any(message.get("msg_uuid") == payload["text_array"][0]["msg_uuid"] for message in history["msgs"])
        payload["text_array"][0]["msg_uuid"] = "22222222-2222-4222-8222-222222222222"
        send(sender, 1016, payload); receive(sender, 1017); receive(receiver, 1018)
        print("production flow: avatar publication/restart, cross-instance send, idempotent retry, authorized download, relogin/history, same-instance send passed")
        import uuid
        subprocess.run(mysql_command,input="USE resource_test; INSERT IGNORE INTO friend(self_id,other_id,backname) VALUES(7,8,''),(7,9,'');",text=True,check=True,timeout=10)
        send(sender,1034,dict(name="Resources",members=[8],request_id=str(uuid.uuid4())))
        group=receive(sender,1035)["chat_id"]
        video_path = directory / "group-video.avi"
        create_video_fixture(video_path)
        fixtures = [("group.png", "image/png", avatar_png((0, 128, 255))),
                    ("group.avi", "video/x-msvideo", video_path.read_bytes()),
                    ("group.txt", "application/octet-stream", b"group-only-file-content")]
        resources = []
        for name, media_type, group_data in fixtures:
            code, body = request_resource("POST", "/uploads", json.dumps(dict(name=name,
                media_type=media_type, size=str(len(group_data)),
                sha256=hashlib.sha256(group_data).hexdigest())).encode())
            assert code == 200, body
            group_resource = json.loads(body)["upload_id"]
            # Reopen HTTP connections between chunks and recover the acknowledged offset.
            offset = 0
            while offset < len(group_data):
                block = group_data[offset:offset + 262144]
                code, body = request_resource("PATCH", "/uploads/" + group_resource, block,
                                              extra={"Upload-Offset": str(offset)})
                assert code == 200, body
                offset = int(json.loads(body)["offset"])
                code, body = request_resource("GET", "/uploads/" + group_resource)
                assert code == 200 and int(json.loads(body)["offset"]) == offset
            assert request_resource("POST", "/uploads/" + group_resource + "/complete")[0] == 200
            resources.append((group_resource, group_data))

        def commit_group(resource):
            """提交真实群资源并验证相同 UUID 重试返回原消息身份。"""
            payload = dict(from_uid=7, to_uid=0, chat_id=group, chat_type="group", membership_epoch="1",
                           resource_id=resource, text_array=[dict(msg_uuid=str(uuid.uuid4()), msg_content="")])
            send(sender, 1016, payload)
            committed = receive(sender, 1017)
            send(sender, 1016, payload)
            assert receive(sender, 1017)["uuid_msgId"] == committed["uuid_msgId"]
            return committed["uuid_msgId"][0]["message_id"]

        def sync_group(sock, uid, after=0):
            """通过现有正文同步合同获取群资源消息。"""
            send(sock, 1027, dict(mode="sync_v1", uid=uid, chat_id=group, after_id=after,
                                 request_id=str(uuid.uuid4()), membership_epoch="1"))
            return receive(sock, 1028)["msgs"]

        online_ids = [commit_group(resource) for resource, _ in resources]
        assert [row["message_id"] for row in sync_group(receiver, 8)] == online_ids
        receiver.close()
        offline_ids = [commit_group(resource) for resource, _ in resources]
        receiver = login(chat_b, 8)
        offline = sync_group(receiver, 8, online_ids[-1])
        assert [row["message_id"] for row in offline] == offline_ids
        for (resource, expected), message in zip(resources, offline):
            assert resource in message["content"]
            code, downloaded = request_resource("GET", "/resources/" + resource, uid=8)
            assert code == 200
            assert hashlib.sha256(downloaded).digest() == hashlib.sha256(expected).digest()
        import cv2
        restored = directory / "group-downloaded.avi"
        restored.write_bytes(request_resource("GET", "/resources/" + resources[1][0], uid=8)[1])
        capture = cv2.VideoCapture(str(restored))
        frames = 0
        while capture.read()[0]:
            frames += 1
        capture.release()
        assert frames == 250, frames
        send(sender, 1038, dict(chat_id=group, operation="add", members=[9], expected_revision="1",
                               request_id=str(uuid.uuid4())))
        receive(sender, 1039)
        late_member = login(chat_a, 9)
        assert sync_group(late_member, 9) == []
        for resource, _ in resources:
            assert request_resource("GET", "/resources/" + resource, uid=9)[0] == 403
        send(sender, 1038, dict(chat_id=group, operation="remove", target_uid=8, expected_revision="2",
                               request_id=str(uuid.uuid4())))
        receive(sender, 1039)
        for resource, _ in resources:
            assert request_resource("GET", "/resources/" + resource, uid=8)[0] == 403
        # A new independent private reference restores access to the same bytes for every media type.
        for resource, expected in resources:
            private_payload = dict(from_uid=7, to_uid=8, chat_id=1, resource_id=resource,
                text_array=[dict(msg_uuid=str(uuid.uuid4()), msg_content="")])
            send(sender, 1016, private_payload)
            receive(sender, 1017)
            receive(receiver, 1018)
            assert request_resource("GET", "/resources/" + resource, uid=8) == (200, expected)
        send(sender, 1038, dict(chat_id=group, operation="dissolve", expected_revision="3",
                               request_id=str(uuid.uuid4())))
        receive(sender, 1039)
        for resource, expected in resources:
            assert request_resource("GET", "/resources/" + resource, uid=8) == (200, expected)
        # The earlier private-chat reference independently continues to authorize this resource.
        assert request_resource("GET", "/resources/" + resource_id, uid=8) == (200, data)
        print("group PNG/video/file: resumed upload, online/offline sync, UUID replay, SHA-256 download, "
              "250 decoded video frames, join boundary, removal and independent private permission passed")

    finally:
        if sys.exc_info()[0] is not None:
            print("Flow process exit codes before cleanup:", [(p.args[0], p.poll()) for p in processes], flush=True)
        for sock in sockets: sock.close()
        for process in reversed(processes):
            process.terminate(); process.wait(timeout=10)
            if process.stdout: process.stdout.close()
        redis.shutdown(); redis.server_close(); redis_thread.join(timeout=5)
        for log in logs: log.close()
