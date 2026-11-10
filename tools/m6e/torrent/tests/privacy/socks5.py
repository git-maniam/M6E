"""A SOCKS5 server for tests (RFC 1928, RFC 1929 user/password).

Listens on 127.0.0.1 only and connects only to loopback addresses: a target
name is resolved here from a fixed table ("localhost" by default), never by
DNS, and any other target is refused. Every request is recorded, and so is
the local port of every upstream connection it makes, so a test can tell a
proxied connection from a direct one.
"""

import ipaddress
import socket
import struct
import threading

LOOPBACK_NAMES = {"localhost": "127.0.0.1"}


class Socks5Server:
    def __init__(self, username=None, password=None, names=None):
        self.username = username
        self.password = password
        self.names = dict(LOOPBACK_NAMES if names is None else names)
        self.lock = threading.Lock()
        self.requests = []
        self.auth = []
        self.upstream_ports = set()
        self.closing = False
        self.conns = []
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(32)
        self.port = self.sock.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()

    def connects(self):
        with self.lock:
            return [r for r in self.requests if r["cmd"] == "connect"]

    def close(self):
        self.closing = True
        for s in [self.sock] + list(self.conns):
            try:
                s.close()
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

    def _serve(self, conn):
        upstream = None
        try:
            ver, count = self._recv(conn, 2)
            if ver != 5:
                return
            methods = self._recv(conn, count)
            wanted = 2 if self.username is not None else 0
            if wanted not in methods:
                conn.sendall(b"\x05\xff")
                return
            conn.sendall(bytes([5, wanted]))
            if wanted == 2:
                sub, ulen = self._recv(conn, 2)
                user = self._recv(conn, ulen).decode("utf-8", "replace")
                (plen,) = self._recv(conn, 1)
                pw = self._recv(conn, plen).decode("utf-8", "replace")
                good = user == self.username and pw == self.password
                with self.lock:
                    self.auth.append((user, pw, good))
                conn.sendall(b"\x01" + (b"\x00" if good else b"\x01"))
                if not good:
                    return
            ver, cmd, _, atyp = self._recv(conn, 4)
            if atyp == 1:
                host = socket.inet_ntop(socket.AF_INET, self._recv(conn, 4))
            elif atyp == 4:
                host = socket.inet_ntop(socket.AF_INET6, self._recv(conn, 16))
            elif atyp == 3:
                (n,) = self._recv(conn, 1)
                host = self._recv(conn, n).decode("utf-8", "replace")
            else:
                return
            (port,) = struct.unpack(">H", self._recv(conn, 2))
            name = {1: "connect", 2: "bind", 3: "udp-associate"}.get(cmd, str(cmd))
            entry = {"cmd": name, "atyp": atyp, "host": host, "port": port, "ok": False}
            with self.lock:
                self.requests.append(entry)
            if cmd != 1:
                conn.sendall(b"\x05\x07\x00\x01" + b"\x00" * 6)
                return
            target = self.names.get(host, host) if atyp == 3 else host
            try:
                address = ipaddress.ip_address(target)
            except ValueError:
                conn.sendall(b"\x05\x04\x00\x01" + b"\x00" * 6)
                return
            if not address.is_loopback:
                conn.sendall(b"\x05\x02\x00\x01" + b"\x00" * 6)
                return
            family = socket.AF_INET6 if address.version == 6 else socket.AF_INET
            upstream = socket.socket(family, socket.SOCK_STREAM)
            upstream.settimeout(5)
            try:
                upstream.connect((str(address), port))
            except OSError:
                conn.sendall(b"\x05\x05\x00\x01" + b"\x00" * 6)
                return
            upstream.settimeout(None)
            with self.lock:
                self.upstream_ports.add(upstream.getsockname()[1])
                entry["ok"] = True
            self.conns.append(upstream)
            conn.sendall(b"\x05\x00\x00\x01" + socket.inet_aton("127.0.0.1") + b"\x00\x00")
            threading.Thread(target=self._pipe, args=(upstream, conn), daemon=True).start()
            self._pipe(conn, upstream)
        except (EOFError, OSError, ValueError, struct.error):
            pass
        finally:
            for s in (conn, upstream):
                if s is not None:
                    try:
                        s.close()
                    except OSError:
                        pass

    @staticmethod
    def _pipe(src, dst):
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
