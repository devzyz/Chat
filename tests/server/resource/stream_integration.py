"""Real loopback HTTP and filesystem tests; authentication is a test-host fake."""
import hashlib
import http.client
import json
import pathlib
import socket
import subprocess
import tempfile
import time
import unittest
import queue
import threading
import struct
import zlib
import sys


def avatar_png(color=(255, 0, 0), size=256):
    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    pixels = (b"\0" + bytes(color) * size) * size
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))

ROOT = pathlib.Path(__file__).resolve().parents[3]
EXE = ROOT / "build/resource/ResourceTests.exe"


def process_line(process, timeout=10):
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
    try:
        value = lines.get(timeout=timeout)
    except queue.Empty:
        process.terminate(); process.wait(timeout=5)
        raise TimeoutError("test host did not report readiness")
    if not value:
        raise RuntimeError("test host exited before readiness")
    return value


class StreamIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="chat-resource-http-")
        cls.path = pathlib.Path(cls.temp.name)
        cls.start_host()

    @classmethod
    def start_host(cls):
        cls.host = subprocess.Popen([str(EXE), "--serve-test", str(cls.path / "store")],
                                    stdout=subprocess.PIPE, text=True,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        cls.port = int(process_line(cls.host).strip())

    @classmethod
    def tearDownClass(cls):
        cls.host.terminate()
        cls.host.wait(timeout=10)
        cls.host.stdout.close()
        cls.temp.cleanup()

    def request(self, method, path, data=b"", headers=None, uid=7):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)
        all_headers = {"X-User-Id": str(uid), "Authorization": "Bearer fixture-token"}
        all_headers.update(headers or {})
        conn.request(method, path, data, all_headers)
        response = conn.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        conn.close()
        return result

    def create(self, data, name="image.png", media_type="image/png"):
        payload = json.dumps(dict(name=name, media_type=media_type, size=str(len(data)),
                                  sha256=hashlib.sha256(data).hexdigest())).encode()
        code, _, body = self.request("POST", "/uploads", payload)
        self.assertEqual(code, 200, body)
        return json.loads(body)["upload_id"]

    def upload_rest(self, upload_id, data, offset):
        while offset < len(data):
            block = data[offset:offset + 262144]
            code, _, body = self.request("PATCH", "/uploads/" + upload_id, block,
                                         {"Upload-Offset": str(offset)})
            self.assertEqual(code, 200, body)
            offset = int(json.loads(body)["offset"])
        code, _, body = self.request("POST", "/uploads/" + upload_id + "/complete")
        self.assertEqual(code, 200, body)

    def interrupt(self, upload_id, data):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        header = (f"PATCH /uploads/{upload_id} HTTP/1.1\r\nHost: localhost\r\n"
                  "X-User-Id: 7\r\nAuthorization: Bearer fixture-token\r\n"
                  f"Upload-Offset: 0\r\nContent-Length: {min(len(data), 1048576)}\r\n\r\n")
        sock.sendall(header.encode() + data[:73451])
        sock.shutdown(socket.SHUT_WR)
        sock.close()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            code, _, body = self.request("GET", "/uploads/" + upload_id)
            self.assertEqual(code, 200, body)
            offset = int(json.loads(body)["offset"])
            if offset == 73451:
                return offset
        self.fail("interrupted prefix was not persisted")

    def test_image_resume_and_download_range(self):
        data = b"\x89PNG\r\n\x1a\n" + bytes(range(256)) * 5000
        upload_id = self.create(data)
        offset = self.interrupt(upload_id, data)
        code, _, _ = self.request("PATCH", "/uploads/" + upload_id, b"x", {"Upload-Offset": "0"})
        self.assertEqual(code, 409)
        self.upload_rest(upload_id, data, offset)
        code, headers, content = self.request("GET", "/resources/" + upload_id, headers={"Range": "bytes=73451-"})
        self.assertEqual(code, 206)
        self.assertEqual(data[:73451] + content, data)
        self.assertEqual(headers["Content-Length"], str(len(data) - 73451))
        code, _, _ = self.request("GET", "/resources/" + upload_id, uid=8)
        self.assertEqual(code, 403)

    def test_video_ten_seconds_resume_after_restart(self):
        import cv2
        import numpy as np
        video = self.path / "ten-seconds.avi"
        writer = cv2.VideoWriter(str(video), cv2.VideoWriter_fourcc(*"MJPG"), 25, (320, 240))
        self.assertTrue(writer.isOpened())
        random = np.random.default_rng(42)
        for frame in range(250):
            pixels = random.integers(0, 256, (240, 320, 3), dtype=np.uint8)
            cv2.putText(pixels, str(frame), (20, 80), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
            writer.write(pixels)
        writer.release()
        capture = cv2.VideoCapture(str(video))
        self.assertAlmostEqual(capture.get(cv2.CAP_PROP_FRAME_COUNT) / capture.get(cv2.CAP_PROP_FPS), 10)
        capture.release()
        data = video.read_bytes()
        upload_id = self.create(data, "ten-seconds.avi", "video/x-msvideo")
        offset = self.interrupt(upload_id, data)
        type(self).host.terminate(); type(self).host.wait(timeout=10); type(self).host.stdout.close()
        type(self).start_host()
        code, _, body = self.request("GET", "/uploads/" + upload_id)
        self.assertEqual(code, 200)
        self.assertEqual(int(json.loads(body)["offset"]), offset)
        self.upload_rest(upload_id, data, offset)
        code, _, downloaded = self.request("GET", "/resources/" + upload_id)
        self.assertEqual(code, 200)
        self.assertEqual(hashlib.sha256(downloaded).digest(), hashlib.sha256(data).digest())
        restored = self.path / "downloaded.avi"; restored.write_bytes(downloaded)
        capture = cv2.VideoCapture(str(restored)); frames = 0
        while capture.read()[0]: frames += 1
        capture.release(); self.assertEqual(frames, 250)
        print(f"video: 10 seconds, {len(data)} bytes, resumed at {offset}, 250 frames decoded")

    def test_keep_alive_and_authentication(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        for _ in range(2):
            conn.request("GET", "/health")
            response = conn.getresponse(); self.assertEqual(response.status, 200); response.read()
            self.assertIsNotNone(conn.sock)
        conn.close()
        code, _, _ = self.request("POST", "/uploads", b"{}", {"Authorization": "Bearer wrong"})
        self.assertEqual(code, 401)

    def test_avatar_publication_permissions_and_validation(self):
        data = avatar_png()
        first = self.create(data)
        body = json.dumps({"resource_id": first}).encode()
        self.assertEqual(self.request("PUT", "/avatars/7", body)[0], 409)
        self.upload_rest(first, data, 0)
        self.assertEqual(self.request("GET", "/resources/" + first, uid=8)[0], 403)
        self.assertEqual(self.request("PUT", "/avatars/7", body, uid=8)[0], 403)
        self.assertEqual(self.request("PUT", "/avatars/8", body, uid=8)[0], 403)
        self.assertEqual(self.request("PUT", "/avatars/7", body)[0], 200)
        self.assertEqual(self.request("PUT", "/avatars/7", body)[0], 200)
        self.assertEqual(self.request("GET", "/resources/" + first, uid=8)[2], data)
        for invalid in (avatar_png(size=255), data[:40], data[:-1] + b"x"):
            item = self.create(invalid)
            self.upload_rest(item, invalid, 0)
            self.assertEqual(self.request("PUT", "/avatars/7", json.dumps({"resource_id": item}).encode())[0], 415)
            current = json.loads(self.request("GET", "/avatars/7", uid=8)[2])
            self.assertEqual(current["resource_id"], first)
        self.assertEqual(self.request("GET", "/avatars/../7")[0], 400)
        self.assertEqual(self.request("PUT", "/avatars/7", b'{"resource_id":123}')[0], 400)

    def test_generic_attachment_bytes(self):
        data = b"arbitrary document content" * 1000
        item = self.create(data, "notes.txt", "application/octet-stream")
        self.upload_rest(item, data, 0)
        self.assertEqual(self.request("GET", "/resources/" + item)[2], data)


if __name__ == "__main__":
    # Windows PowerShell treats native stderr progress as error records even on
    # success. Keep diagnostics in stdout and preserve unittest's failure exit code.
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
