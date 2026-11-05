import hmac
import json
import socket
import struct
import threading
import time

MAGIC = 0x4350
VERSION = 0x01
STREAM_VIDEO = 0x01
FLAG_KEYFRAME = 1 << 0
FLAG_CODEC_CONFIG = 1 << 1
FLAG_HDR_10BIT = 1 << 2

def encode_packet(stream_type: int, flags: int, pts_us: int, payload: bytes) -> bytes:
    packed = ((flags & 0xFF) << 24) | (len(payload) & 0x00FFFFFF)
    return struct.pack(">HBBI", MAGIC, VERSION, stream_type, (pts_us >> 32) & 0xFFFFFFFF) + \
           struct.pack(">II", pts_us & 0xFFFFFFFF, packed) + payload

class MockAndroidCamPhoneServer:
    def __init__(self, pin: str):
        self.pin = pin
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(5)
        self.port = self.sock.getsockname()[1]
        self.running = True
        self.last_patch = None
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _loop(self):
        while self.running:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                break
            threading.Thread(target=self._handle, args=(conn,), daemon=True).start()

    def _handle(self, conn: socket.socket):
        with conn:
            data = conn.recv(4096).decode("utf-8", errors="replace")
            if not data:
                return
            expected_auth = f"Authorization: Bearer {self.pin}"
            if not hmac.compare_digest(
                expected_auth if expected_auth in data else "",
                expected_auth
            ):
                conn.sendall(b"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n")
                return

            if data.startswith("GET /v1/capabilities"):
                caps = json.dumps({
                    "deviceModel": "Pixel 9 Pro XL",
                    "soc": "Tensor G4",
                    "codecs": ["hevc", "av1", "avc"],
                    "hdrCapabilities": ["hdr10", "hlg10"],
                    "lenses": [{"id": "0", "label": "Main Wide (24mm)", "facing": "back"}]
                }).encode("utf-8")
                conn.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(caps)}\r\n\r\n".encode() + caps)
            elif data.startswith("PATCH /v1/control"):
                body = data.split("\r\n\r\n", 1)[-1]
                self.last_patch = json.loads(body)
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 18\r\n\r\n{\"status\":\"applied\"}")
            elif data.startswith("GET /v1/stream"):
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/x-camphone-stream\r\n\r\n")
                # Send SPS/PPS Codec Config packet + 4K60 HDR10 Keyframe packet
                conn.sendall(encode_packet(STREAM_VIDEO, FLAG_CODEC_CONFIG, 0, b"\x00\x00\x00\x01\x40\x01"))
                conn.sendall(encode_packet(STREAM_VIDEO, FLAG_KEYFRAME | FLAG_HDR_10BIT, 16666, b"\x00\x00\x00\x01\x26\x01\x99\x88"))
                time.sleep(0.2)

    def close(self):
        self.running = False
        self.sock.close()

def main():
    srv = MockAndroidCamPhoneServer(pin="739204")
    try:
        # 1. Verify unauthorized client is rejected with 401
        with socket.create_connection(("127.0.0.1", srv.port), timeout=2) as s:
            s.sendall(b"GET /v1/capabilities HTTP/1.1\r\nAuthorization: Bearer 000000\r\n\r\n")
            resp = s.recv(1024).decode()
            assert "401 Unauthorized" in resp, f"Expected 401, got {resp}"

        # 2. Verify authorized capability discovery
        with socket.create_connection(("127.0.0.1", srv.port), timeout=2) as s:
            s.sendall(b"GET /v1/capabilities HTTP/1.1\r\nAuthorization: Bearer 739204\r\n\r\n")
            resp = s.recv(4096).decode()
            assert "200 OK" in resp and "Pixel 9 Pro XL" in resp

        # 3. Verify live camera control patch
        with socket.create_connection(("127.0.0.1", srv.port), timeout=2) as s:
            patch = b'{"lens":"0","zoom":2.5,"iso":200,"wbKelvin":5600,"torch":false}'
            req = (
                f"PATCH /v1/control HTTP/1.1\r\n"
                f"Authorization: Bearer 739204\r\n"
                f"Content-Length: {len(patch)}\r\n\r\n"
            ).encode() + patch
            s.sendall(req)
            resp = s.recv(1024).decode()
            assert "200 OK" in resp
            assert srv.last_patch["zoom"] == 2.5

        # 4. Verify binary CamPhoneWire v1 stream framing
        with socket.create_connection(("127.0.0.1", srv.port), timeout=2) as s:
            s.sendall(b"GET /v1/stream?lens=0&res=3840x2160&fps=60&codec=hevc&hdr=1 HTTP/1.1\r\nAuthorization: Bearer 739204\r\n\r\n")
            raw = b""
            while b"\r\n\r\n" not in raw:
                raw += s.recv(1)
            hdr1 = s.recv(16)
            magic, ver, stype, pts, packed = struct.unpack(">HBBQI", hdr1)
            flags = (packed >> 24) & 0xFF
            size = packed & 0x00FFFFFF
            payload1 = s.recv(size)
            assert magic == 0x4350 and ver == 1 and stype == STREAM_VIDEO
            assert (flags & FLAG_CODEC_CONFIG) != 0
            assert payload1.startswith(b"\x00\x00\x00\x01")

        print("[PASS] End-to-End CamPhoneWire v1 Security, Capability Negotiation, Control & Streaming verified.")
    finally:
        srv.close()

if __name__ == "__main__":
    main()
