"""Isolated MySQL + production persistence and TCP incremental-sync contracts.

Reuses the existing Redis/Status fixtures; never connects to a personal database.
"""
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/server/resource"))
from chat_flow_integration import RedisFixture, port, wait_port, send, receive
from stream_integration import process_line


def tcp_flow(directory, mysql_command, mysql_port):
    schema = """USE message_sync_test;
    DELETE FROM private_chat WHERE chat_id=101;
    INSERT INTO friend(self_id,other_id,backname) VALUES(7,8,''),(8,7,'');
    INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(201,7,8);
    INSERT INTO chat(chat_id,type) VALUES(201,'private');
    """
    subprocess.run(mysql_command, input=schema, text=True, check=True, timeout=10)
    redis = RedisFixture()
    thread = threading.Thread(target=redis.serve_forever, daemon=True)
    thread.start()
    processes, sockets, logs = [], [], []
    try:
        status = subprocess.Popen([str(ROOT / "build/resource/ResourceTests.exe"), "--serve-status-fixture"],
            stdout=subprocess.PIPE, text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        processes.append(status)
        status_port = int(process_line(status))
        tcp_ports = [port(), port()]
        rpc_ports = [port(), port()]
        for index in range(2):
            config = directory / f"chat-{index}.ini"
            config.write_text(f"[SelfServer]\nName=sync-{index}\nHost=127.0.0.1\nPort={tcp_ports[index]}\nRPCPort={rpc_ports[index]}\n"
                f"[StatusServer]\nHost=127.0.0.1\nPort={status_port}\n"
                f"[Redis]\nHost=127.0.0.1\nPort={redis.server_address[1]}\nPassword=fixture\n"
                f"[PeerServer]\nServers=Peer\n[Peer]\nName=sync-{1-index}\nHost=127.0.0.1\nPort={rpc_ports[1-index]}\n"
                f"[Mysql]\nHost=127.0.0.1\nPort={mysql_port}\nUser=root\nPassword=\nSchema=message_sync_test\n"
                f"[Log]\nName=sync-{index}\nLogDir={directory / 'logs'}\nMaxSizeMB=2\nMaxTotalFiles=2\nLevel=debug\nFlushLevel=debug\n")
            log = open(directory / f"chat-{index}.stdout", "wb")
            logs.append(log)
            process = subprocess.Popen([str(ROOT / "build/windows-servers/Release/ChatServer/ChatServer.exe"),
                "--config", str(config)], stdout=log, stderr=log,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            processes.append(process)
            try:
                wait_port(tcp_ports[index], process)
            except Exception:
                log.flush()
                print(f"ChatServer startup exit={process.poll()}: " +
                    (directory / f"chat-{index}.stdout").read_text(errors="replace"))
                raise

        def login(index, uid):
            sock = socket.create_connection(("127.0.0.1", tcp_ports[index]), timeout=10)
            sockets.append(sock)
            send(sock, 1005, dict(uid=uid, token="fixture-token"))
            receive(sock, 1006)
            return sock

        def sync(sock, after):
            request = dict(mode="sync_v1", uid=8, chat_id=201, after_id=after, request_id=f"request-{after}")
            send(sock, 1027, request)
            response = receive(sock, 1028)
            assert response["after_id"] == after and response["request_id"] == request["request_id"]
            return response

        sender = login(0, 7)
        # Preserve the legacy malformed-history error response.
        send(sender, 1027, [])
        malformed = receive(sender, 1028, success=False)
        assert malformed["error"] != 0
        payload = dict(from_uid=7, to_uid=8, chat_id=201,
            text_array=[dict(msg_uuid="00000000-0000-4000-8000-000000000201", msg_content="x" * 1850)])
        send(sender, 1016, payload)
        first = receive(sender, 1017)["uuid_msgId"][0]["message_id"]
        # Offline recipient no longer turns a committed submission into failure.
        send(sender, 1016, payload)
        assert receive(sender, 1017)["uuid_msgId"][0]["message_id"] == first
        receiver = login(1, 8)
        initial = sync(receiver, 0)
        assert [row["message_id"] for row in initial["msgs"]] == [first]
        assert initial["msgs"][0]["msg_uuid"] == "00000000-0000-4000-8000-000000000201"
        assert initial["msgs"][0]["content"] == "x" * 1850
        assert sync(receiver, first)["msgs"] == []
        payload["text_array"][0] = dict(msg_uuid="00000000-0000-4000-8000-000000000202", msg_content="only the increment")
        send(sender, 1016, payload)
        second = receive(sender, 1017)["uuid_msgId"][0]["message_id"]
        receive(receiver, 1018)
        increment = sync(receiver, first)
        assert [row["message_id"] for row in increment["msgs"]] == [second]
        receiver.close()
        receiver = login(1, 8)
        assert sync(receiver, second)["msgs"] == []
        receiver.close()
        probe = ROOT / "build/windows-client/Release/message_sync_probe.exe"
        local_account = directory / "client-account"
        subprocess.run([str(probe), str(tcp_ports[1]), str(local_account), "0", "2"], check=True, timeout=18)
        payload["text_array"][0] = dict(msg_uuid="00000000-0000-4000-8000-000000000203", msg_content="persisted cursor")
        send(sender, 1016, payload)
        third = receive(sender, 1017)["uuid_msgId"][0]["message_id"]
        subprocess.run([str(probe), str(tcp_ports[1]), str(local_account), str(second), "3"], check=True, timeout=18)
        subprocess.run([str(probe), str(tcp_ports[1]), str(local_account), str(third), "3"], check=True, timeout=18)
        print("TCP: offline commit, UUID retry, cross-instance push, incremental sync and relogin passed")
        print("Qt/SQLite: process restart resumes from persisted cursor; no old history requested")
    finally:
        if sys.exc_info()[0] is not None:
            evidence = ROOT / "build/message-sync/failed-flow"
            evidence.mkdir(parents=True, exist_ok=True)
            for log in logs:
                log.flush()
            for log in list(directory.glob("*.stdout")) + list((directory / "logs").rglob("*")):
                if log.is_file():
                    shutil.copyfile(log, evidence / log.name)
        for sock in sockets:
            sock.close()
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            if process.stdout:
                process.stdout.close()
        redis.shutdown()
        redis.server_close()
        thread.join(timeout=5)
        for log in logs:
            log.close()


def run():
    mysqld, mysql = shutil.which("mysqld"), shutil.which("mysql")
    if not mysqld or not mysql:
        raise RuntimeError("mysqld and mysql are required; dependencies are never restored")
    output = ROOT / "build/message-sync"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mysql-", dir=output) as temporary:
        directory = pathlib.Path(temporary)
        data = directory / "data"
        subprocess.run([mysqld, "--no-defaults", "--initialize-insecure", f"--datadir={data}",
            f"--log-error={directory / 'mysql.log'}"], check=True, timeout=90)
        mysql_port = port()
        process = subprocess.Popen([mysqld, "--no-defaults", f"--datadir={data}",
            "--bind-address=127.0.0.1", f"--port={mysql_port}", "--mysqlx=0",
            f"--log-error={directory / 'mysql.log'}"], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        command = [mysql, "--no-defaults", "-h127.0.0.1", f"-P{mysql_port}", "-uroot", "--connect-timeout=2"]
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                result = subprocess.run(command + ["-e", "SELECT 1"], capture_output=True, timeout=5)
                if result.returncode == 0:
                    break
                if process.poll() is not None:
                    raise RuntimeError("isolated MySQL exited")
                time.sleep(0.1)
            else:
                raise TimeoutError("isolated MySQL startup timed out")
            subprocess.run(command + ["-e", "CREATE DATABASE message_sync_test"], check=True, timeout=10)
            migration_env = os.environ.copy()
            migration_env.update(CHAT_MYSQL_CLIENT=mysql, CHAT_MYSQL_HOST="127.0.0.1",
                CHAT_MYSQL_PORT=str(mysql_port), CHAT_MYSQL_USER="root", CHAT_MYSQL_PASSWORD="",
                CHAT_MYSQL_DATABASE="message_sync_test")
            for _ in range(2):
                subprocess.run(["node", str(ROOT / "schema/migrate.js"), "apply"],
                    env=migration_env, check=True, timeout=60)
            schema = """USE message_sync_test;
                INSERT INTO user(uid,name,email,password,description,icon,sex) VALUES
                (7,'sender','s@example.invalid','','','',0),(8,'receiver','r@example.invalid','','','',0),
                (9,'nine','9@example.invalid','','','',0),(10,'ten','10@example.invalid','','','',0),
                (11,'eleven','11@example.invalid','','','',0);
                UPDATE user_id SET id=11;
                INSERT INTO chat(chat_id,type) VALUES(101,'private'),(102,'private'),(103,'private'),(104,'private');
                INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(101,7,8),(102,7,9),(103,7,10),(104,7,11);
            """
            subprocess.run(command, input=schema, text=True, check=True, timeout=10)
            env = os.environ.copy()
            env["MESSAGE_SYNC_TEST_MYSQL"] = f"tcp://127.0.0.1:{mysql_port}"
            subprocess.run([str(output / "MessageSyncTests.exe"), f"--gtest_output=xml:{output / 'mysql.xml'}"],
                env=env, check=True, timeout=30)
            tcp_flow(directory, command, mysql_port)
        finally:
            try:
                if process.poll() is None:
                    subprocess.run(command + ["-e", "SHUTDOWN"], capture_output=True, timeout=8)
                    process.wait(timeout=10)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)


if __name__ == "__main__":
    run()
