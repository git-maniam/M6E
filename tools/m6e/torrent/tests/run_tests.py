#!/usr/bin/env python3
"""Conformance tests for an m6e-torrent helper (tools/m6e/torrent/PROTOCOL.md).

    python3 -I tools/m6e/torrent/tests/run_tests.py [--helper CMD] [--slow]

Only loopback fixtures from tools/m6e/torrent/fixtures are used (spec 8.9, D38).
No real torrent, magnet link or tracker appears here. Output is TAP; the exit
status is 1 when any test fails.
"""

import argparse
import base64
import hashlib
import json
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
TORRENT_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(TORRENT_DIR)))
sys.path.insert(0, os.path.join(TORRENT_DIR, "fixtures"))

import bencode  # noqa: E402
import make_torrent  # noqa: E402
import swarm  # noqa: E402

ERROR_CODES = {
    "bad-request",
    "not-found",
    "invalid-torrent",
    "too-large",
    "unsafe-path",
    "duplicate",
    "disk-full",
    "io-error",
    "engine-error",
}
STATES = {
    "pending-confirm",
    "fetching-metadata",
    "checking",
    "queued",
    "downloading",
    "seeding",
    "paused",
    "completed",
    "error",
}
STATUS_KEYS = {
    "tid": str,
    "name": str,
    "state": str,
    "progress": (int, float),
    "size": int,
    "downloaded": int,
    "uploaded": int,
    "downKBps": (int, float),
    "upKBps": (int, float),
    "ratio": (int, float),
    "seeds": int,
    "peers": int,
    "isPrivate": bool,
    "savePath": str,
    "sequential": bool,
    "addedAt": (int, float),
}
DONE_STATES = ("seeding", "completed")
MAX_LINE = 1 << 20

OPTIONS = None
SEEN_CODES = {}
TARGET = {"fake": False, "engine": "", "cmd": []}


class Skip(Exception):
    pass


class Timeout(AssertionError):
    pass


class Helper:
    def __init__(self, cmd, profile, env=None, stderr_path=None):
        self.profile = profile
        self.events = []
        self.replies = {}
        self.reply_counts = {}
        self.violations = []
        self.first = None
        self.next_id = 1
        self.cond = threading.Condition()
        self.eof = False
        os.makedirs(profile, exist_ok=True)
        full_env = dict(os.environ)
        if env:
            full_env.update(env)
        self.stderr_file = open(stderr_path or os.path.join(profile, "..", "stderr.log"), "ab")
        self.proc = subprocess.Popen(
            list(cmd) + ["--protocol", "1", "--profile-dir", profile],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr_file,
            env=full_env,
        )
        self.started = time.monotonic()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for raw in iter(self.proc.stdout.readline, b""):
            now = time.monotonic()
            with self.cond:
                if len(raw) > MAX_LINE:
                    self.violations.append("stdout line longer than 1 MiB")
                try:
                    obj = json.loads(raw.decode("utf-8"))
                    if not isinstance(obj, dict):
                        raise ValueError("not an object")
                except ValueError:
                    self.violations.append("stdout is not a JSON line: %r" % raw[:80])
                    self.cond.notify_all()
                    continue
                if self.first is None:
                    self.first = obj
                if "event" in obj:
                    self.events.append((now, obj))
                elif "id" in obj:
                    self.reply_counts[obj["id"]] = self.reply_counts.get(obj["id"], 0) + 1
                    self.replies.setdefault(obj["id"], obj)
                else:
                    self.violations.append("neither event nor reply: %r" % obj)
                self.cond.notify_all()
        with self.cond:
            self.eof = True
            self.cond.notify_all()

    @property
    def pid(self):
        for _, e in self.events:
            if e.get("event") == "ready":
                return e.get("pid")
        return self.proc.pid

    def alive(self):
        return self.proc.poll() is None

    def mark(self):
        with self.cond:
            return len(self.events)

    def events_of(self, name, since=0):
        with self.cond:
            return [e for _, e in self.events[since:] if e.get("event") == name]

    def timed_events(self, name, since=0):
        with self.cond:
            return [(t, e) for t, e in self.events[since:] if e.get("event") == name]

    def wait_event(self, name, pred=None, timeout=10, since=0):
        deadline = time.monotonic() + timeout
        with self.cond:
            seen = since
            while True:
                while seen < len(self.events):
                    _, e = self.events[seen]
                    seen += 1
                    if e.get("event") == name and (pred is None or pred(e)):
                        return e
                if self.eof and seen >= len(self.events):
                    raise AssertionError("helper ended before event %r" % name)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise Timeout("no %r event within %ss" % (name, timeout))
                self.cond.wait(min(remaining, 0.2))

    def wait_ready(self, timeout=15):
        self.wait_event("ready", timeout=timeout)
        with self.cond:
            first = self.first
        if not first or first.get("event") != "ready":
            raise AssertionError("first line is not the ready event: %r" % (first,))
        return first

    def send_raw(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def send(self, op, args=None):
        rid = self.next_id
        self.next_id += 1
        self.send_raw((json.dumps({"id": rid, "op": op, "args": args or {}}) + "\n").encode())
        return rid

    def wait_reply(self, rid, timeout=10):
        deadline = time.monotonic() + timeout
        with self.cond:
            while rid not in self.replies:
                if self.eof:
                    raise AssertionError("helper ended before replying to request %d" % rid)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise Timeout("no reply to request %d within %ss" % (rid, timeout))
                self.cond.wait(min(remaining, 0.2))
            return self.replies[rid]

    def req(self, op, args=None, timeout=10):
        rid = self.send(op, args)
        reply = self.wait_reply(rid, timeout)
        if reply.get("id") != rid:
            raise AssertionError("reply id mismatch")
        if reply.get("ok") is False:
            code = (reply.get("error") or {}).get("code")
            SEEN_CODES.setdefault(code, op)
            if code == "unsupported":
                raise Skip("%s is unsupported by the target" % op)
        return reply

    def call(self, op, args=None, timeout=10):
        reply = self.req(op, args, timeout)
        if reply.get("ok") is not True:
            raise AssertionError("%s failed: %r" % (op, reply.get("error")))
        result = reply.get("result")
        if not isinstance(result, dict):
            raise AssertionError("%s result is not an object: %r" % (op, reply))
        return result

    def err(self, op, args, codes, timeout=10):
        reply = self.req(op, args, timeout)
        if reply.get("ok") is not False:
            raise AssertionError("%s should fail with %s, got ok" % (op, sorted(codes)))
        error = reply.get("error") or {}
        if error.get("code") not in codes:
            raise AssertionError(
                "%s failed with %r, expected one of %s" % (op, error, sorted(codes))
            )
        if not isinstance(error.get("message"), str):
            raise AssertionError("error has no message text: %r" % (error,))
        return error

    def close_stdin(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass

    def wait_exit(self, timeout):
        try:
            return self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            return None

    def kill(self):
        if self.alive():
            self.proc.send_signal(signal.SIGKILL)
        self.proc.wait()

    def stop(self):
        if self.alive():
            self.close_stdin()
            if self.wait_exit(2) is None:
                self.kill()
        try:
            self.stderr_file.close()
        except OSError:
            pass


class Ctx:
    def __init__(self, root, name):
        self.dir = os.path.join(root, name)
        self.profile = os.path.join(self.dir, "profile")
        self.save = os.path.join(self.dir, "save")
        self.files = os.path.join(self.dir, "in")
        self.outside = os.path.join(self.dir, "outside")
        for d in (self.save, self.files, self.outside):
            os.makedirs(d)
        self.helpers = []
        self.closers = []
        self.gaps = []
        self.notes = []
        self.fake = TARGET["fake"]

    def start(self, env=None, profile=None, ready=True):
        h = Helper(
            TARGET["cmd"],
            profile or self.profile,
            env,
            os.path.join(self.dir, "stderr.log"),
        )
        self.helpers.append(h)
        if ready:
            h.wait_ready()
            if not self.fake:
                try:
                    h.req("setConfig", {"bindInterface": "lo0"})
                except (Skip, AssertionError):
                    pass
        return h

    def gap(self, condition, message):
        if condition:
            return
        if self.fake:
            self.gaps.append(message)
            return
        raise AssertionError(message)

    def note(self, message):
        self.notes.append(message)

    def swarm(self, maker, **seed_args):
        tracker = swarm.Tracker()
        fixture = maker(tracker.url)
        seed = swarm.Seed([fixture], **seed_args)
        tracker.peers.append(("127.0.0.1", seed.port))
        self.closers += [tracker.close, seed.close]
        return fixture, tracker, seed

    def torrent_file(self, fixture, name=None):
        path = os.path.join(self.files, name or fixture.name + ".torrent")
        fixture.write(path)
        return path

    def raw_file(self, name, data):
        path = os.path.join(self.files, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def finish(self):
        for h in self.helpers:
            h.stop()
        for c in self.closers:
            try:
                c()
            except Exception:
                pass
        for h in self.helpers:
            if h.violations:
                raise AssertionError("protocol violation on stdout: %s" % h.violations[0])


def fixed_multi(announce):
    return make_torrent.multi_file(announce=announce)


def big_single(announce, size=600000, name="m6e-slow.bin"):
    return make_torrent.single_file(announce=announce, name=name, size=size)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def content_path(save, fixture, rel):
    if rel is None:
        return os.path.join(save, fixture.name)
    return os.path.join(save, fixture.name, *rel)


def verify_output(fixture, save, skip=()):
    for i, (rel, data) in enumerate(fixture.files):
        path = content_path(save, fixture, rel)
        if i in skip:
            if os.path.exists(path):
                with open(path, "rb") as f:
                    if f.read() == data:
                        raise AssertionError("skipped file %s was fully downloaded" % path)
            continue
        if not os.path.isfile(path) or os.path.islink(path):
            raise AssertionError("missing output file %s" % path)
        with open(path, "rb") as f:
            got = f.read()
        if sha(got) != sha(data):
            raise AssertionError("sha mismatch for %s (%d bytes)" % (path, len(got)))
    extra = [e for e in os.listdir(save) if e != fixture.name and not e.startswith(".")]
    if extra:
        raise AssertionError("unexpected entries in the save folder: %r" % extra)


def tree(path):
    found = set()
    for base, dirs, files in os.walk(path):
        for n in dirs + files:
            found.add(os.path.relpath(os.path.join(base, n), path))
    return found


def check_status(ctx, st, tid=None):
    assert isinstance(st, dict), "status is not an object"
    for key, kind in STATUS_KEYS.items():
        assert key in st, "status lacks %s" % key
        assert isinstance(st[key], kind) and not (
            kind is int and isinstance(st[key], bool)
        ), "status.%s has type %s" % (key, type(st[key]).__name__)
    for key in ("etaSeconds", "error"):
        assert key in st, "status lacks %s" % key
    assert st["state"] in STATES, "unknown state %r" % st["state"]
    assert 0 <= st["progress"] <= 1, "progress out of range"
    assert st["etaSeconds"] is None or isinstance(st["etaSeconds"], int)
    if st["error"] is not None:
        assert "code" in st["error"] and "message" in st["error"]
    if tid:
        assert st["tid"] == tid, "status for another torrent"


def check_metadata(ctx, md, fixture):
    for key, kind in (
        ("infoHash", str),
        ("name", str),
        ("totalSize", int),
        ("pieceLength", int),
        ("files", list),
        ("trackers", list),
        ("webSeeds", list),
        ("isPrivate", bool),
    ):
        assert key in md and isinstance(md[key], kind), "metadata.%s is wrong" % key
    ctx.gap(md["infoHash"] == fixture.tid, "infoHash is not the info hash of the torrent")
    ctx.gap(md["name"] == fixture.name, "metadata name differs from the torrent name")
    ctx.gap(md["totalSize"] == len(fixture.payload), "totalSize differs")
    ctx.gap(md["pieceLength"] == fixture.piece_length, "pieceLength differs")
    ctx.gap(md["isPrivate"] == fixture.private, "isPrivate differs")
    ctx.gap(
        [f["length"] for f in md["files"]] == [len(d) for _, d in fixture.files],
        "file lengths differ",
    )
    expected = ["/".join(p) if p else fixture.name for p, _ in fixture.files]
    ctx.gap([f["path"] for f in md["files"]] == expected, "file paths differ")
    for f in md["files"]:
        assert isinstance(f["index"], int) and isinstance(f["length"], int)
        path = f["path"]
        assert not path.startswith("/") and ".." not in path.split("/")
    if fixture.announce:
        ctx.gap(fixture.announce in md["trackers"], "tracker missing from metadata")


def confirm_args(md, save, start=True, priorities=None, sequential=False):
    return {
        "files": [
            {"index": f["index"], "priority": (priorities or {}).get(f["index"], 2)}
            for f in md["files"]
        ],
        "savePath": save,
        "sequential": sequential,
        "start": start,
    }


def add_file(h, path):
    result = h.call("add", {"file": path})
    assert isinstance(result.get("tid"), str) and len(result["tid"]) == 40, "bad tid"
    return result


def wait_done(h, tid, since, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            h.wait_event("completed", lambda e: e.get("tid") == tid, 1.0, since)
            return
        except Timeout:
            pass
        for st in h.call("list")["torrents"]:
            if st["tid"] == tid:
                if st["state"] in DONE_STATES:
                    return
                if st["state"] == "error":
                    raise AssertionError("torrent in error state: %r" % (st["error"],))
    raise Timeout("torrent not complete within %ss" % timeout)


def state_of(h, tid):
    for st in h.call("list")["torrents"]:
        if st["tid"] == tid:
            return st
    return None


def wait_until(predicate, timeout, interval=0.1):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return predicate()


def sockets(pid):
    try:
        r = subprocess.run(
            ["lsof", "-nP", "-iTCP", "-iUDP", "-a", "-p", str(pid)],
            capture_output=True,
            text=True,
            timeout=15,
        )
    except FileNotFoundError:
        raise Skip("lsof is not available")
    return [line for line in r.stdout.splitlines()[1:] if line.strip()]


REGISTRY = []


def test(name=None, slow=False, optional=False):
    def wrap(fn):
        REGISTRY.append(
            {"name": name or fn.__name__, "fn": fn, "slow": slow, "optional": optional}
        )
        return fn

    return wrap


@test()
def ready_first(ctx):
    h = ctx.start(ready=False)
    first = h.wait_ready()
    assert first["protocol"] == 1, "protocol is not 1"
    assert isinstance(first["engine"], str) and first["engine"], "engine is empty"
    assert isinstance(first["pid"], int), "pid is not an integer"
    ctx.note("engine: %s" % first["engine"])
    time.sleep(0.5)
    with h.cond:
        others = [e for _, e in h.events if e.get("event") != "ready" and "event" in e]
    assert not others, "unrequested events at start: %r" % others[:2]
    assert os.path.isdir(ctx.profile), "profile directory not created"


@test()
def one_reply_per_request(ctx):
    h = ctx.start()
    ids = []
    for op, args in (
        ("list", {}),
        ("setLimits", {}),
        ("pauseAll", {}),
        ("resumeAll", {}),
        ("setConfig", {}),
        ("no-such-op", {}),
        ("detail", {"tid": "0" * 40}),
        ("list", {}),
    ):
        ids.append(h.send(op, args))
    for rid in ids:
        reply = h.wait_reply(rid)
        assert reply["id"] == rid
        if reply["ok"]:
            assert isinstance(reply.get("result"), dict), "ok reply without result"
        else:
            assert reply["error"]["code"] in ERROR_CODES, reply["error"]
            assert isinstance(reply["error"]["message"], str)
    time.sleep(0.5)
    for rid in ids:
        assert h.reply_counts[rid] == 1, "request %d got %d replies" % (rid, h.reply_counts[rid])


@test()
def bad_requests(ctx):
    cases = [
        ("no-such-op", {}),
        ("add", {}),
        ("add", {"file": 5}),
        ("add", {"file": "relative/path.torrent"}),
        ("add", {"magnet": "http://not-a-magnet.invalid/"}),
        ("confirm", {}),
        ("pause", {}),
        ("setLimits", {"downKBps": "fast"}),
    ]
    for n, (op, args) in enumerate(cases):
        h = ctx.start(profile=os.path.join(ctx.dir, "profile-bad%d" % n))
        try:
            reply = h.req(op, args, timeout=4)
        except AssertionError as e:
            ctx.gap(False, "%s %r: %s" % (op, args, e))
            continue
        ok = reply.get("ok") is False and reply["error"]["code"] in (
            ("bad-request", "invalid-torrent", "not-found")
            if op == "add" and "file" in args
            else ("bad-request",)
        )
        ctx.gap(ok, "%s %r answered %r, not bad-request" % (op, args, reply))
        h.stop()


@test()
def not_found(ctx):
    h = ctx.start()
    ghost = "ab" * 20
    for op, args in (
        ("pause", {"tid": ghost}),
        ("resume", {"tid": ghost}),
        ("remove", {"tid": ghost, "deleteFiles": False}),
        ("detail", {"tid": ghost}),
        ("cancel", {"tid": ghost}),
        ("confirm", dict(confirm_args({"files": []}, ctx.save), tid=ghost)),
    ):
        h.err(op, args, {"not-found"})
    assert h.call("list")["torrents"] == []


@test()
def operations_shape(ctx):
    h = ctx.start()
    assert h.call("restore", {"start": False}) == {"torrents": []}
    assert h.call("list") == {"torrents": []}
    for op in ("pauseAll", "resumeAll"):
        assert h.call(op) == {}
    assert h.call("setLimits", {"downKBps": 0, "upKBps": 0}) == {}
    assert h.call(
        "setLimits",
        {"turtle": {"enabled": False, "downKBps": 50, "upKBps": 10}, "maxActiveDownloads": 3},
    ) == {}
    cfg = h.call("setConfig", {"dht": False, "pex": False, "lpd": False, "portMapping": False})
    assert isinstance(cfg.get("applied"), list) and isinstance(cfg.get("unsupported"), list)
    assert sorted(cfg["applied"] + cfg["unsupported"]) == ["dht", "lpd", "pex", "portMapping"]
    fx = make_torrent.multi_file()
    path = ctx.torrent_file(fx)
    added = add_file(h, path)
    assert added["state"] == "pending-confirm", added["state"]
    check_metadata(ctx, added["metadata"], fx)
    tid = added["tid"]
    ctx.gap(tid == fx.tid, "tid is not the hex info hash")
    assert h.call("list") == {"torrents": []}, "pending torrent listed"
    result = h.call("confirm", dict(confirm_args(added["metadata"], ctx.save, start=False), tid=tid))
    check_status(ctx, result["status"], tid)
    assert result["status"]["state"] == "paused", result["status"]["state"]
    assert os.path.realpath(result["status"]["savePath"]) == os.path.realpath(ctx.save)
    listed = h.call("list")["torrents"]
    assert [s["tid"] for s in listed] == [tid]
    check_status(ctx, listed[0], tid)
    d = h.call("detail", {"tid": tid})
    check_status(ctx, d["status"], tid)
    check_metadata(ctx, d["metadata"], fx)
    assert isinstance(d["trackers"], list) and isinstance(d["peers"], list)
    assert isinstance(d["pieces"]["count"], int)
    base64.b64decode(d["pieces"]["have"], validate=True)
    assert [f["index"] for f in d["files"]] == [f["index"] for f in added["metadata"]["files"]]
    for f in d["files"]:
        assert 0 <= f["priority"] <= 3 and 0 <= f["progress"] <= 1
    assert not os.listdir(ctx.save), "paused confirm wrote payload data"


@test()
def error_codes_unsafe_and_malformed(ctx):
    h = ctx.start()
    before = tree(ctx.dir)
    kinds = [
        "traversal",
        "traversal-mid",
        "absolute",
        "empty-component",
        "dot",
        "backslash",
        "colon",
        "nul",
        "name-dotdot",
        "name-slash",
        "single-name-traversal",
    ]
    for kind in kinds:
        path = ctx.raw_file(kind + ".torrent", make_torrent.hostile(kind))
        try:
            h.err("add", {"file": path}, {"unsafe-path"})
        except AssertionError as e:
            ctx.gap(False, "hostile %s: %s" % (kind, e))
    base = make_torrent.multi_file(name="m6e-hostile-symlink")
    info = dict(base.info)
    info["files"] = [
        {"length": 10, "path": ["ok.txt"]},
        {"length": 0, "path": ["link"], "attr": b"l", "symlink path": ["..", "outside"]},
    ]
    info["pieces"] = hashlib.sha1(b"x" * 10).digest()
    path = ctx.raw_file("symlink.torrent", bencode.encode({"info": info}))
    try:
        h.err("add", {"file": path}, {"unsafe-path"})
    except AssertionError as e:
        ctx.gap(False, "symlink entry: %s" % e)
    for kind in ("truncated", "garbage", "empty", "no-info", "bad-pieces"):
        path = ctx.raw_file("malformed-" + kind + ".torrent", make_torrent.malformed(kind))
        try:
            h.err("add", {"file": path}, {"invalid-torrent"})
        except AssertionError as e:
            ctx.gap(False, "malformed %s: %s" % (kind, e))
    assert h.call("list") == {"torrents": []}
    assert h.alive(), "helper died on hostile input"
    after = tree(ctx.dir)
    new = [
        p
        for p in after - before
        if not p.startswith("profile") and not p.startswith("stderr") and not p.endswith(".torrent")
    ]
    assert not new, "files appeared outside the profile: %r" % new
    assert not tree(ctx.save), "hostile torrents wrote into the save folder"
    for name in ("escape.txt", "escape.bin"):
        assert not os.path.exists(os.path.join(ctx.dir, name)), name
        assert not os.path.exists(os.path.join(os.path.dirname(ctx.dir), name)), name


@test()
def error_codes_size_and_depth(ctx):
    h = ctx.start()
    big = os.path.join(ctx.files, "oversized.torrent")
    make_torrent.oversized(big)
    assert os.path.getsize(big) > 16 * 1024 * 1024
    try:
        h.err("add", {"file": big}, {"too-large", "invalid-torrent"}, timeout=30)
    except AssertionError as e:
        ctx.gap(False, "oversized torrent: %s" % e)
    for depth in (100, 5000):
        path = ctx.raw_file("deep%d.torrent" % depth, make_torrent.too_deep(depth))
        try:
            h.err("add", {"file": path}, {"too-large", "invalid-torrent"}, timeout=30)
        except AssertionError as e:
            ctx.gap(False, "bencoding depth %d: %s" % (depth, e))
    assert h.alive() and h.call("list") == {"torrents": []}


@test()
def error_codes_missing_file(ctx):
    h = ctx.start()
    h.err(
        "add",
        {"file": os.path.join(ctx.files, "does-not-exist.torrent")},
        {"not-found", "io-error", "invalid-torrent", "bad-request"},
    )


@test()
def duplicates_and_pending(ctx):
    h = ctx.start()
    fx = make_torrent.multi_file()
    path = ctx.torrent_file(fx)
    first = add_file(h, path)
    again = h.req("add", {"file": path})
    idem = (
        again.get("ok") is True
        and again["result"]["tid"] == first["tid"]
        and again["result"]["state"] == "pending-confirm"
        and again["result"].get("metadata") == first["metadata"]
    )
    ctx.gap(idem, "a repeated add of a pending torrent is not idempotent")
    if not idem:
        h.err("add", {"file": path}, {"duplicate"})
    tid = first["tid"]
    h.call("confirm", dict(confirm_args(first["metadata"], ctx.save, start=False), tid=tid))
    error = h.err("add", {"file": path}, {"duplicate"})
    assert error.get("tid") == tid, "duplicate error lacks the tid of the existing torrent"
    reply = h.req("add", {"magnet": fx.magnet()})
    ctx.gap(
        reply.get("ok") is False
        and reply["error"]["code"] == "duplicate"
        and reply["error"].get("tid") == tid,
        "the same torrent as a magnet link was not refused as duplicate",
    )
    h.err("cancel", {"tid": tid}, {"not-found"})
    h.call("remove", {"tid": tid, "deleteFiles": False})
    fresh = add_file(h, path)
    assert fresh["tid"] == tid
    h.call("cancel", {"tid": tid})
    h.err("confirm", dict(confirm_args(first["metadata"], ctx.save), tid=tid), {"not-found"})
    assert not tree(ctx.save)


@test()
def confirm_with_unusable_save_path(ctx):
    h = ctx.start()
    fx = make_torrent.multi_file()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    blocker = ctx.raw_file("blocker", b"i am a file")
    cases = [
        (os.path.join(blocker, "sub"), {"io-error", "unsafe-path", "bad-request"}),
        ("relative/save", {"bad-request", "unsafe-path"}),
    ]
    for save, codes in cases:
        args = dict(confirm_args(added["metadata"], save, start=True), tid=tid)
        reply = h.req("confirm", args, timeout=15)
        failed = reply.get("ok") is False and reply["error"]["code"] in codes
        if reply.get("ok") is True:
            h.call("remove", {"tid": tid, "deleteFiles": False})
            added = add_file(h, os.path.join(ctx.files, fx.name + ".torrent"))
        ctx.gap(failed, "confirm into %r answered %r" % (save, reply))
    assert not os.path.exists(os.path.join("relative", "save"))


@test()
def download_file_and_verify(ctx):
    fx, tracker, seed = ctx.swarm(fixed_multi)
    h = ctx.start()
    since = h.mark()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    time.sleep(2.5)
    assert seed.request_count() == 0 and seed.bytes_served == 0, "payload requested before confirm"
    assert not tree(ctx.save), "files written before confirm"
    result = h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    assert result["status"]["state"] in (
        "checking",
        "queued",
        "downloading",
        "seeding",
        "completed",
    ), result["status"]["state"]
    if ctx.fake:
        ctx.note("the fake helper moves no data: the transfer part is not run")
        return
    wait_done(h, tid, since)
    verify_output(fx, ctx.save)
    st = state_of(h, tid)
    assert st["progress"] == 1 and st["state"] in DONE_STATES and st["error"] is None
    assert seed.bytes_served >= len(fx.payload) - 1


@test()
def selective_priority_zero(ctx):
    if ctx.fake:
        raise Skip("the fake helper moves no data")
    fx, tracker, seed = ctx.swarm(fixed_multi)
    h = ctx.start()
    since = h.mark()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    args = confirm_args(added["metadata"], ctx.save, priorities={0: 0})
    h.call("confirm", dict(args, tid=tid))
    wait_done(h, tid, since)
    verify_output(fx, ctx.save, skip=(0,))


@test()
def magnet_metadata_only_then_download(ctx):
    fx, tracker, seed = ctx.swarm(fixed_multi)
    h = ctx.start()
    since = h.mark()
    magnet = fx.magnet(trackers=[tracker.url], peers=[seed.address])
    result = h.call("add", {"magnet": magnet})
    tid = result["tid"]
    ctx.gap(tid == fx.tid, "tid of a magnet link is not its info hash")
    assert result["state"] == "fetching-metadata", result["state"]
    assert h.call("list") == {"torrents": []}, "a fetching torrent is listed"
    event = h.wait_event("metadata", lambda e: e.get("tid") == tid, 20, since)
    if not ctx.fake:
        check_metadata(ctx, event["metadata"], fx)
    time.sleep(2.5)
    assert seed.request_count() == 0 and seed.bytes_served == 0, "payload before confirm"
    assert not tree(ctx.save), "save folder not empty before confirm"
    if ctx.fake:
        h.call("cancel", {"tid": tid})
        ctx.note("the fake helper moves no data: the download part is not run")
        return
    h.call("confirm", dict(confirm_args(event["metadata"], ctx.save), tid=tid))
    wait_done(h, tid, since)
    verify_output(fx, ctx.save)


@test()
def magnet_cancel_and_hostile_name(ctx):
    fx, tracker, seed = ctx.swarm(fixed_multi)
    h = ctx.start()
    since = h.mark()
    magnet = fx.magnet(trackers=[tracker.url], peers=[seed.address]).replace(
        "dn=" + fx.name, "dn=..%2F..%2Fescape"
    )
    tid = h.call("add", {"magnet": magnet})["tid"]
    h.call("cancel", {"tid": tid})
    h.err("confirm", dict(confirm_args({"files": []}, ctx.save), tid=tid), {"not-found"})
    time.sleep(1.0)
    again = h.call("add", {"magnet": magnet})
    event = h.wait_event("metadata", lambda e: e.get("tid") == again["tid"], 20, since)
    assert "/" not in event["metadata"]["name"]
    h.call("cancel", {"tid": again["tid"]})
    assert seed.request_count() == 0
    assert not tree(ctx.save) and not tree(ctx.outside)
    assert not os.path.exists(os.path.join(os.path.dirname(ctx.dir), "escape"))


@test()
def progress_batched(ctx):
    fx, tracker, seed = ctx.swarm(big_single, block_delay=0.3)
    h = ctx.start()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    since = h.mark()
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    if ctx.fake:
        time.sleep(2.5)
        ctx.note("the fake helper emits no progress by itself: only the idle part is run")
    else:
        time.sleep(6.5)
    events = h.timed_events("progress", since)
    if not ctx.fake:
        assert len(events) >= 3, "only %d progress events in 6.5 s" % len(events)
        gaps = [b[0] - a[0] for a, b in zip(events, events[1:])]
        assert min(gaps) >= 0.8, "progress events %.2fs apart" % min(gaps)
        assert max(gaps) <= 2.5, "progress gap of %.2fs" % max(gaps)
    for _, e in events:
        assert isinstance(e["torrents"], list)
        for st in e["torrents"]:
            check_status(ctx, st)
            assert st["state"] not in ("pending-confirm", "fetching-metadata")
    h.call("pause", {"tid": tid})
    time.sleep(1.5)
    quiet = h.mark()
    time.sleep(2.5)
    loose = h.timed_events("progress", quiet)
    assert not loose, "progress events while nothing is active"
    h.call("remove", {"tid": tid, "deleteFiles": True})
    quiet = h.mark()
    time.sleep(2.5)
    assert not h.events_of("progress", quiet), "progress events with no torrent"


@test()
def pause_and_resume(ctx):
    fx, tracker, seed = ctx.swarm(big_single, block_delay=0.2)
    h = ctx.start()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    if not ctx.fake:
        assert wait_until(lambda: seed.request_count() >= 3, 20), "download did not start"
    st = h.call("pause", {"tid": tid})["status"]
    check_status(ctx, st, tid)
    assert st["state"] == "paused", st["state"]
    assert state_of(h, tid)["state"] == "paused"
    time.sleep(1.0)
    c1 = seed.request_count()
    time.sleep(2.0)
    assert seed.request_count() - c1 <= 1, "data requested while paused"
    st = h.call("resume", {"tid": tid})["status"]
    assert st["state"] in ("checking", "queued", "downloading", "seeding"), st["state"]
    if not ctx.fake:
        assert wait_until(lambda: seed.request_count() > c1 + 2, 15), "no transfer after resume"
    assert h.call("pauseAll") == {}
    assert state_of(h, tid)["state"] == "paused"
    assert h.call("resumeAll") == {}
    assert state_of(h, tid)["state"] != "paused"


@test()
def remove_with_and_without_delete(ctx):
    h = ctx.start()
    for delete in (False, True):
        save = os.path.join(ctx.save, "delete" if delete else "keep")
        os.makedirs(save)
        fx = make_torrent.multi_file(name="m6e-remove-%s" % delete)
        fx.write_content(save)
        added = add_file(h, ctx.torrent_file(fx))
        tid = added["tid"]
        h.call("confirm", dict(confirm_args(added["metadata"], save, start=True), tid=tid))
        h.call("remove", {"tid": tid, "deleteFiles": delete})
        assert h.call("list") == {"torrents": []}
        h.err("detail", {"tid": tid}, {"not-found"})
        h.err("remove", {"tid": tid, "deleteFiles": delete}, {"not-found"})
        paths = [content_path(save, fx, rel) for rel, _ in fx.files]
        if delete:
            ctx.gap(not any(os.path.exists(p) for p in paths), "deleteFiles left files behind")
        else:
            for p, (_, data) in zip(paths, fx.files):
                assert os.path.isfile(p), "remove without deleteFiles deleted %s" % p
                with open(p, "rb") as f:
                    assert f.read() == data, "remove without deleteFiles changed %s" % p
    h.call("shutdown")
    assert h.wait_exit(3) == 0
    h2 = ctx.start()
    assert h2.call("restore", {"start": False}) == {"torrents": []}, "removed torrents came back"


@test()
def shutdown_idle(ctx):
    h = ctx.start()
    t0 = time.monotonic()
    assert h.call("shutdown") == {}
    rc = h.wait_exit(2)
    assert rc == 0, "exit status %r (None means still running after 2 s)" % rc
    assert time.monotonic() - t0 < 2.0


@test()
def eof_idle(ctx):
    h = ctx.start()
    t0 = time.monotonic()
    h.close_stdin()
    rc = h.wait_exit(2)
    assert rc == 0, "exit status %r after stdin EOF (None means still running)" % rc
    assert time.monotonic() - t0 < 2.0


def active_exit(mode):
    def run(ctx):
        fx, tracker, seed = ctx.swarm(big_single, block_delay=0.3)
        h = ctx.start()
        added = add_file(h, ctx.torrent_file(fx))
        tid = added["tid"]
        h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
        if not ctx.fake:
            assert wait_until(lambda: seed.request_count() >= 2, 20), "download did not start"
        t0 = time.monotonic()
        if mode == "shutdown":
            assert h.call("shutdown") == {}
        else:
            h.close_stdin()
        rc = h.wait_exit(2)
        assert rc == 0, "exit status %r with an active download" % rc
        assert time.monotonic() - t0 < 2.0

    return run


REGISTRY.append(
    {"name": "shutdown_active", "fn": active_exit("shutdown"), "slow": False, "optional": False}
)
REGISTRY.append(
    {"name": "eof_active", "fn": active_exit("eof"), "slow": False, "optional": False}
)


@test()
def killed_helper_resumes(ctx):
    fx, tracker, seed = ctx.swarm(big_single, block_delay=0.15)
    h = ctx.start()
    since = h.mark()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    if not ctx.fake:
        assert wait_until(lambda: seed.request_count() >= 6, 20), "download did not start"
    h.kill()
    assert not h.alive()
    ctx.gap(
        any(tid in name for name in os.listdir(ctx.profile)),
        "no resume file named after the tid in the profile folder (--profile-dir) after confirm",
    )
    h2 = ctx.start()
    paused = h2.call("restore", {"start": False})["torrents"]
    assert [s["tid"] for s in paused] == [tid], "the killed torrent was not restored"
    assert all(s["state"] == "paused" for s in paused), "restore with start false must pause"
    check_status(ctx, paused[0], tid)
    h2.call("shutdown")
    assert h2.wait_exit(3) == 0
    h3 = ctx.start()
    since = h3.mark()
    listed = h3.call("restore", {"start": True})["torrents"]
    assert [s["tid"] for s in listed] == [tid]
    assert listed[0]["state"] != "paused", "restore with start true left it paused"
    if ctx.fake:
        ctx.note("the fake helper moves no data: the completion part is not run")
        return
    served = seed.bytes_served
    wait_done(h3, tid, since, timeout=90)
    verify_output(fx, ctx.save)
    ctx.note("bytes served in total: %d of %d" % (seed.bytes_served, len(fx.payload)))
    assert seed.bytes_served < served + len(fx.payload) + 1


@test()
def idle_has_no_sockets(ctx):
    h = ctx.start()
    pid = h.pid
    assert sockets(pid) == [], "sockets at idle: %r" % sockets(pid)
    h.call("restore", {"start": True})
    assert sockets(pid) == [], "sockets after restore with nothing saved"
    fx = make_torrent.multi_file()
    added = add_file(h, ctx.torrent_file(fx))
    time.sleep(0.5)
    assert sockets(pid) == [], "sockets with only a pending file torrent"
    h.call("cancel", {"tid": added["tid"]})
    h.call("setConfig", {"dht": True, "pex": True})
    time.sleep(0.5)
    assert sockets(pid) == [], "setConfig opened a socket"
    assert not h.events_of("listening"), "listening event with nothing active"


@test()
def sockets_follow_active_torrents(ctx):
    fx, tracker, seed = ctx.swarm(big_single, block_delay=0.3)
    h = ctx.start()
    pid = h.pid
    since = h.mark()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    if ctx.fake:
        ctx.note("the fake helper opens no sockets: only the closing part is run")
    else:
        event = h.wait_event("listening", timeout=10, since=since)
        assert isinstance(event["port"], int) and event["port"] > 0
        assert sockets(pid), "no socket while a torrent is downloading"
    h.call("remove", {"tid": tid, "deleteFiles": True})
    if not ctx.fake:
        h.wait_event("closed", timeout=10, since=since)
    assert wait_until(lambda: not sockets(pid), 8), "sockets after the last torrent: %r" % sockets(pid)


@test(slow=True)
def idle_exit(ctx):
    if ctx.fake:
        raise Skip("the fake helper has no idle timeout")
    seconds = OPTIONS.idle_seconds
    env = {"M6E_TORRENT_IDLE_SECONDS": str(seconds)} if seconds != 60 else None
    h = ctx.start(env=env)
    fx = make_torrent.multi_file()
    save = os.path.join(ctx.save, "idle")
    os.makedirs(save)
    fx.write_content(save)
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], save), tid=tid))
    h.call("remove", {"tid": tid, "deleteFiles": False})
    t0 = time.monotonic()
    rc = h.wait_exit(seconds + 15)
    elapsed = time.monotonic() - t0
    assert rc == 0, "helper still running %ss after the last torrent" % round(elapsed)
    assert elapsed >= seconds * 0.5, "helper left after only %.1fs" % elapsed
    ctx.note("exited %.1fs after the last torrent was removed" % elapsed)


@test()
def no_symlink_escape(ctx):
    for single in (False, True):
        fx, tracker, seed = ctx.swarm(
            (lambda a: big_single(a, 60000, "m6e-link.bin")) if single else fixed_multi
        )
        outside = os.path.join(ctx.outside, "single" if single else "multi")
        os.makedirs(outside)
        sentinel = os.path.join(outside, "sentinel")
        with open(sentinel, "wb") as f:
            f.write(b"keep")
        save = os.path.join(ctx.save, "s%d" % single)
        os.makedirs(save)
        target = sentinel if single else outside
        os.symlink(target, os.path.join(save, fx.name))
        h = ctx.start(profile=os.path.join(ctx.dir, "profile-link%d" % single))
        since = h.mark()
        added = add_file(h, ctx.torrent_file(fx))
        tid = added["tid"]
        reply = h.req("confirm", dict(confirm_args(added["metadata"], save), tid=tid), timeout=15)
        if reply.get("ok") and not ctx.fake:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                st = state_of(h, tid)
                if st is None or st["state"] in DONE_STATES + ("error",):
                    break
                time.sleep(0.5)
        h.stop()
        if single:
            with open(sentinel, "rb") as f:
                assert f.read() == b"keep", "a symlink was followed out of the save folder"
        else:
            assert os.listdir(outside) == ["sentinel"], "files written through a symlink"


@test(optional=True)
def disk_full(ctx):
    if ctx.fake:
        raise Skip("the fake helper moves no data")
    if sys.platform != "darwin" or not shutil.which("hdiutil"):
        raise Skip("needs hdiutil")
    image = os.path.join(ctx.dir, "small.dmg")
    mount = os.path.join(ctx.dir, "small")
    os.makedirs(mount)
    r = subprocess.run(
        ["hdiutil", "create", "-size", "6m", "-fs", "HFS+", "-volname", "M6EFull", image],
        capture_output=True,
    )
    if r.returncode:
        raise Skip("cannot create a disk image")
    r = subprocess.run(
        ["hdiutil", "attach", "-nobrowse", "-mountpoint", mount, image], capture_output=True
    )
    if r.returncode:
        raise Skip("cannot attach a disk image")
    ctx.closers.append(
        lambda: subprocess.run(["hdiutil", "detach", "-force", mount], capture_output=True)
    )
    fx, tracker, seed = ctx.swarm(lambda a: big_single(a, 24 * 1024 * 1024, "m6e-big.bin"))
    h = ctx.start()
    since = h.mark()
    added = add_file(h, ctx.torrent_file(fx))
    tid = added["tid"]
    reply = h.req("confirm", dict(confirm_args(added["metadata"], mount), tid=tid), timeout=30)
    if reply.get("ok") is False:
        assert reply["error"]["code"] == "disk-full", reply["error"]
        return

    def seen():
        for e in h.events_of("error", since):
            if e.get("code") == "disk-full":
                return True
        st = state_of(h, tid)
        return bool(st and st["error"] and st["error"]["code"] == "disk-full")

    assert wait_until(seen, 60), "no disk-full error on a full volume"
    st = state_of(h, tid)
    assert st is None or st["state"] in ("error", "paused"), st["state"]


@test(name="zz_documented_error_codes_only")
def documented_codes_only(ctx):
    unknown = [c for c in SEEN_CODES if c not in ERROR_CODES and c != "unsupported"]
    assert not unknown, "undocumented error codes: %r" % unknown


def run_one(entry, root):
    name = entry["name"]
    ctx = Ctx(root, name)
    status, reason, detail = "ok", "", ""
    try:
        entry["fn"](ctx)
    except Skip as e:
        status, reason = "skip", str(e)
    except Exception as e:
        status = "fail"
        reason = "%s: %s" % (type(e).__name__, e)
        detail = traceback.format_exc()
    try:
        ctx.finish()
    except Exception as e:
        if status != "fail":
            status, reason = "fail", "%s: %s" % (type(e).__name__, e)
    if status == "fail" and entry["optional"]:
        status = "todo"
    return status, reason, detail, ctx


def main():
    global OPTIONS
    default = os.path.join(TORRENT_DIR, "build", "m6e-torrent")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--helper", default=default, help="helper command (shell-split)")
    ap.add_argument("--slow", action="store_true", help="also run the slow tests")
    ap.add_argument("--only", action="append", default=[], help="run tests whose name contains this")
    ap.add_argument("--keep", action="store_true", help="keep the temporary folder")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument(
        "--idle-seconds",
        type=int,
        default=60,
        help="idle timeout for the slow idle_exit test; other values are passed as "
        "M6E_TORRENT_IDLE_SECONDS",
    )
    OPTIONS = ap.parse_args()
    cmd = shlex.split(OPTIONS.helper)
    if not os.path.exists(cmd[0]) and not shutil.which(cmd[0]):
        print("Bail out! helper not found: %s" % cmd[0])
        return 2
    TARGET["cmd"] = cmd
    root = os.path.realpath(tempfile.mkdtemp(prefix="m6e-torrent-test-"))
    print("TAP version 13")
    print("# helper: %s" % " ".join(cmd))
    try:
        probe = Helper(cmd, os.path.join(root, "probe", "profile"), None, os.path.join(root, "probe.log"))
        try:
            ready = probe.wait_ready()
            TARGET["engine"] = ready.get("engine", "")
            TARGET["fake"] = str(TARGET["engine"]).lower().startswith("fake")
        except AssertionError as e:
            print("# probe failed: %s" % e)
        probe.stop()
    except OSError as e:
        print("Bail out! cannot start the helper: %s" % e)
        return 2
    print("# engine: %s%s" % (TARGET["engine"], " (fake target)" if TARGET["fake"] else ""))
    selected = [
        t
        for t in REGISTRY
        if (OPTIONS.slow or not t["slow"])
        and (not OPTIONS.only or any(o in t["name"] for o in OPTIONS.only))
    ]
    print("1..%d" % len(selected))
    counts = {"ok": 0, "fail": 0, "skip": 0, "todo": 0}
    gaps = []
    for n, entry in enumerate(selected, 1):
        t0 = time.monotonic()
        status, reason, detail, ctx = run_one(entry, root)
        counts[status] += 1
        label = "%d - %s" % (n, entry["name"])
        if status == "ok":
            print("ok %s" % label)
        elif status == "skip":
            print("ok %s # SKIP %s" % (label, reason))
        elif status == "todo":
            print("not ok %s # TODO optional: %s" % (label, reason))
        else:
            print("not ok %s" % label)
            print("# %s" % reason)
            for line in detail.splitlines()[-6:]:
                print("# %s" % line)
        for note in ctx.notes:
            print("# %s: %s" % (entry["name"], note))
        for gap in ctx.gaps:
            print("# %s: fake gap: %s" % (entry["name"], gap))
            gaps.append("%s: %s" % (entry["name"], gap))
        print("# %s took %.1fs" % (entry["name"], time.monotonic() - t0))
        if OPTIONS.verbose:
            log = os.path.join(ctx.dir, "stderr.log")
            if os.path.exists(log):
                with open(log, errors="replace") as f:
                    for line in f.read().splitlines()[-20:]:
                        print("#   stderr: %s" % line)
    print("# error codes seen: %s" % sorted(map(str, SEEN_CODES)))
    print(
        "# pass %d fail %d skip %d todo %d, fake gaps %d"
        % (counts["ok"], counts["fail"], counts["skip"], counts["todo"], len(gaps))
    )
    if OPTIONS.keep:
        print("# kept %s" % root)
    else:
        shutil.rmtree(root, ignore_errors=True)
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
