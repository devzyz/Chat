"""Isolated MySQL + production persistence and TCP incremental-sync contracts.

Reuses the existing Redis fixtures; never connects to a personal database.
"""
from contextlib import contextmanager
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


def mysql_value(command, statement):
    """Read one scalar from the disposable suite database."""
    return subprocess.check_output(command + ["message_sync_test", "-BN", "-e", statement],
                                   text=True, timeout=3).strip()


def wait_count(command, statement, count):
    """Bound the wait for an observed lock holder or blocked query."""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if int(mysql_value(command, statement)) >= count:
            return
        time.sleep(0.05)
    raise TimeoutError(f"expected {count} for {statement}")


@contextmanager
def hold_table(command, table):
    """Keep a test-owned MySQL connection locked until both workers reach the barrier."""
    owner = "IS_USED_LOCK('chat_dispatch_test_holder')"
    holder = subprocess.Popen(command + ["message_sync_test", "-e",
        f"LOCK TABLES {table} WRITE; DO GET_LOCK('chat_dispatch_test_holder', 0); DO SLEEP(30)"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_count(command, f"SELECT {owner} IS NOT NULL", 1)
        yield
    finally:
        try:
            connection_id = mysql_value(command, f"SELECT {owner}")
            if connection_id.isdecimal():
                mysql_value(command, f"KILL CONNECTION {int(connection_id)}")
        finally:
            try:
                holder.wait(timeout=5)
            except subprocess.TimeoutExpired:
                holder.kill()
                holder.wait(timeout=5)


def verify_database_dispatch(mysql_command, tcp_port, sender, observer, redis):
    """Revoke both a loading login and an authenticated queued writer while both SQL workers wait."""
    blocked = ("SELECT COUNT(*) FROM information_schema.processlist WHERE DB='message_sync_test' "
               "AND STATE LIKE 'Waiting for table%lock'")
    message_uuid = "b125aa01-fb89-4602-9403-170000000001"
    with socket.create_connection(("127.0.0.1", tcp_port), timeout=8) as valid, \
            socket.create_connection(("127.0.0.1", tcp_port), timeout=8) as revoked:
        with hold_table(mysql_command, "user"):
            for sock, uid in ((valid, 8), (revoked, 9)):
                with redis.lock:
                    credential = redis.hashes[str(uid).encode()][f"utoken_{uid}".encode()].decode()
                send(sock, 1005, {"uid": uid, "token": credential, "capabilities": ["basic_social_v1"]})
            wait_count(mysql_command, blocked, 2)
            admitted = threading.Event()
            with redis.lock:
                redis.refresh_events[b"7"] = admitted
            send(sender, 1016, dict(from_uid=7, to_uid=8, chat_id=201,
                 text_array=[dict(msg_uuid=message_uuid, msg_content="queued before logout")]))
            assert admitted.wait(3), "sender did not reach ingress authentication"
            # The same dispatcher can answer this heartbeat only after it has submitted the write.
            send(observer, 1020, {"uid": 10})
            receive(observer, 1021)
            with redis.lock:
                writer_credential = redis.hashes[b"7"].pop(b"utoken_7")
                login_credential = redis.hashes[b"9"].pop(b"utoken_9")
        receive(valid, 1006)
        # Failed presence publication closes the socket; its best-effort error frame can arrive first.
        try:
            failure = receive(revoked, 1006, success=False)
            assert failure == {"error": 1002}
            assert revoked.recv(1) == b""
        except ConnectionError:
            pass
        assert sender.recv(1) == b"", "revoked queued writer received a frame"
        assert mysql_value(mysql_command, "SELECT COUNT(*) FROM chat_message WHERE send_id=7 "
                           f"AND client_msg_uuid='{message_uuid}'") == "0"
        with redis.lock:
            assert b"usessionid_9" not in redis.values
            redis.hashes[b"7"][b"utoken_7"] = writer_credential
            redis.hashes[b"9"][b"utoken_9"] = login_credential


def verify_concurrent_private_chat(mysql_command, first, second):
    """Race opposite-direction first creates and verify one chat, one pair and identical replies."""
    pair = "SELECT chat_id FROM private_chat WHERE user1_id=8 AND user2_id=9"
    assert not mysql_value(mysql_command, pair)
    before = int(mysql_value(mysql_command, "SELECT COUNT(*) FROM chat"))
    with hold_table(mysql_command, "chat"):
        send(first, 1023, {"self_id": 8, "other_id": 9})
        send(second, 1023, {"self_id": 9, "other_id": 8})
        wait_count(mysql_command, "SELECT COUNT(*) FROM information_schema.processlist "
                   "WHERE DB='message_sync_test' AND STATE LIKE 'Waiting for table%lock' "
                   "AND INFO LIKE 'INSERT INTO chat(%'", 2)
    first_id = receive(first, 1024)["chat_id"]
    second_id = receive(second, 1024)["chat_id"]
    assert first_id == second_id == int(mysql_value(mysql_command, pair))
    assert int(mysql_value(mysql_command, "SELECT COUNT(*) FROM chat")) == before + 1
    send(second, 1023, {"self_id": 9, "other_id": 8})
    assert receive(second, 1024)["chat_id"] == first_id


def verify_failed_binding(sock):
    """An already-bound socket gets only the public error, even with valid credentials for another user."""
    send(sock, 1005, {"uid": 9, "token": "fixture-token"})
    failure = receive(sock, 1006, success=False)
    assert failure == {"error": 1002}, sorted(failure)


def tcp_flow(directory, mysql_command, mysql_port):
    schema = """USE message_sync_test;
    DELETE FROM private_chat WHERE chat_id=101;
    INSERT INTO friend(self_id,other_id,backname) VALUES(7,8,''),(8,7,'');
    INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(201,7,8);
    INSERT INTO chat(chat_id,type) VALUES(201,'private');
    """
    subprocess.run(mysql_command, input=schema, text=True, check=True, timeout=10)
    redis = RedisFixture(uids=(7, 8, 9, 10))
    thread = threading.Thread(target=redis.serve_forever, daemon=True)
    thread.start()
    processes, sockets, logs = [], [], []
    try:
        tcp_ports = [port(), port()]
        rpc_ports = [port(), port()]
        for index in range(2):
            config = directory / f"chat-{index}.ini"
            config.write_text(f"[SelfServer]\nName=sync-{index}\nHost=127.0.0.1\nPort={tcp_ports[index]}\nRPCPort={rpc_ports[index]}\n"
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

        def login(index, uid, social=False, expected_friends=None):
            sock = socket.create_connection(("127.0.0.1", tcp_ports[index]), timeout=10)
            sockets.append(sock)
            capabilities = ["message_receipts_v1", "group_membership_v1"] + (["basic_social_v1"] if social else [])
            send(sock, 1005, {"uid": uid, "token": "fixture-token", "capabilities": capabilities})
            response = receive(sock, 1006)
            assert response["capabilities"] == capabilities
            if expected_friends is not None:
                assert sorted(response["friend_list"], key=lambda row: row["uid"]) == expected_friends
            return sock

        def sync(sock, after):
            request = dict(mode="sync_v1", uid=8, chat_id=201, after_id=after, request_id=f"request-{after}")
            send(sock, 1027, request)
            response = receive(sock, 1028)
            assert response["after_id"] == after and response["request_id"] == request["request_id"]
            return response

        # Warm the DAO before measuring prepared statements on this isolated MySQL instance.
        login(0, 7).close()

        def statement_count():
            output = subprocess.check_output(mysql_command + ["-BN", "-e",
                "SHOW GLOBAL STATUS LIKE 'Com_stmt_execute'"], text=True, timeout=10)
            return int(output.split()[1])

        def measured_login(expected_friends):
            before = statement_count()
            sock = login(0, 7, expected_friends=expected_friends)
            return sock, statement_count() - before

        friends = [dict(uid=8, name="receiver", description="", icon="", sex=0, backname="")]
        one, one_count = measured_login(friends)
        one.close()
        subprocess.run(mysql_command, input="""USE message_sync_test;
            INSERT INTO friend(self_id,other_id,backname) VALUES(7,9,'school'),(7,10,'work');
            UPDATE user SET description='test bio',icon='test.png',sex=1 WHERE uid=9;""",
            text=True, check=True, timeout=10)
        friends.extend([dict(uid=9, name="nine", description="test bio", icon="test.png", sex=1, backname="school"),
                        dict(uid=10, name="ten", description="", icon="", sex=0, backname="work")])
        sender, three_count = measured_login(friends)
        assert three_count == one_count > 0, (one_count, three_count)
        print(f"Legacy login: 1 and 3 friends each used {one_count} prepared statement executions")
        with login(0, 10, social=True) as observer:
            verify_database_dispatch(mysql_command, tcp_ports[0], sender, observer, redis)
            verify_failed_binding(observer)
        sender.close()
        sender = login(0, 7)
        with login(0, 8, social=True) as first, login(0, 9, social=True) as second:
            verify_concurrent_private_chat(mysql_command, first, second)
        print("Database dispatch: revoked queued write rejected, binding error sanitized, concurrent chat IDs match")
        subprocess.run(mysql_command, input="""USE message_sync_test;
            DELETE FROM friend WHERE self_id=7 AND other_id IN (9,10);
            UPDATE user SET description='',icon='',sex=0 WHERE uid=9;""", text=True, check=True, timeout=10)
        # Search identity is optional for legacy clients, mandatory for safe new-client retry.
        search_id = "00000000-0000-4000-8000-000000000701"
        send(sender, 1007, {"uid_name": "8", "request_id": search_id})
        searched = receive(sender, 1008)
        assert searched["uid"] == 8 and searched["request_id"] == search_id
        send(sender, 1007, {"uid_name": "8"})
        legacy_search = receive(sender, 1008)
        assert legacy_search["uid"] == 8 and "request_id" not in legacy_search
        send(sender, 1007, {"uid_name": [], "request_id": search_id})
        rejected_search = receive(sender, 1008, success=False)
        assert rejected_search["error"] != 0 and rejected_search["request_id"] == search_id
        send(sender, 1007, {"uid_name": "2147483647", "request_id": search_id})
        missing_search = receive(sender, 1008, success=False)
        assert missing_search["error"] != 0 and missing_search["request_id"] == search_id
        print("Search: correlated success/error responses and legacy request compatibility passed")

        # Authenticated object requests with missing history fields get a bounded error.
        send(sender, 1027, {})
        malformed = receive(sender, 1028, success=False)
        assert malformed["error"] != 0
        payload = dict(from_uid=7, to_uid=8, chat_id=201,
            text_array=[dict(msg_uuid="00000000-0000-4000-8000-000000000201", msg_content="x" * 1850)])
        send(sender, 1016, payload)
        send(sender, 1027, dict(mode="sync_v1", uid=7, chat_id=201, after_id=0, request_id="read-after-write"))
        first = receive(sender, 1017)["uuid_msgId"][0]["message_id"]
        page = receive(sender, 1028)
        assert page["request_id"] == "read-after-write" and page["msgs"][0]["message_id"] == first
        # Offline recipient no longer turns a committed submission into failure.
        send(sender, 1016, payload)
        assert receive(sender, 1017)["uuid_msgId"][0]["message_id"] == first
        receiver = login(1, 8)
        initial = sync(receiver, 0)
        assert [row["message_id"] for row in initial["msgs"]] == [first]
        assert initial["msgs"][0]["msg_uuid"] == "00000000-0000-4000-8000-000000000201"
        assert initial["msgs"][0]["content"] == "x" * 1850
        assert sync(receiver, first)["msgs"] == []

        def report(level):
            send(receiver, 1029, dict(version=1, request_id=f"report-{level}", chat_id=201,
                items=[dict(message_id=first, level=level)]))
            result = receive(receiver, 1030)
            hint = receive(sender, 1031)
            assert hint["chat_id"] == 201 and hint["latest_revision"] == result["latest_revision"]
            return result

        def receipt_page(sock, after):
            send(sock, 1032, dict(version=1, request_id="receipt-sync", chat_id=201, after_revision=str(after)))
            return receive(sock, 1033)

        assert receipt_page(sender, 0)["items"] == []
        delivered = report("delivered")
        assert delivered["items"][0]["level"] == "delivered"
        assert receipt_page(sender, 0)["items"][0]["level"] == "delivered"
        send(sender, 1029, dict(version=1, request_id="forged", chat_id=201,
            items=[dict(message_id=first, level="read")]))
        assert receive(sender, 1030, success=False)["receipt_error"] == "InvalidMessage"
        read = report("read")
        assert read["items"][0]["level"] == "read"
        assert report("delivered")["items"][0]["level"] == "read"
        assert receipt_page(sender, 1)["items"][0]["message_id"] == first
        assert receipt_page(sender, 2)["items"] == []
        sender.close()
        sender = login(0, 7)
        assert receipt_page(sender, 0)["items"][0]["level"] == "read"
        print("Receipts: cross-instance Delivered/Read, forged reader rejection, late downgrade and relogin passed")
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
        sender.close() # Exercise durable receipt catch-up while the sender is offline.
        subprocess.run([str(probe), str(tcp_ports[1]), str(local_account), str(third), "3", "receipts"],
            check=True, timeout=18)
        sender = login(0, 7)
        confirmed = receipt_page(sender, 0)["items"]
        assert len(confirmed) == 3 and all(item["level"] == "read" for item in confirmed)
        subprocess.run([str(probe), str(tcp_ports[1]), str(local_account), str(third), "3", "receipts"],
            check=True, timeout=18)
        print("Qt/SQLite receipts: durable report, Read intent, confirmed state and process restart passed")
        print("TCP: offline commit, UUID retry, cross-instance push, incremental sync and relogin passed")
        print("Qt/SQLite: process restart resumes from persisted cursor; no old history requested")
        # Fixed-member group journey against two production ChatServer instances.
        receiver = login(1, 8)
        outsider = login(1, 9)
        creation = dict(name="Basic group", members=[8], request_id="00000000-0000-4000-8000-000000009001")
        send(sender, 1034, creation)
        group = receive(sender, 1035)["chat_id"]
        send(sender, 1034, creation)
        assert receive(sender, 1035)["chat_id"] == group
        send(sender, 1034, dict(creation, name="Conflicting name"))
        assert receive(sender, 1035, success=False)["error"] != 0
        send(sender, 1034, dict(creation, members=[10], request_id="00000000-0000-4000-8000-000000009002"))
        assert receive(sender, 1035, success=False)["error"] != 0
        send(receiver, 1025, dict(uid=8, current_chat_id=group - 1))
        listed = receive(receiver, 1026)["chat_list"]
        assert any(row["chat_id"] == group and row["type"] == "group" for row in listed)
        text = dict(from_uid=7, to_uid=0, chat_id=group, chat_type="group", membership_epoch="1",
            text_array=[dict(msg_uuid="00000000-0000-4000-8000-000000009003", msg_content="Hello group")])
        send(sender, 1016, text)
        group_message = receive(sender, 1017)["uuid_msgId"][0]["message_id"]
        send(sender, 1016, text)
        assert receive(sender, 1017)["uuid_msgId"][0]["message_id"] == group_message
        send(sender, 1016, dict(text, chat_type="private"))
        assert receive(sender, 1017, success=False)["error"] != 0
        send(outsider, 1016, dict(text, from_uid=9))
        assert receive(outsider, 1017, success=False)["error"] != 0
        send(outsider, 1027, dict(mode="sync_v1", uid=9, chat_id=group, after_id=0, request_id="outsider"))
        assert receive(outsider, 1028, success=False)["error"] != 0
        send(receiver, 1027, dict(mode="sync_v1", uid=8, chat_id=group, after_id=0, request_id="group-sync"))
        page = receive(receiver, 1028)
        assert len(page["msgs"]) == 1 and page["msgs"][0]["recv_id"] == 0
        assert page["msgs"][0]["content"] == "Hello group"
        send(receiver, 1016, dict(text, from_uid=8))  # UUID identities are sender-scoped.
        reply = receive(receiver, 1017)["uuid_msgId"][0]["message_id"]
        assert reply != group_message
        receiver.close()
        text["text_array"][0] = dict(msg_uuid="00000000-0000-4000-8000-000000009004", msg_content="While offline")
        send(sender, 1016, text)
        offline = receive(sender, 1017)["uuid_msgId"][0]["message_id"]
        receiver = login(1, 8)
        send(receiver, 1027, dict(mode="sync_v1", uid=8, chat_id=group, after_id=reply, request_id="group-restart"))
        page = receive(receiver, 1028)
        assert [row["message_id"] for row in page["msgs"]] == [offline]
        # Long UTF-8 names and more than one directory page stay within the TCP frame bound.
        for index in range(11):
            send(sender, 1034, dict(name="群" * 20, members=[8],
                request_id=f"00000000-0000-4000-8000-{9100 + index:012d}"))
            receive(sender, 1035)
        found = []
        cursor = group - 1
        for _ in range(8):
            send(receiver, 1025, dict(uid=8, current_chat_id=cursor))
            result = receive(receiver, 1026)
            found.extend(row["chat_id"] for row in result["chat_list"])
            cursor = result["current_chat_id"]
            if not result["load_more"]:
                break
        assert len(found) == 12 and len(set(found)) == 12
        receiver.close()
        receiver = login(1, 8)
        print("Group TCP: atomic creation/retry, membership, cross-instance send/sync and offline recovery passed")
        # Dynamic group operations use production protocol, including stale versions and replay after mutation.
        import uuid
        def manage(sock, operation, version, **fields):
            request = dict(chat_id=group, operation=operation, expected_revision=str(version), request_id=str(uuid.uuid4()), **fields)
            send(sock, 1038, request)
            return request, receive(sock, 1039)
        subprocess.run(mysql_command, input="USE message_sync_test; INSERT INTO friend(self_id,other_id,backname) VALUES(7,9,''),(7,10,'');",text=True,check=True,timeout=10)
        request, changed = manage(sender, "add", 1, members=[9,10])
        assert changed["group_revision"] == "2"
        send(sender, 1038, request)
        assert receive(sender, 1039)["group_revision"] == "2"
        send(outsider, 1027, dict(mode="sync_v1",uid=9,chat_id=group,after_id=0,request_id="new-member",membership_epoch="1"))
        assert receive(outsider, 1028)["msgs"] == []
        send(receiver, 1038, dict(request, request_id=str(uuid.uuid4()),expected_revision="2",operation="rename",name="forbidden"))
        assert receive(receiver, 1039, success=False)["group_error"] == "Forbidden"
        _, changed = manage(sender,"rename",2,name="Renamed")
        send(sender,1034,creation)
        assert receive(sender,1035)["chat_id"]==group
        send(sender,1036,dict(chat_id=group,after_uid=0,request_id="members"))
        details=receive(sender,1037)
        assert details["member_count"]==4 and details["group_name"]=="Renamed"
        _, changed=manage(sender,"remove",3,target_uid=8)
        send(receiver,1027,dict(mode="sync_v1",uid=8,chat_id=group,after_id=0,request_id="removed",membership_epoch="1"))
        assert receive(receiver,1028,success=False)["error"]!=0
        _, changed=manage(sender,"add",4,members=[8])
        send(receiver,1016,dict(text,from_uid=8))
        assert receive(receiver,1017,success=False)["error"]!=0
        send(receiver,1027,dict(mode="sync_v1",uid=8,chat_id=group,after_id=0,request_id="rejoined",membership_epoch="2"))
        assert receive(receiver,1028)["msgs"]==[]
        _, changed=manage(sender,"transfer",5,target_uid=9)
        _, changed=manage(sender,"leave",6)
        assert changed["group_state"]=="left"
        request,changed=manage(outsider,"dissolve",7)
        assert changed["group_state"]=="dissolved"
        send(outsider,1038,request)
        assert receive(outsider,1039)["group_revision"]=="8"
        send(receiver,1025,dict(uid=8,current_chat_id=group-1))
        assert receive(receiver,1026)["chat_list"][0]["group_state"]=="dissolved"
        remark=dict(target_uid=8,name="Local nickname",request_id=str(uuid.uuid4()))
        send(sender,1040,remark)
        assert receive(sender,1041)["name"]=="Local nickname"
        sender.close(); sender=login(0,7)
        verify=subprocess.run(mysql_command+["-N","-B","-e","USE message_sync_test; SELECT backname FROM friend WHERE self_id=7 AND other_id=8; SELECT backname FROM friend WHERE self_id=8 AND other_id=7;"],capture_output=True,text=True,check=True,timeout=10)
        assert verify.stdout.splitlines()==["Local nickname", ""]
        print("Dynamic group TCP: four accounts, version conflict, membership epochs, history boundary, transfer/leave/dissolve, replay and personal remark passed")

        sender.close(); receiver.close()
        sender, receiver = login(0,7,True), login(1,8,True)

        def social(sock, code, success=True, **request):
            request["request_id"] = str(uuid.uuid4())
            send(sock,code,request)
            result = receive(sock,code+1,success=success)
            assert result["request_id"] == request["request_id"]
            return result

        profile = social(sender,1046,kind="profile")["profile"]
        assert not {"password","pwd","email","token"}.intersection(profile)
        updated = social(sender,1042,name="sender updated",description="current profile",expected_revision=profile["profile_revision"])
        assert updated["profile"]["profile_revision"] == "2"
        assert social(sender,1042,False,name="stale",description="",expected_revision="1")["social_error"] == "VersionConflict"
        assert social(sender,1042,False,name="receiver",description="",expected_revision="2")["social_error"] == "NameExists"
        send(receiver,1007,dict(uid_name="sender updated",request_id="profile-search"))
        found = receive(receiver,1008)
        assert found["uid"] == 7 and found["description"] == "current profile"
        assert not {"password","pwd","email","token"}.intersection(found)
        social(sender,1044,operation="delete",target_uid=8,expected_revision="1")
        for sock,peer in [(sender,8),(receiver,7)]:
            relation = social(sock,1046,kind="profile",target_uid=peer)["profile"]
            assert not relation["relationship_active"] and relation["relationship_revision"] == "2"
        stale = dict(from_uid=7,to_uid=8,chat_id=201,relationship_revision="1",
            text_array=[dict(msg_uuid=str(uuid.uuid4()),msg_content="must not be sent")])
        send(sender,1016,stale); assert receive(sender,1017,success=False)["error"] != 0
        confirmed = dict(from_uid=7,to_uid=8,chat_id=201,relationship_revision="1",
            text_array=[dict(msg_uuid="00000000-0000-4000-8000-000000000201",msg_content="x"*1850)])
        send(sender,1016,confirmed); assert receive(sender,1017)["uuid_msgId"][0]["message_id"] == first
        assert sync(receiver,0)["msgs"][0]["message_id"] == first
        outgoing = social(sender,1046,kind="profile",target_uid=8)["profile"]["outgoing_revision"]
        social(sender,1044,operation="apply",target_uid=8,expected_revision=outgoing,description="again",backname="receiver")
        applications = social(receiver,1046,kind="applications",after="0")["items"]
        assert all(row["touid"] == 8 and "backname" in row for row in applications)
        version = next(row["application_revision"] for row in applications if row["fromuid"] == 7)
        social(receiver,1044,operation="reject",target_uid=7,expected_revision=version)
        assert social(receiver,1044,False,operation="accept",target_uid=7,expected_revision=version,description="",backname="")["social_error"] == "VersionConflict"
        outgoing = social(sender,1046,kind="profile",target_uid=8)["profile"]["outgoing_revision"]
        social(sender,1044,operation="apply",target_uid=8,expected_revision=outgoing,description="new application",backname="receiver")
        version = social(receiver,1046,kind="applications",after="0")["items"][0]["application_revision"]
        social(receiver,1044,operation="accept",target_uid=7,expected_revision=version,description="accepted",backname="my sender")
        relation = social(receiver,1046,kind="profile",target_uid=7)["profile"]
        assert relation["chat_id"] == 201 and relation["relationship_revision"] == "3" and relation["relationship_active"]
        send(sender,1016,stale); assert receive(sender,1017,success=False)["error"] != 0
        stale["relationship_revision"] = "3"
        stale["text_array"][0] = dict(msg_uuid=str(uuid.uuid4()),msg_content="fresh explicit send")
        send(sender,1016,stale); receive(sender,1017)
        receiver.close(); receiver=login(1,8,True)
        contacts=social(receiver,1046,kind="contacts",after="0")["items"]
        assert any(row["uid"] == 7 and row["relationship_revision"] == "3" for row in contacts)
        assert social(sender,1042,False,name="x"*256,description="",expected_revision="2")["social_error"] == "InvalidRequest"
        large=dict(from_uid=7,to_uid=8,chat_id=201,relationship_revision="3",
            text_array=[dict(msg_uuid=str(uuid.uuid4()),msg_content="z"*1850)])
        send(sender,1016,large); committed=receive(sender,1017)["uuid_msgId"][0]["message_id"]
        assert receive(receiver,1018)["chat_id"] == 201
        assert sync(receiver,committed-1)["msgs"][0]["content"] == "z"*1850
        # Each legal UTF-8 field fits its write frame, their combined read row exceeds the ordinary frame limit.
        social(sender,1042,name="名"*255,description="述"*255,expected_revision="2")
        social(receiver,1044,operation="delete",target_uid=7,expected_revision="3")
        outgoing=social(sender,1046,kind="profile",target_uid=8)["profile"]["outgoing_revision"]
        social(sender,1044,operation="apply",target_uid=8,expected_revision=outgoing,description="请"*255,backname="备"*255)
        applications=social(receiver,1046,kind="applications",after="0")["items"]
        assert applications[0]["description"] == "请"*255 and applications[0]["applydescription"] == "述"*255
        social(sender,1042,name="名"*255,description="新"*255,expected_revision="3")
        version=applications[0]["application_revision"]
        social(receiver,1044,operation="accept",target_uid=7,expected_revision=version,description="",backname="注"*255)
        relation=social(receiver,1046,kind="profile",target_uid=7)["profile"]
        assert relation["name"] == "名"*255 and relation["description"] == "新"*255 and relation["backname"] == "注"*255
        contacts=social(receiver,1046,kind="contacts",after="0")["items"]
        assert contacts[0]["uid"] == 7 and contacts[0]["backname"] == "注"*255
        receiver.close(); receiver = login(1, 8, True)
        sender.close(); sender = login(0, 7, True)
        contacts = social(receiver,1046,kind="contacts",after="0")["items"]
        assert contacts[0]["name"] == "名"*255 and contacts[0]["backname"] == "注"*255
        batch = dict(from_uid=7,to_uid=8,chat_id=201,relationship_revision=contacts[0]["relationship_revision"],
            text_array=[dict(msg_uuid=str(uuid.uuid4()),msg_content="x") for _ in range(20)])
        send(sender,1016,batch)
        ack = receive(sender,1017)
        assert len(ack["uuid_msgId"]) == 20
        notification = receive(receiver,1018)
        assert [item["message_id"] for item in notification["notify_msgs"]] == [item["message_id"] for item in ack["uuid_msgId"]]
        send(sender,1016,batch)
        assert receive(sender,1017)["uuid_msgId"] == ack["uuid_msgId"]
        print("Social UTF-8: relogin, complete directory and 20-item ACK retry passed")
        seeds="USE message_sync_test;"
        for uid in range(20,36):
            seeds+=f"INSERT INTO user(uid,name,email,password,description,icon,sex) VALUES({uid},'page{uid}','page{uid}@example.invalid','','','',0);"
            seeds+=f"INSERT INTO apply_friend(from_uid,to_uid,description,backname) VALUES({uid},8,REPEAT('x',255),'');"
        subprocess.run(mysql_command+["-e",seeds],capture_output=True,text=True,check=True,timeout=10)
        after="0"; seen=set(); pages=0
        while True:
            result=social(receiver,1046,kind="applications",after=after); pages+=1
            for row in result["items"]:
                assert row["fromuid"] not in seen; seen.add(row["fromuid"])
            if not result["load_more"]: break
            assert int(result["next"]) > int(after); after=result["next"]
            assert pages < 25
        assert pages > 1 and seen == {7,*range(20,36)}
        print("Basic social TCP: profile conflict, safe projection, bilateral deletion, history/UUID confirmation, reject/reapply/accept and stale-send denial passed")

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
