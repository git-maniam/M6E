"""Synthetic fixtures for the security tests: torrents with BEP 47 attributes,
a hybrid (BEP 52) torrent, and a seed that has only some pieces. Everything is
generated here and served on 127.0.0.1 only (spec 8.9, D38)."""

import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
TORRENT_DIR = os.path.dirname(TESTS)
sys.path.insert(0, os.path.join(TORRENT_DIR, "fixtures"))
sys.path.insert(0, os.path.join(TESTS, "privacy"))

import bencode  # noqa: E402
import make_torrent  # noqa: E402
import swarm  # noqa: E402
import v2torrent  # noqa: E402


def rehash(fx):
    fx.info_bytes = bencode.encode(fx.info)
    fx.info_hash = hashlib.sha1(fx.info_bytes).digest()
    fx.tid = fx.info_hash.hex()
    return fx


def with_attrs(fx, attrs):
    """Sets the BEP 47 `attr` string of the files at the given indexes."""
    info = dict(fx.info)
    files = [dict(f) for f in info["files"]]
    for index, attr in attrs.items():
        files[index]["attr"] = attr
    info["files"] = files
    fx.info = info
    return rehash(fx)


def executable_fixture(name, big=120000):
    """Two small files marked executable (`x`) in piece 0, then a big file
    that a partial seed holds back."""
    files = [
        (("Play.command",), make_torrent.deterministic_bytes(name + "p", 1000)),
        (("X.app", "Contents", "MacOS", "X"), make_torrent.deterministic_bytes(name + "x", 1500)),
        (("big.bin",), make_torrent.deterministic_bytes(name + "b", big)),
    ]
    return with_attrs(make_torrent.Fixture(name, files), {0: b"x", 1: b"x"})


class HybridFixture:
    """A single-file torrent with both v1 and v2 (BEP 52) info."""

    def __init__(self, name, data, piece_length=32768):
        v2 = v2torrent.V2Fixture(name, data, piece_length)
        info = dict(v2.info)
        info["length"] = len(data)
        info["pieces"] = b"".join(
            hashlib.sha1(data[i : i + piece_length]).digest()
            for i in range(0, len(data), piece_length)
        )
        self.name = name
        self.data = data
        self.piece_length = piece_length
        self.pieces_root = v2.pieces_root
        self.layers = v2.layers
        self.info = info
        self.info_bytes = bencode.encode(info)
        self.tid = hashlib.sha1(self.info_bytes).hexdigest()
        self.v2_hex = hashlib.sha256(self.info_bytes).hexdigest()
        self.v2_tid = self.v2_hex[:40]

    def torrent_bytes(self, announce=None):
        top = {"info": self.info, "created by": "m6e-test"}
        if self.layers:
            top["piece layers"] = {self.pieces_root: self.layers}
        if announce:
            top["announce"] = announce
        return bencode.encode(top)

    def btmh_magnet(self, tracker=None, peers=()):
        uri = "magnet:?xt=urn:btmh:1220%s&dn=%s" % (self.v2_hex, self.name)
        if tracker:
            uri += "&tr=" + tracker.replace(":", "%3A").replace("/", "%2F")
        for p in peers:
            uri += "&x.pe=" + p
        return uri

    def write_content(self, save):
        with open(os.path.join(save, self.name), "wb") as f:
            f.write(self.data)


class PartialSeed(swarm.Seed):
    """A seed that has, and serves, only the pieces in `have`."""

    def __init__(self, fixtures, have, **kwargs):
        self.have = set(have)
        super().__init__(fixtures, **kwargs)

    def _serve(self, conn):
        try:
            hs = self._recv(conn, 68)
            if hs[1:20] != swarm.PSTR:
                return
            info_hash = hs[28:48]
            fixture = self.fixtures.get(info_hash)
            if fixture is None:
                return
            conn.sendall(
                b"\x13" + swarm.PSTR + bytes(8) + info_hash + b"-SD0001-" + b"0" * 12
            )
            pieces = (len(fixture.payload) + fixture.piece_length - 1) // fixture.piece_length
            bitfield = bytearray((pieces + 7) // 8)
            for i in self.have:
                if i < pieces:
                    bitfield[i // 8] |= 0x80 >> (i % 8)
            self._send_msg(conn, 5, bytes(bitfield))
            self._send_msg(conn, 1)
            while True:
                (length,) = struct.unpack(">I", self._recv(conn, 4))
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
                    if index not in self.have:
                        continue
                    offset = index * fixture.piece_length + begin
                    data = fixture.payload[offset : offset + size]
                    self._send_msg(conn, 7, struct.pack(">II", index, begin) + data)
                    with self.lock:
                        self.bytes_served += len(data)
        except (EOFError, OSError, struct.error, ValueError):
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass


def partial_swarm(fixture, have):
    tracker = swarm.Tracker()
    seed = PartialSeed([fixture], have)
    tracker.peers.append(("127.0.0.1", seed.port))
    return tracker, seed
