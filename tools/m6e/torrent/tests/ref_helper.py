#!/usr/bin/env python3
"""A small stdlib reference helper that really transfers data from the loopback
fixture swarm. It exists only to prove that run_tests.py works end to end when
no engine-backed helper is available; it is not shipped and not a design.

    run_tests.py --helper "python3 -I tools/m6e/torrent/tests/ref_helper.py"

Environment: M6E_TORRENT_IDLE_SECONDS (default 60).
"""

import base64
import errno
import hashlib
import json
import os
import shutil
import socket
import struct
import sys
import threading
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fixtures"))
import bencode  # noqa: E402

PSTR = b"BitTorrent protocol"
MAX_TORRENT = 16 * 1024 * 1024
MAX_DEPTH = 64
IDLE = float(os.environ.get("M6E_TORRENT_IDLE_SECONDS", "60"))
profile_dir = sys.argv[sys.argv.index("--profile-dir") + 1]
resume_dir = os.path.join(profile_dir, "torrents")
lock = threading.RLock()
out_lock = threading.Lock()
torrents = {}
pending = {}
state = {"listener": None, "idle_since": time.monotonic()}


class Fail(Exception):
    def __init__(self, code, message, **extra):
        super().__init__(message)
        self.code, self.message, self.extra = code, message, extra


def emit(obj):
    with out_lock:
        sys.stdout.write(json.dumps(obj) + "\n")
        sys.stdout.flush()


def safe_component(c):
    if isinstance(c, bytes):
        c = c.decode("utf-8", "replace")
    return bool(c) and c not in (".", "..") and not any(ch in c for ch in "\0:\\/")


def decode_limited(data, depth_limit=MAX_DEPTH):
    def val(pos, depth):
        if depth > depth_limit:
            raise Fail("too-large", "bencoding nested too deeply")
        c = data[pos : pos + 1]
        if c == b"i":
            end = data.index(b"e", pos)
            return int(data[pos + 1 : end]), end + 1
        if c in (b"l", b"d"):
            pos += 1
            items = []
            while data[pos : pos + 1] != b"e":
                v, pos = val(pos, depth + 1)
                items.append(v)
            if c == b"l":
                return items, pos + 1
            return {items[i]: items[i + 1] for i in range(0, len(items) - 1, 2)}, pos + 1
        colon = data.index(b":", pos)
        n = int(data[pos:colon])
        if n < 0 or colon + 1 + n > len(data):
            raise ValueError("short string")
        return data[colon + 1 : colon + 1 + n], colon + 1 + n

    try:
        return val(0, 1)
    except (ValueError, IndexError):
        raise Fail("invalid-torrent", "malformed bencoding")


def parse_info(info):
    if not isinstance(info, dict):
        raise Fail("invalid-torrent", "info is not a dictionary")
    name = info.get(b"name")
    pieces = info.get(b"pieces")
    plen = info.get(b"piece length")
    if not isinstance(name, bytes) or not isinstance(pieces, bytes) or not isinstance(plen, int):
        raise Fail("invalid-torrent", "info lacks name, pieces or piece length")
    if plen <= 0 or len(pieces) % 20:
        raise Fail("invalid-torrent", "bad pieces")
    if not safe_component(name):
        raise Fail("unsafe-path", "unsafe torrent name")
    files = []
    if b"files" in info:
        for i, entry in enumerate(info[b"files"]):
            path = entry.get(b"path")
            if b"symlink path" in entry or b"l" in entry.get(b"attr", b""):
                raise Fail("unsafe-path", "symlink entry")
            if not isinstance(path, list) or not path or not all(isinstance(p, bytes) for p in path):
                raise Fail("invalid-torrent", "bad file path")
            if not all(safe_component(p) for p in path):
                raise Fail("unsafe-path", "unsafe file path")
            files.append({"index": i, "path": "/".join(p.decode() for p in path), "length": entry[b"length"]})
    else:
        files.append({"index": 0, "path": name.decode(), "length": info[b"length"]})
    total = sum(f["length"] for f in files)
    if (total + plen - 1) // plen != len(pieces) // 20:
        raise Fail("invalid-torrent", "piece count does not match the size")
    return {
        "infoHash": hashlib.sha1(bencode.encode(info)).hexdigest(),
        "name": name.decode(),
        "totalSize": total,
        "pieceLength": plen,
        "files": files,
        "trackers": [],
        "webSeeds": [],
        "isPrivate": bool(info.get(b"private")),
        "pieces": base64.b64encode(pieces).decode(),
        "single": b"files" not in info,
    }


def public_metadata(md):
    return {k: v for k, v in md.items() if k not in ("pieces", "single")}


class Torrent:
    def __init__(self, tid, md, trackers=(), peers=()):
        self.tid, self.md = tid, md
        self.trackers, self.peers = list(trackers), list(peers)
        self.state = "pending-confirm"
        self.error = None
        self.save = ""
        self.sequential = False
        self.prio = {}
        self.have = set()
        self.added = 0
        self.stop = threading.Event()
        self.thread = None
        self.downloaded = 0
        self.pieces = base64.b64decode(md["pieces"]) if md else b""

    def wanted(self):
        plen, offset, pieces = self.md["pieceLength"], 0, set()
        for f in self.md["files"]:
            if self.prio.get(f["index"], 2) > 0 and f["length"]:
                pieces.update(range(offset // plen, (offset + f["length"] - 1) // plen + 1))
            offset += f["length"]
        return pieces

    def file_path(self, f):
        if self.md["single"]:
            return os.path.join(self.save, self.md["name"])
        return os.path.join(self.save, self.md["name"], *f["path"].split("/"))

    def status(self):
        want = self.wanted() or {0}
        done = len(self.have & want)
        total = self.md["totalSize"] if self.md else 0
        return {
            "tid": self.tid,
            "name": self.md["name"] if self.md else self.tid,
            "state": self.state,
            "progress": min(1, done / len(want)) if self.md else 0,
            "size": total,
            "downloaded": self.downloaded,
            "uploaded": 0,
            "downKBps": 0,
            "upKBps": 0,
            "etaSeconds": None,
            "ratio": 0,
            "seeds": -1,
            "peers": -1,
            "isPrivate": bool(self.md and self.md["isPrivate"]),
            "savePath": self.save,
            "sequential": self.sequential,
            "addedAt": self.added,
            "error": self.error,
        }

    def save_resume(self):
        os.makedirs(resume_dir, exist_ok=True)
        data = {
            "md": self.md,
            "trackers": self.trackers,
            "peers": self.peers,
            "save": self.save,
            "sequential": self.sequential,
            "prio": {str(k): v for k, v in self.prio.items()},
            "state": self.state,
            "added": self.added,
        }
        tmp = os.path.join(resume_dir, self.tid + ".tmp")
        with open(tmp, "w") as f:
            json.dump(data, f)
        os.replace(tmp, os.path.join(resume_dir, self.tid + ".json"))

    def check_path(self, path):
        rel = os.path.relpath(path, self.save)
        cur = self.save
        for part in rel.split(os.sep):
            cur = os.path.join(cur, part)
            if os.path.islink(cur):
                raise Fail("unsafe-path", "a symlink in the download path")

    def write(self, offset, data):
        pos = 0
        for f in self.md["files"]:
            end = pos + f["length"]
            lo, hi = max(offset, pos), min(offset + len(data), end)
            if lo < hi and self.prio.get(f["index"], 2) > 0:
                path = self.file_path(f)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                self.check_path(path)
                fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
                try:
                    os.pwrite(fd, data[lo - offset : hi - offset], lo - pos)
                finally:
                    os.close(fd)
            pos = end

    def read(self, offset, size):
        buf, pos = b"", 0
        for f in self.md["files"]:
            end = pos + f["length"]
            lo, hi = max(offset, pos), min(offset + size, end)
            if lo < hi:
                try:
                    with open(self.file_path(f), "rb") as fh:
                        fh.seek(lo - pos)
                        chunk = fh.read(hi - lo)
                except OSError:
                    chunk = b""
                buf += chunk.ljust(hi - lo, b"\0")
            pos = end
        return buf

    def piece_size(self, p):
        plen = self.md["pieceLength"]
        return min(plen, self.md["totalSize"] - p * plen)

    def recheck(self):
        self.have = set()
        for p in self.wanted():
            data = self.read(p * self.md["pieceLength"], self.piece_size(p))
            if hashlib.sha1(data).digest() == self.pieces[p * 20 : p * 20 + 20]:
                self.have.add(p)

    def fail(self, code, message):
        self.error = {"code": code, "message": message}
        self.state = "error"
        emit({"event": "error", "tid": self.tid, "code": code, "message": message})
        update_listener()

    def peer_list(self):
        peers = list(self.peers)
        for url in self.trackers:
            try:
                q = "?info_hash=%s&peer_id=%s&port=0&uploaded=0&downloaded=0&left=1&compact=1" % (
                    urllib.parse.quote_from_bytes(bytes.fromhex(self.tid)),
                    urllib.parse.quote_from_bytes(b"-XX0001-" + b"1" * 12),
                )
                with urllib.request.urlopen(url + q, timeout=5) as r:
                    reply = bencode.decode(r.read())
                blob = reply.get(b"peers", b"")
                for i in range(0, len(blob) - 5, 6):
                    peers.append("%s:%d" % (socket.inet_ntoa(blob[i : i + 4]), struct.unpack(">H", blob[i + 4 : i + 6])[0]))
            except (OSError, ValueError, KeyError):
                pass
        return peers

    def run(self):
        want = self.wanted()
        try:
            while not self.stop.is_set() and not (want <= self.have):
                for peer in self.peer_list():
                    host, port = peer.rsplit(":", 1)
                    try:
                        self.fetch(host, int(port), want)
                    except (OSError, EOFError, struct.error):
                        pass
                    if self.stop.is_set() or want <= self.have:
                        break
                self.stop.wait(0.3)
        except OSError as e:
            code = "disk-full" if e.errno == errno.ENOSPC else "io-error"
            self.fail(code, str(e))
            return
        except Fail as e:
            self.fail(e.code, e.message)
            return
        if want <= self.have and not self.stop.is_set():
            self.state = "seeding"
            self.save_resume()
            emit({"event": "completed", "tid": self.tid})
            update_listener()

    def fetch(self, host, port, want):
        s = socket.create_connection((host, port), timeout=5)
        s.settimeout(5)
        try:
            s.sendall(b"\x13" + PSTR + bytes(8) + bytes.fromhex(self.tid) + b"-XX0001-" + b"2" * 12)
            recv(s, 68)
            s.sendall(struct.pack(">IB", 1, 2))
            unchoked = False
            todo = sorted(want - self.have)
            while todo and not self.stop.is_set():
                if not unchoked:
                    msg = read_msg(s)
                    unchoked = msg is not None and msg[0] == 1
                    continue
                p = todo[0]
                size, got = self.piece_size(p), b""
                while len(got) < size and not self.stop.is_set():
                    n = min(16384, size - len(got))
                    s.sendall(struct.pack(">IBIII", 13, 6, p, len(got), n))
                    while True:
                        msg = read_msg(s)
                        if msg and msg[0] == 7:
                            got += msg[1][8:]
                            break
                if self.stop.is_set():
                    return
                if hashlib.sha1(got).digest() != self.pieces[p * 20 : p * 20 + 20]:
                    raise OSError("bad piece")
                try:
                    self.write(p * self.md["pieceLength"], got)
                except OSError as e:
                    code = "disk-full" if e.errno == errno.ENOSPC else "io-error"
                    raise Fail(code, str(e))
                self.have.add(p)
                self.downloaded += len(got)
                todo.pop(0)
        finally:
            s.close()

    def start(self):
        self.stop = threading.Event()
        self.state = "downloading"
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()
        update_listener()

    def halt(self):
        self.stop.set()
        if self.thread:
            self.thread.join(3)


def recv(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError
        buf += chunk
    return buf


def read_msg(s):
    (length,) = struct.unpack(">I", recv(s, 4))
    if length == 0:
        return None
    body = recv(s, length)
    return body[0], body[1:]


def fetch_metadata(t):
    for peer in t.peer_list():
        host, port = peer.rsplit(":", 1)
        try:
            s = socket.create_connection((host, int(port)), timeout=5)
            s.settimeout(5)
            reserved = bytearray(8)
            reserved[5] |= 0x10
            s.sendall(b"\x13" + PSTR + bytes(reserved) + bytes.fromhex(t.tid) + b"-XX0001-" + b"3" * 12)
            recv(s, 68)
            ext = bencode.encode({"m": {"ut_metadata": 1}})
            s.sendall(struct.pack(">IB", len(ext) + 2, 20) + b"\x00" + ext)
            theirs = size = None
            data = b""
            while True:
                msg = read_msg(s)
                if not msg or msg[0] != 20:
                    continue
                if msg[1][0] == 0 and theirs is None:
                    hs = bencode.decode(msg[1][1:])
                    theirs, size = hs[b"m"][b"ut_metadata"], hs[b"metadata_size"]
                    for piece in range((size + 16383) // 16384):
                        body = bencode.encode({"msg_type": 0, "piece": piece})
                        s.sendall(struct.pack(">IB", len(body) + 2, 20) + bytes([theirs]) + body)
                elif msg[1][0] == 1:
                    head, end = decode_limited(msg[1][1:])
                    data += msg[1][1 + end :]
                    if len(data) >= size:
                        s.close()
                        return data
        except (OSError, EOFError, KeyError, ValueError, Fail):
            continue
    return None


def magnet_worker(t):
    info = fetch_metadata(t)
    with lock:
        if pending.get(t.tid) is not t or info is None:
            return
        if hashlib.sha1(info).hexdigest() != t.tid:
            return
        try:
            root, _ = decode_limited(info)
            md = parse_info(root)
        except Fail as e:
            emit({"event": "error", "tid": t.tid, "code": e.code, "message": e.message})
            return
        md["trackers"] = t.trackers
        t.md, t.pieces, t.state = md, base64.b64decode(md["pieces"]), "pending-confirm"
        emit({"event": "metadata", "tid": t.tid, "metadata": public_metadata(md)})


def update_listener():
    active = any(t.state in ("downloading", "seeding", "checking") for t in torrents.values())
    if active and state["listener"] is None:
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        s.listen(5)
        state["listener"] = s
        emit({"event": "listening", "port": s.getsockname()[1]})
    elif not active and state["listener"] is not None:
        state["listener"].close()
        state["listener"] = None
        emit({"event": "closed"})
    if active:
        state["idle_since"] = None
    elif state["idle_since"] is None:
        state["idle_since"] = time.monotonic()


def want_str(a, key):
    v = a.get(key)
    if not isinstance(v, str) or not v:
        raise Fail("bad-request", "%s must be a string" % key)
    return v


def get_torrent(a, table):
    tid = want_str(a, "tid")
    if tid not in table:
        raise Fail("not-found", "no such torrent")
    return table[tid]


def add(a):
    if "file" in a:
        path = want_str(a, "file")
        if not os.path.isabs(path):
            raise Fail("bad-request", "path must be absolute")
        try:
            if os.path.getsize(path) > MAX_TORRENT:
                raise Fail("too-large", "torrent file larger than 16 MiB")
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            raise Fail("io-error" if os.path.exists(path) else "not-found", str(e))
        root, _ = decode_limited(data)
        if not isinstance(root, dict) or b"info" not in root:
            raise Fail("invalid-torrent", "no info dictionary")
        md = parse_info(root[b"info"])
        tid = md["infoHash"]
        if tid in torrents:
            raise Fail("duplicate", "already added", tid=tid)
        trackers = [root[b"announce"].decode()] if isinstance(root.get(b"announce"), bytes) else []
        md["trackers"] = trackers
        if tid not in pending:
            pending[tid] = Torrent(tid, md, trackers)
        t = pending[tid]
        return {"tid": tid, "state": "pending-confirm", "metadata": public_metadata(t.md)}
    uri = want_str(a, "magnet")
    parsed = urllib.parse.urlparse(uri)
    q = urllib.parse.parse_qs(parsed.query)
    xt = [v for v in q.get("xt", []) if v.startswith("urn:btih:")]
    if parsed.scheme != "magnet" or not xt or len(xt[0]) != 9 + 40:
        raise Fail("bad-request", "not a magnet link with a hex info hash")
    tid = xt[0][9:].lower()
    if tid in torrents:
        raise Fail("duplicate", "already added", tid=tid)
    if tid not in pending:
        t = Torrent(tid, None, q.get("tr", []), q.get("x.pe", []))
        t.state = "fetching-metadata"
        pending[tid] = t
        threading.Thread(target=magnet_worker, args=(t,), daemon=True).start()
    t = pending[tid]
    if t.md:
        return {"tid": tid, "state": "pending-confirm", "metadata": public_metadata(t.md)}
    return {"tid": tid, "state": "fetching-metadata"}


def confirm(a):
    tid = want_str(a, "tid")
    t = pending.get(tid)
    if t is None:
        raise Fail("not-found", "no such pending torrent")
    if t.md is None:
        raise Fail("bad-request", "metadata has not arrived")
    save = want_str(a, "savePath")
    if not os.path.isabs(save):
        raise Fail("bad-request", "savePath must be absolute")
    files = a.get("files")
    if not isinstance(files, list) or not isinstance(a.get("start"), bool):
        raise Fail("bad-request", "files and start are required")
    try:
        os.makedirs(save, exist_ok=True)
        shutil.disk_usage(save)
    except OSError as e:
        raise Fail("io-error", str(e))
    del pending[tid]
    t.save, t.sequential = save, bool(a.get("sequential"))
    t.prio = {f["index"]: f["priority"] for f in files}
    t.added = int(time.time() * 1000)
    torrents[tid] = t
    t.state = "checking"
    t.recheck()
    t.state = "paused"
    t.save_resume()
    if a["start"]:
        launch(t)
        t.save_resume()
    return {"status": t.status()}


def launch(t):
    if t.wanted() <= t.have:
        t.state = "seeding"
        t.error = None
        emit({"event": "completed", "tid": t.tid})
        t.save_resume()
        update_listener()
    else:
        t.start()


def remove(a):
    t = get_torrent(a, torrents)
    t.halt()
    del torrents[t.tid]
    try:
        os.remove(os.path.join(resume_dir, t.tid + ".json"))
    except OSError:
        pass
    if a.get("deleteFiles"):
        for f in t.md["files"]:
            try:
                os.remove(t.file_path(f))
            except OSError:
                pass
        if not t.md["single"]:
            shutil.rmtree(os.path.join(t.save, t.md["name"]), ignore_errors=True)
    update_listener()
    return {}


def restore(a):
    torrents.clear()
    if os.path.isdir(resume_dir):
        for name in sorted(os.listdir(resume_dir)):
            if not name.endswith(".json"):
                continue
            with open(os.path.join(resume_dir, name)) as f:
                d = json.load(f)
            t = Torrent(name[:-5], d["md"], d["trackers"], d["peers"])
            t.save, t.sequential, t.added = d["save"], d["sequential"], d["added"]
            t.prio = {int(k): v for k, v in d["prio"].items()}
            t.recheck()
            t.state = "paused"
            torrents[t.tid] = t
            if a.get("start") and d["state"] != "paused":
                launch(t)
    return {"torrents": [t.status() for t in torrents.values()]}


def handle(req):
    op, a = req["op"], req.get("args") or {}
    with lock:
        if op == "add":
            return add(a)
        if op == "confirm":
            return confirm(a)
        if op == "cancel":
            get_torrent(a, pending)
            del pending[a["tid"]]
            return {}
        if op in ("pause", "resume"):
            t = get_torrent(a, torrents)
            if op == "pause":
                t.halt()
                t.state = "paused"
                t.save_resume()
                update_listener()
            elif t.state in ("paused", "error"):
                launch(t)
            return {"status": t.status()}
        if op in ("pauseAll", "resumeAll"):
            for t in torrents.values():
                if op == "pauseAll":
                    t.halt()
                    t.state = "paused"
                elif t.state == "paused":
                    launch(t)
            update_listener()
            return {}
        if op == "remove":
            if not isinstance(a.get("deleteFiles"), bool):
                raise Fail("bad-request", "deleteFiles is required")
            return remove(a)
        if op == "list":
            return {"torrents": [t.status() for t in torrents.values()]}
        if op == "detail":
            t = torrents.get(a.get("tid")) or pending.get(a.get("tid"))
            if t is None or t.md is None:
                raise Fail("not-found", "no such torrent")
            pieces = len(t.pieces) // 20
            bits = bytearray((pieces + 7) // 8)
            for p in t.have:
                bits[p // 8] |= 0x80 >> (p % 8)
            return {
                "status": t.status(),
                "metadata": public_metadata(t.md),
                "trackers": [],
                "peers": [],
                "pieces": {"count": pieces, "have": base64.b64encode(bytes(bits)).decode()},
                "files": [
                    {"index": f["index"], "priority": t.prio.get(f["index"], 2), "progress": 0}
                    for f in t.md["files"]
                ],
            }
        if op == "setLimits":
            for k in ("downKBps", "upKBps"):
                if k in a and not isinstance(a[k], (int, float)):
                    raise Fail("bad-request", "%s must be a number" % k)
            return {}
        if op == "setConfig":
            return {"applied": list(a), "unsupported": []}
        if op == "restore":
            return restore(a)
        if op == "shutdown":
            return {}
        raise Fail("bad-request", "unknown op")


def idle_watch():
    while True:
        time.sleep(0.2)
        with lock:
            since = state["idle_since"]
            if since is not None and time.monotonic() - since >= IDLE:
                os._exit(0)


def progress_loop():
    while True:
        time.sleep(1.0)
        with lock:
            active = [t for t in torrents.values() if t.state in ("downloading", "seeding", "checking")]
            if active:
                emit({"event": "progress", "torrents": [t.status() for t in active]})


def main():
    os.makedirs(resume_dir, exist_ok=True)
    emit({"event": "ready", "protocol": 1, "engine": "ref-python 1.0", "pid": os.getpid()})
    threading.Thread(target=progress_loop, daemon=True).start()
    threading.Thread(target=idle_watch, daemon=True).start()
    for raw in sys.stdin.buffer:
        try:
            req = json.loads(raw)
            rid = req["id"]
        except (ValueError, KeyError, TypeError):
            continue
        try:
            result = handle(req)
            emit({"id": rid, "ok": True, "result": result})
        except Fail as e:
            error = {"code": e.code, "message": e.message}
            error.update(e.extra)
            emit({"id": rid, "ok": False, "error": error})
        except Exception as e:
            emit({"id": rid, "ok": False, "error": {"code": "engine-error", "message": str(e)}})
        if req.get("op") == "shutdown":
            os._exit(0)
    os._exit(0)


main()
