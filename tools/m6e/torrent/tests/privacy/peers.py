"""Loopback test peers for the privacy tests: a recording tracker, a
recording seed, plain TCP and UDP listeners, a capturing TCP relay and a web
seed. Everything binds to 127.0.0.1 or ::1 only (spec 8.9, D38).
"""

import os
import socket
import socketserver
import struct
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "fixtures"))

import bencode  # noqa: E402
import swarm  # noqa: E402


class Tracker:
    """An HTTP tracker that records each announce with the client's port and
    headers and answers with a fixed peer list."""

    def __init__(self, peers=None):
        self.peers = list(peers or [])
        self.announces = []
        self.lock = threading.Lock()
        tracker = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                parsed = urllib.parse.urlparse(self.path)
                params = {}
                for part in parsed.query.split("&"):
                    if "=" in part:
                        k, v = part.split("=", 1)
                        params[k] = urllib.parse.unquote_to_bytes(v)
                with tracker.lock:
                    tracker.announces.append(
                        {
                            "client_port": self.client_address[1],
                            "path": parsed.path,
                            "query": parsed.query,
                            "params": params,
                            "headers": dict(self.headers.items()),
                            "user_agent": self.headers.get("User-Agent"),
                        }
                    )
                    compact = b"".join(
                        socket.inet_aton(ip) + struct.pack(">H", port)
                        for ip, port in tracker.peers
                    )
                body = bencode.encode({"interval": 60, "peers": compact})
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.url = "http://127.0.0.1:%d/announce" % self.port
        self.name_url = "http://localhost:%d/announce" % self.port
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def count(self):
        with self.lock:
            return len(self.announces)

    def snapshot(self):
        with self.lock:
            return list(self.announces)

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class _RecordingConn:
    def __init__(self, conn, sink):
        self._conn = conn
        self._sink = sink

    def recv(self, n):
        data = self._conn.recv(n)
        if len(self._sink) < 1 << 16:
            self._sink.extend(data)
        return data

    def __getattr__(self, name):
        return getattr(self._conn, name)


class RecordingSeed(swarm.Seed):
    """The fixture seed, recording the client port of each accepted
    connection and the first 64 KiB the client sent on it."""

    def __init__(self, fixtures, **kwargs):
        self.accepted = []
        self.received = []
        super().__init__(fixtures, **kwargs)

    def _accept(self):
        while not self.closing:
            try:
                conn, peer = self.sock.accept()
            except OSError:
                return
            sink = bytearray()
            with self.lock:
                self.accepted.append(peer[1])
                self.received.append(sink)
            self.conns.append(conn)
            threading.Thread(
                target=self._serve, args=(_RecordingConn(conn, sink),), daemon=True
            ).start()

    def accept_count(self):
        with self.lock:
            return len(self.accepted)


class Listener:
    """Records connection attempts (TCP) or datagrams (UDP) and does nothing
    else."""

    def __init__(self, host="127.0.0.1", udp=False, port=0):
        family = socket.AF_INET6 if ":" in host else socket.AF_INET
        kind = socket.SOCK_DGRAM if udp else socket.SOCK_STREAM
        self.sock = socket.socket(family, kind)
        if family == socket.AF_INET6:
            self.sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        self.sock.bind((host, port))
        self.port = self.sock.getsockname()[1]
        self.udp = udp
        self.hits = []
        self.lock = threading.Lock()
        self.closing = False
        if not udp:
            self.sock.listen(16)
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while not self.closing:
            try:
                if self.udp:
                    data, peer = self.sock.recvfrom(65536)
                    with self.lock:
                        self.hits.append((peer, data))
                else:
                    conn, peer = self.sock.accept()
                    with self.lock:
                        self.hits.append((peer, b""))
                    conn.close()
            except OSError:
                return

    def count(self):
        with self.lock:
            return len(self.hits)

    def datagrams(self):
        with self.lock:
            return [d for _, d in self.hits]

    def close(self):
        self.closing = True
        try:
            self.sock.close()
        except OSError:
            pass


class Relay:
    """A TCP relay from 127.0.0.1 to a target that keeps the first 256 KiB of
    each direction of each connection."""

    def __init__(self, target_port, target_host="127.0.0.1"):
        self.target = (target_host, target_port)
        self.streams = []
        self.lock = threading.Lock()
        self.closing = False
        self.conns = []
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(16)
        self.port = self.sock.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while not self.closing:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            try:
                up = socket.create_connection(self.target, timeout=5)
                up.settimeout(None)
            except OSError:
                conn.close()
                continue
            stream = {"out": bytearray(), "back": bytearray()}
            with self.lock:
                self.streams.append(stream)
            self.conns += [conn, up]
            for args in ((conn, up, stream["out"]), (up, conn, stream["back"])):
                threading.Thread(target=self._pipe, args=args, daemon=True).start()

    @staticmethod
    def _pipe(src, dst, sink):
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                if len(sink) < 1 << 18:
                    sink.extend(data)
                dst.sendall(data)
        except OSError:
            pass
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def captured(self):
        with self.lock:
            return [(bytes(s["out"]), bytes(s["back"])) for s in self.streams]

    def close(self):
        self.closing = True
        for s in [self.sock] + self.conns:
            try:
                s.close()
            except OSError:
                pass


class WebSeed:
    """An HTTP server with Range support serving {url path: bytes}."""

    def __init__(self, files):
        self.files = dict(files)
        self.requests = []
        self.lock = threading.Lock()
        web = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def do_GET(self):
                path = urllib.parse.unquote(urllib.parse.urlparse(self.path).path)
                with web.lock:
                    web.requests.append(
                        {
                            "path": path,
                            "range": self.headers.get("Range"),
                            "user_agent": self.headers.get("User-Agent"),
                            "client_port": self.client_address[1],
                        }
                    )
                data = web.files.get(path)
                if data is None:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                start, end = 0, len(data) - 1
                rng = self.headers.get("Range")
                if rng and rng.startswith("bytes="):
                    a, _, b = rng[6:].partition("-")
                    start = int(a) if a else 0
                    end = min(int(b), len(data) - 1) if b else len(data) - 1
                    self.send_response(206)
                    self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, len(data)))
                else:
                    self.send_response(200)
                body = data[start : end + 1]
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.base = "http://127.0.0.1:%d/" % self.port
        self.name_base = "http://localhost:%d/" % self.port
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def count(self):
        with self.lock:
            return len(self.requests)

    def snapshot(self):
        with self.lock:
            return list(self.requests)

    def close(self):
        self.server.shutdown()
        self.server.server_close()


socketserver.TCPServer.allow_reuse_address = True
