import os
import socket
import struct
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bencode  # noqa: E402

PSTR = b"BitTorrent protocol"


class Tracker:
    def __init__(self, host="127.0.0.1"):
        self.announces = []
        self.peers = []
        self.lock = threading.Lock()
        tracker = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                parsed = urllib.parse.urlparse(self.path)
                if parsed.path != "/announce":
                    self.send_response(404)
                    self.end_headers()
                    return
                raw = parsed.query
                params = {}
                for part in raw.split("&"):
                    if "=" in part:
                        k, v = part.split("=", 1)
                        params[k] = urllib.parse.unquote_to_bytes(v)
                entry = {
                    "info_hash": params.get("info_hash", b"").hex(),
                    "peer_id": params.get("peer_id", b""),
                    "event": params.get("event", b"").decode(),
                    "left": int(params.get("left", b"0")),
                    "port": int(params.get("port", b"0")),
                    "user_agent": self.headers.get("User-Agent", ""),
                    "query": raw,
                }
                with tracker.lock:
                    tracker.announces.append(entry)
                    compact = b"".join(
                        socket.inet_aton(ip) + struct.pack(">H", port)
                        for ip, port in tracker.peers
                    )
                body = bencode.encode({"interval": 60, "peers": compact})
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = ThreadingHTTPServer((host, 0), Handler)
        self.port = self.server.server_address[1]
        self.url = "http://%s:%d/announce" % (host, self.port)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def announces_for(self, tid):
        with self.lock:
            return [a for a in self.announces if a["info_hash"] == tid]

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class Seed:
    def __init__(self, fixtures, host="127.0.0.1", serve_blocks=True, block_delay=0.0):
        self.fixtures = {f.info_hash: f for f in fixtures}
        self.serve_blocks = serve_blocks
        self.block_delay = block_delay
        self.requests = []
        self.handshakes = []
        self.metadata_requests = 0
        self.bytes_served = 0
        self.lock = threading.Lock()
        self.closing = False
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, 0))
        self.sock.listen(16)
        self.port = self.sock.getsockname()[1]
        self.address = "%s:%d" % (host, self.port)
        self.conns = []
        threading.Thread(target=self._accept, daemon=True).start()

    def add(self, fixture):
        self.fixtures[fixture.info_hash] = fixture

    def request_count(self, tid=None):
        with self.lock:
            return len([r for r in self.requests if tid is None or r[0] == tid])

    def close(self):
        self.closing = True
        try:
            self.sock.close()
        except OSError:
            pass
        for c in list(self.conns):
            try:
                c.close()
            except OSError:
                pass

    def _accept(self):
        while not self.closing:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            self.conns.append(conn)
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    @staticmethod
    def _recv(conn, n):
        buf = b""
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                raise EOFError
            buf += chunk
        return buf

    @staticmethod
    def _send_msg(conn, msg_id, payload=b""):
        conn.sendall(struct.pack(">IB", len(payload) + 1, msg_id) + payload)

    def _serve(self, conn):
        try:
            hs = self._recv(conn, 68)
            if hs[1:20] != PSTR:
                return
            info_hash = hs[28:48]
            fixture = self.fixtures.get(info_hash)
            with self.lock:
                self.handshakes.append(
                    {"info_hash": info_hash.hex(), "peer_id": hs[48:68], "reserved": hs[20:28]}
                )
            if fixture is None:
                return
            reserved = bytearray(8)
            reserved[5] |= 0x10
            conn.sendall(
                b"\x13" + PSTR + bytes(reserved) + info_hash + b"-SD0001-" + b"0" * 12
            )
            ext = bencode.encode(
                {"m": {"ut_metadata": 2}, "metadata_size": len(fixture.info_bytes), "v": "seed"}
            )
            self._send_msg(conn, 20, b"\x00" + ext)
            pieces = (len(fixture.payload) + fixture.piece_length - 1) // fixture.piece_length
            bitfield = bytearray((pieces + 7) // 8)
            for i in range(pieces):
                bitfield[i // 8] |= 0x80 >> (i % 8)
            self._send_msg(conn, 5, bytes(bitfield))
            self._send_msg(conn, 1)
            peer_ut_metadata = 0
            while True:
                header = self._recv(conn, 4)
                (length,) = struct.unpack(">I", header)
                if length == 0:
                    continue
                body = self._recv(conn, length)
                msg_id, payload = body[0], body[1:]
                if msg_id == 2:
                    self._send_msg(conn, 1)
                elif msg_id == 6:
                    index, begin, size = struct.unpack(">III", payload)
                    with self.lock:
                        self.requests.append((fixture.tid, index, begin, size))
                    if not self.serve_blocks:
                        continue
                    if self.block_delay:
                        threading.Event().wait(self.block_delay)
                    offset = index * fixture.piece_length + begin
                    data = fixture.payload[offset : offset + size]
                    self._send_msg(conn, 7, struct.pack(">II", index, begin) + data)
                    with self.lock:
                        self.bytes_served += len(data)
                elif msg_id == 20:
                    ext_id, rest = payload[0], payload[1:]
                    if ext_id == 0:
                        theirs = bencode.decode(rest)
                        peer_ut_metadata = theirs.get(b"m", {}).get(b"ut_metadata", 0)
                    elif ext_id == 2 and peer_ut_metadata:
                        req = bencode.decode(rest)
                        piece = req[b"piece"]
                        with self.lock:
                            self.metadata_requests += 1
                        chunk = fixture.info_bytes[piece * 16384 : (piece + 1) * 16384]
                        head = bencode.encode(
                            {
                                "msg_type": 1,
                                "piece": piece,
                                "total_size": len(fixture.info_bytes),
                            }
                        )
                        self._send_msg(conn, 20, bytes([peer_ut_metadata]) + head + chunk)
        except (EOFError, OSError, struct.error, ValueError):
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass
