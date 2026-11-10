import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bencode  # noqa: E402


def deterministic_bytes(seed, length):
    out = bytearray()
    counter = 0
    while len(out) < length:
        out += hashlib.sha256(b"%s:%d" % (seed.encode(), counter)).digest()
        counter += 1
    return bytes(out[:length])


class Fixture:
    def __init__(self, name, files, piece_length=16384, announce=None, private=False):
        self.name = name
        self.files = files
        self.piece_length = piece_length
        self.announce = announce
        self.private = private
        self.info = self._info()
        self.info_bytes = bencode.encode(self.info)
        self.info_hash = hashlib.sha1(self.info_bytes).digest()
        self.tid = self.info_hash.hex()

    @property
    def payload(self):
        return b"".join(data for _, data in self.files)

    def _info(self):
        payload = self.payload
        pieces = b"".join(
            hashlib.sha1(payload[i : i + self.piece_length]).digest()
            for i in range(0, len(payload), self.piece_length)
        )
        info = {
            "name": self.name,
            "piece length": self.piece_length,
            "pieces": pieces,
        }
        if len(self.files) == 1 and self.files[0][0] is None:
            info["length"] = len(self.files[0][1])
        else:
            info["files"] = [
                {"length": len(data), "path": list(path)} for path, data in self.files
            ]
        if self.private:
            info["private"] = 1
        return info

    def torrent_bytes(self, extra=None):
        top = {"info": self.info, "created by": "m6e-test"}
        if self.announce:
            top["announce"] = self.announce
        if extra:
            top.update(extra)
        return bencode.encode(top)

    def write(self, path, extra=None):
        with open(path, "wb") as f:
            f.write(self.torrent_bytes(extra))

    def write_content(self, save_path):
        for rel, data in self.files:
            if rel is None:
                target = os.path.join(save_path, self.name)
            else:
                target = os.path.join(save_path, self.name, *rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as f:
                f.write(data)

    def magnet(self, trackers=(), peers=()):
        uri = "magnet:?xt=urn:btih:%s&dn=%s" % (self.tid, self.name)
        for t in trackers:
            uri += "&tr=" + t.replace(":", "%3A").replace("/", "%2F")
        for p in peers:
            uri += "&x.pe=" + p
        return uri


def multi_file(announce=None, private=False, name="m6e-multi", piece_length=16384):
    files = [
        (("a.txt",), deterministic_bytes(name + "a", 40000)),
        (("sub", "b.bin"), deterministic_bytes(name + "b", 70000)),
        (("sub", "deep", "c.bin"), deterministic_bytes(name + "c", 25000)),
    ]
    return Fixture(name, files, piece_length, announce, private)


def single_file(announce=None, name="m6e-single.bin", size=100000, piece_length=16384):
    return Fixture(
        name, [(None, deterministic_bytes(name, size))], piece_length, announce, False
    )


def hostile(kind, announce=None):
    base = multi_file(announce, name="m6e-hostile-" + kind)
    info = dict(base.info)
    pieces = info["pieces"]
    if kind == "traversal":
        info["files"] = [{"length": 10, "path": ["..", "escape.txt"]}]
    elif kind == "traversal-mid":
        info["files"] = [{"length": 10, "path": ["ok", "..", "escape.txt"]}]
    elif kind == "absolute":
        info["files"] = [{"length": 10, "path": ["/etc", "passwd"]}]
    elif kind == "empty-component":
        info["files"] = [{"length": 10, "path": ["a", "", "b"]}]
    elif kind == "dot":
        info["files"] = [{"length": 10, "path": [".", "b"]}]
    elif kind == "backslash":
        info["files"] = [{"length": 10, "path": ["a\\b"]}]
    elif kind == "colon":
        info["files"] = [{"length": 10, "path": ["C:", "b"]}]
    elif kind == "nul":
        info["files"] = [{"length": 10, "path": [b"a\x00b"]}]
    elif kind == "name-dotdot":
        info["name"] = ".."
    elif kind == "name-slash":
        info["name"] = "x/y"
    elif kind == "single-name-traversal":
        info = {
            "name": "../escape.bin",
            "piece length": 16384,
            "pieces": pieces[:20],
            "length": 100,
        }
    else:
        raise ValueError(kind)
    if "files" in info:
        total = sum(f["length"] for f in info["files"])
        info["pieces"] = hashlib.sha1(b"x" * total).digest()
    top = {"info": info}
    if announce:
        top["announce"] = announce
    return bencode.encode(top)


def too_deep(depth=100):
    nested = b"l" * depth + b"e" * depth
    base = multi_file(name="m6e-deep")
    return b"d5:extra" + nested + b"4:info" + base.info_bytes + b"e"


def malformed(kind):
    base = multi_file(name="m6e-malformed").torrent_bytes()
    if kind == "truncated":
        return base[: len(base) // 2]
    if kind == "garbage":
        return b"this is not a torrent file\n"
    if kind == "empty":
        return b""
    if kind == "no-info":
        return bencode.encode({"announce": "http://x/"})
    if kind == "bad-pieces":
        top = bencode.decode(base)
        top[b"info"][b"pieces"] = b"short"
        return bencode.encode(top)
    raise ValueError(kind)


def oversized(path, size=17 * 1024 * 1024):
    base = multi_file(name="m6e-oversized").torrent_bytes({"padding": b"x" * size})
    with open(path, "wb") as f:
        f.write(base)


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    multi_file().write(os.path.join(out, "multi.torrent"))
    multi_file(private=True, name="m6e-private").write(os.path.join(out, "private.torrent"))
    single_file().write(os.path.join(out, "single.torrent"))
