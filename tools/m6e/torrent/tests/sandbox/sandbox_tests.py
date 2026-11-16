#!/usr/bin/env python3
"""Sandbox, setConfig and setFilePriority tests for the m6e-torrent helper
(spec 8.4, 8.5; PROTOCOL.md, "Additions (helper-sandbox, P8.1)").

    python3 -I tools/m6e/torrent/tests/sandbox/sandbox_tests.py \
        --helper <binary> --query <sandbox_query>

Only synthetic fixtures on 127.0.0.1 (spec 8.9, D38): a local tracker and
seed. No real torrent, magnet link, tracker or DHT node appears here. Output
is TAP; the exit status is 1 when any test fails.
"""

import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
TORRENT_DIR = os.path.dirname(TESTS)
sys.path.insert(0, TESTS)
sys.path.insert(0, os.path.join(TORRENT_DIR, "fixtures"))

import bencode  # noqa: E402
import make_torrent  # noqa: E402
import run_tests  # noqa: E402
import swarm  # noqa: E402

Helper = run_tests.Helper
Skip = run_tests.Skip
Timeout = run_tests.Timeout
confirm_args = run_tests.confirm_args
add_file = run_tests.add_file
check_status = run_tests.check_status
content_path = run_tests.content_path
state_of = run_tests.state_of
verify_output = run_tests.verify_output
wait_done = run_tests.wait_done
wait_until = run_tests.wait_until

OPTS = None
REGISTRY = []
POWER_NAME = "M6E BitTorrent transfer"


def test(fn):
    REGISTRY.append(fn)
    return fn


class Ctx:
    def __init__(self, root, name):
        self.dir = os.path.join(root, name)
        self.profile = os.path.join(self.dir, "profile")
        self.save = os.path.join(self.dir, "save")
        self.save2 = os.path.join(self.dir, "save2")
        self.inbox = os.path.join(self.dir, "in")
        self.outside = os.path.join(self.dir, "outside")
        for d in (self.save, self.save2, self.inbox, self.outside):
            os.makedirs(d)
        self.helpers = []
        self.closers = []
        self.notes = []

    def args(self, sandbox=True, downloads=None, inbox=True):
        if not sandbox:
            return []
        out = ["--sandbox"]
        if inbox:
            out += ["--inbox-dir", self.inbox]
        for d in [self.save, self.save2] if downloads is None else downloads:
            out += ["--download-dir", d]
        return out

    def start(self, sandbox=True, profile=None, bind=True, **kw):
        h = Helper(
            [OPTS.helper] + self.args(sandbox, **kw),
            profile or self.profile,
            None,
            os.path.join(self.dir, "stderr.log"),
        )
        self.helpers.append(h)
        ready = h.wait_ready()
        assert ready.get("sandbox") is sandbox, "ready event: %r" % ready
        if bind:
            h.call("setConfig", {"bindInterface": "lo0"})
        return h

    def swarm(self, fixture, **seed_args):
        tracker = swarm.Tracker()
        seed = swarm.Seed([fixture], **seed_args)
        tracker.peers.append(("127.0.0.1", seed.port))
        self.closers += [tracker.close, seed.close]
        return tracker, seed

    def torrent(self, fixture, folder=None, announce=None, name=None):
        path = os.path.join(folder or self.inbox, name or fixture.name + ".torrent")
        fixture.write(path, {"announce": announce} if announce else None)
        return path

    def note(self, text):
        self.notes.append(text)

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


def rehash(fx):
    fx.info_bytes = bencode.encode(fx.info)
    fx.info_hash = hashlib.sha1(fx.info_bytes).digest()
    fx.tid = fx.info_hash.hex()
    return fx


def with_attrs(fx, attrs):
    info = dict(fx.info)
    files = [dict(f) for f in info["files"]]
    for index, attr in attrs.items():
        files[index]["attr"] = attr
    info["files"] = files
    fx.info = info
    return rehash(fx)


def fixture_with_tiny_file(name):
    files = [
        (("a.txt",), make_torrent.deterministic_bytes(name + "a", 40000)),
        (("tiny.bin",), make_torrent.deterministic_bytes(name + "t", 100)),
        (("c.bin",), make_torrent.deterministic_bytes(name + "c", 25000)),
    ]
    return with_attrs(make_torrent.Fixture(name, files), {1: b"x"})


def query(pid, op, arg=None):
    r = subprocess.run(
        [OPTS.query, str(pid), op] + ([arg] if arg else []),
        capture_output=True,
        text=True,
        timeout=10,
    )
    return r.stdout.strip()


def quarantine(path):
    r = subprocess.run(
        ["/usr/bin/xattr", "-p", "com.apple.quarantine", path], capture_output=True, text=True
    )
    return r.stdout.strip() if r.returncode == 0 else None


def assert_finalized(path):
    mode = os.lstat(path).st_mode
    assert stat.S_ISREG(mode), "%s is not a regular file" % path
    assert not mode & 0o111, "%s still has execute bits (%o)" % (path, mode & 0o777)
    value = quarantine(path)
    assert value, "%s has no com.apple.quarantine" % path
    return value


def completed_events(h, tid, since=0):
    return [e for e in h.events_of("completed", since) if e.get("tid") == tid]


def detail_priorities(h, tid):
    return [f["priority"] for f in h.call("detail", {"tid": tid})["files"]]


def run_helper(args, timeout=10):
    try:
        return subprocess.run(
            [OPTS.helper] + args,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        raise AssertionError("the helper did not exit: %r" % args)


# (1) The built-in profile and the command line ----------------------------


@test
def built_in_profile_is_the_profile_file(ctx):
    with open(os.path.join(TORRENT_DIR, "src", "sandbox_profile.hpp")) as f:
        header = f.read()
    with open(os.path.join(TORRENT_DIR, "sandbox", "m6e-torrent.sb")) as f:
        profile = f.read()
    start = header.index('R"SBPL(') + len('R"SBPL(')
    end = header.index(')SBPL"', start)
    assert header[start:end] == profile, (
        "src/sandbox_profile.hpp differs from sandbox/m6e-torrent.sb; copy the file into it"
    )


@test
def bad_sandbox_arguments_exit_2(ctx):
    a_file = os.path.join(ctx.dir, "a-file")
    with open(a_file, "w") as f:
        f.write("x")
    base = ["--protocol", "1", "--profile-dir", ctx.profile]
    extra = [os.path.join(ctx.dir, "d%d" % i) for i in range(5)]
    for d in extra:
        os.makedirs(d)
    cases = [
        ["--download-dir", ctx.save],
        ["--inbox-dir", ctx.inbox],
        ["--sandbox", "--download-dir", "relative/save"],
        ["--sandbox", "--download-dir", os.path.join(ctx.dir, "missing")],
        ["--sandbox", "--download-dir", a_file],
        ["--sandbox", "--download-dir", "/"],
        ["--sandbox"] + sum((["--download-dir", d] for d in extra), []),
        ["--sandbox", "--download-dir"],
        ["--sandbox", "--inbox-dir", "relative/in"],
        ["--sandbox", "--inbox-dir", ctx.inbox, "--inbox-dir", ctx.inbox],
        ["--sandbox", "--inbox-dir", ctx.profile],
        ["--sandbox", "--inbox-dir", ctx.dir, "--download-dir", ctx.save],
        ["--sandbox", "--inbox-dir", "/"],
    ]
    for args in cases:
        r = run_helper(base + args)
        assert r.returncode == 2, "%r exited with %r: %s" % (args, r.returncode, r.stderr)
        assert "usage:" in r.stderr, "%r printed no usage: %r" % (args, r.stderr)
        assert r.stdout == "", "%r wrote to stdout: %r" % (args, r.stdout)


@test
def good_sandbox_arguments_start(ctx):
    extra = [os.path.join(ctx.dir, "d%d" % i) for i in range(2)]
    for d in extra:
        os.makedirs(d)
    downloads = [ctx.save, ctx.save2] + extra
    h = ctx.start(downloads=downloads + [ctx.save + "/"], bind=False)
    assert query(h.pid, "sandboxed") == "sandboxed"
    h.call("shutdown")
    assert h.wait_exit(3) == 0
    fresh = os.path.join(ctx.dir, "profile-default-inbox")
    h = ctx.start(profile=fresh, inbox=False, downloads=[ctx.save], bind=False)
    inbox = os.path.join(fresh, "inbox")
    assert os.path.isdir(inbox), "the default inbox was not created"
    assert stat.S_IMODE(os.stat(inbox).st_mode) == 0o700
    assert query(h.pid, "file-read-data", fresh) == "allow"
    h = ctx.start(sandbox=False, profile=os.path.join(ctx.dir, "profile-plain"), bind=False)
    assert query(h.pid, "sandboxed") == "not-sandboxed"


# (2) What the live helper's sandbox allows --------------------------------


@test
def live_helper_rules(ctx):
    h = ctx.start()
    pid = h.pid
    files = {}
    for name, folder in (
        ("save", ctx.save),
        ("save2", ctx.save2),
        ("profile", ctx.profile),
        ("inbox", ctx.inbox),
        ("outside", ctx.outside),
    ):
        files[name] = os.path.join(folder, "probe.txt")
        with open(files[name], "w") as f:
            f.write("probe")
    home = None
    for candidate in (".zshrc", ".zprofile", ".bash_profile", ".gitconfig"):
        path = os.path.join(os.path.expanduser("~"), candidate)
        if os.path.isfile(path):
            home = path
            break
    expect = [
        ("sandboxed", None, "sandboxed"),
        ("process-exec*", "/bin/sh", "deny"),
        ("process-exec*", "/bin/echo", "deny"),
        ("process-exec*", os.path.realpath(OPTS.helper), "deny"),
        ("process-fork", None, "deny"),
        ("file-write-data", files["save"], "allow"),
        ("file-write-data", files["save2"], "allow"),
        ("file-write-data", files["profile"], "allow"),
        ("file-write-data", files["inbox"], "deny"),
        ("file-write-data", files["outside"], "deny"),
        ("file-read-data", files["inbox"], "allow"),
        ("file-read-data", files["outside"], "deny"),
        ("file-read-data", "/private/etc/passwd", "deny"),
        ("file-read-data", os.path.expanduser("~/Library/Keychains"), "deny"),
        ("file-read-metadata", files["outside"], "allow"),
        ("network-outbound", None, "allow"),
        ("network-inbound", None, "allow"),
        ("mach-lookup", "com.apple.PowerManagement.control", "allow"),
        ("mach-lookup", "com.apple.SystemConfiguration.configd", "allow"),
        ("mach-lookup", "com.apple.pasteboard.1", "deny"),
        ("mach-lookup", "com.apple.coreservices.launchservicesd", "deny"),
    ]
    if home:
        expect.append(("file-read-data", home, "deny"))
        expect.append(("file-write-data", home, "deny"))
    wrong = []
    for op, arg, want in expect:
        got = query(pid, op, arg)
        if got != want:
            wrong.append("%s %s: %s, expected %s" % (op, arg or "", got, want))
    assert not wrong, "; ".join(wrong)
    with open(files["outside"]) as f:
        assert f.read() == "probe"


# (3) Real transfers under the sandbox --------------------------------------


@test
def download_over_loopback_finalizes_and_holds_power(ctx):
    fx = with_attrs(make_torrent.multi_file(name="m6e-sandboxed"), {1: b"x"})
    tracker, seed = ctx.swarm(fx, block_delay=0.4)
    h = ctx.start()
    pid = h.pid
    announce = tracker.url.replace("127.0.0.1", "localhost")
    added = add_file(h, ctx.torrent(fx, announce=announce))
    tid = added["tid"]
    since = h.mark()
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    assert wait_until(lambda: tracker.announces_for(tid), 20), (
        "no announce reached the tracker by its name localhost (DNS in the sandbox)"
    )
    held = False
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and not held:
        out = subprocess.run(["pmset", "-g", "assertions"], capture_output=True, text=True).stdout
        held = any(POWER_NAME in line and "pid %d(" % pid in line for line in out.splitlines())
        if not held:
            time.sleep(0.3)
    assert held, "no %r power assertion of pid %d during the transfer" % (POWER_NAME, pid)
    assert run_tests.sockets(pid), "no socket while downloading"
    wait_done(h, tid, since)
    verify_output(fx, ctx.save)
    events = completed_events(h, tid, since)
    assert events and "warning" not in events[0], "completed event: %r" % events
    values = [
        assert_finalized(content_path(ctx.save, fx, rel)) for rel, _ in fx.files
    ]
    ctx.note("quarantine written in the sandbox: %s" % values[0].split(";")[0])


@test
def confirm_outside_the_allowed_folders_is_refused(ctx):
    fx = make_torrent.multi_file(name="m6e-outside")
    h = ctx.start()
    added = add_file(h, ctx.torrent(fx))
    tid = added["tid"]
    for folder in (ctx.outside, ctx.dir, os.path.dirname(ctx.dir)):
        err = h.err(
            "confirm", dict(confirm_args(added["metadata"], folder), tid=tid), {"unsafe-path"}
        )
        assert "sandbox" in err["message"], err
    assert os.listdir(ctx.outside) == []
    sub = os.path.join(ctx.save2, "deeper")
    os.makedirs(sub)
    result = h.call("confirm", dict(confirm_args(added["metadata"], sub, start=False), tid=tid))
    assert result["status"]["state"] == "paused"


@test
def add_reads_only_the_inbox_and_download_folders(ctx):
    h = ctx.start()
    out_fx = make_torrent.multi_file(name="m6e-unreadable")
    err = h.err("add", {"file": ctx.torrent(out_fx, folder=ctx.outside)}, {"io-error"})
    assert "not permitted" in err["message"], err
    add_file(h, ctx.torrent(out_fx))
    add_file(h, ctx.torrent(make_torrent.multi_file(name="m6e-in-save"), folder=ctx.save))


def plant_link(ctx, sandbox, single):
    if single:
        fx = make_torrent.single_file(name="m6e-planted.bin", size=200000)
    else:
        fx = make_torrent.multi_file(name="m6e-planted")
    tracker, seed = ctx.swarm(fx, block_delay=0.2)
    target = os.path.join(ctx.outside, "target")
    os.makedirs(target)
    sentinel = os.path.join(target, "sentinel")
    with open(sentinel, "wb") as f:
        f.write(b"keep")
    h = ctx.start(sandbox=sandbox)
    added = add_file(h, ctx.torrent(fx, announce=tracker.url))
    tid = added["tid"]
    since = h.mark()
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    planted = os.path.join(ctx.save, fx.name)
    if single:
        os.symlink(sentinel, planted)
    else:
        os.rename(planted, planted + ".checked")
        os.symlink(target, planted)

    def escaped():
        with open(sentinel, "rb") as f:
            return f.read() != b"keep" or sorted(os.listdir(target)) != ["sentinel"]

    def refused():
        for e in h.events_of("error", since):
            if e.get("tid") == tid:
                return e
        st = state_of(h, tid)
        return st and st["error"]

    wait_until(lambda: escaped() or refused(), 30, 0.2)
    return h, tid, escaped(), refused(), seed


@test
def sandbox_stops_a_link_planted_after_the_check(ctx):
    for single in (False, True):
        sub = Ctx(ctx.dir, "single" if single else "multi")
        try:
            h, tid, escaped, refused, seed = plant_link(sub, True, single)
            assert not escaped, "a link planted after the check redirected a write out of the sandbox"
            assert refused, "the helper reported nothing (seed served %d bytes)" % seed.bytes_served
            message = refused.get("message", "")
            assert "not permitted" in message, "the error is not the sandbox's: %r" % refused
            ctx.note("%s: %s" % ("single" if single else "multi", message))
        finally:
            sub.finish()


@test
def control_without_the_sandbox_a_planted_link_escapes(ctx):
    h, tid, escaped, refused, seed = plant_link(ctx, False, False)
    ctx.note(
        "unsandboxed, the link planted after the check %s"
        % ("redirected the download out of the save folder (the gap the sandbox closes)"
           if escaped else "was not followed (%r)" % (refused,))
    )


@test
def torrents_outside_the_folders_stay_paused(ctx):
    fx, fx2 = make_torrent.multi_file(name="m6e-first"), make_torrent.multi_file(name="m6e-second")
    fx.write_content(ctx.save)
    fx2.write_content(ctx.save2)
    h = ctx.start()
    for f, folder in ((fx, ctx.save), (fx2, ctx.save2)):
        added = add_file(h, ctx.torrent(f))
        h.call("confirm", dict(confirm_args(added["metadata"], folder), tid=added["tid"]))
    h.call("shutdown")
    assert h.wait_exit(3) == 0
    h = ctx.start(downloads=[ctx.save])
    since = h.mark()
    listed = {s["tid"]: s for s in h.call("restore", {"start": True})["torrents"]}
    assert set(listed) == {fx.tid, fx2.tid}, listed
    assert listed[fx2.tid]["state"] == "paused", listed[fx2.tid]
    errors = [e for e in h.events_of("error", since) if e.get("tid") == fx2.tid]
    assert errors and errors[0]["code"] == "unsafe-path", errors
    h.err("resume", {"tid": fx2.tid}, {"unsafe-path"})
    h.call("resume", {"tid": fx.tid})


# (4) setConfig --------------------------------------------------------------


ALL_KEYS = {
    "encryption": "enabled",
    "dht": False,
    "pex": False,
    "lpd": False,
    "portMapping": False,
    "listenPort": 0,
    "proxy": {"type": "none"},
    "bindInterface": "lo0",
    "seedGoal": {"ratio": None, "hours": None},
    "peerIdPrefix": None,
    "maxConnections": 50,
    "userAgent": None,
}


def listed_once(result, keys):
    applied, unsupported = result["applied"], result["unsupported"]
    both = applied + unsupported
    assert sorted(both) == sorted(keys), "setConfig listed %r for %r" % (result, sorted(keys))
    assert len(both) == len(set(both)), "a key is listed twice: %r" % result
    return set(applied), set(unsupported)


@test
def set_config_lists_every_key_once(ctx):
    for sandbox in (True, False):
        h = ctx.start(sandbox=sandbox, profile=os.path.join(ctx.dir, "profile-%s" % sandbox))
        inbox = ctx.inbox if sandbox else os.path.join(h.profile, "inbox")
        os.makedirs(inbox, exist_ok=True)
        path = os.path.join(inbox, "list.p2p")
        with open(path, "wb") as f:
            f.write(b"Doc range:192.0.2.0-192.0.2.255\n")
        keys = dict(ALL_KEYS, ipFilterPath=path, m6eNotAKey=1, utp=True)
        applied, unsupported = listed_once(h.call("setConfig", keys), keys)
        assert unsupported == {"m6eNotAKey", "utp"}, unsupported
        for key, value in keys.items():
            listed_once(h.call("setConfig", {key: value}), [key])
        listed_once(h.call("setConfig", {}), [])
        rid = h.next_id
        h.next_id += 1
        h.send_raw(
            (
                '{"id": %d, "op": "setConfig", "args": {"dht": false, "pex": true, "dht": true}}\n'
                % rid
            ).encode()
        )
        reply = h.wait_reply(rid)
        assert reply["ok"], reply
        listed_once(reply["result"], ["dht", "pex"])
        h.err("setConfig", {"dht": "yes", "pex": True}, {"bad-request"})


@test
def ip_filter_is_read_during_set_config(ctx):
    for sandbox in (True, False):
        sub = Ctx(ctx.dir, "sandboxed" if sandbox else "plain")
        try:
            fx = make_torrent.single_file(name="m6e-filtered.bin", size=60000)
            tracker, seed = sub.swarm(fx)
            h = sub.start(sandbox=sandbox)
            inbox = sub.inbox if sandbox else os.path.join(h.profile, "inbox")
            os.makedirs(inbox, exist_ok=True)
            path = os.path.join(inbox, "block.p2p")
            with open(path, "wb") as f:
                f.write(b"Loopback test:127.0.0.1-127.0.0.1\n")
            r = h.call("setConfig", {"ipFilterPath": path})
            os.remove(path)
            assert r["applied"] == ["ipFilterPath"] and r["ipFilter"]["ranges"] == 1, r
            added = add_file(h, sub.torrent(fx, announce=tracker.url))
            tid = added["tid"]
            since = h.mark()
            h.call("confirm", dict(confirm_args(added["metadata"], sub.save), tid=tid))
            time.sleep(6)
            assert not tracker.announces, "the filter read from a deleted file was not applied"
            assert not seed.handshakes, "a filtered peer was contacted"
            assert h.call("setConfig", {"ipFilterPath": None})["applied"] == ["ipFilterPath"]
            h.call("pause", {"tid": tid})
            h.call("resume", {"tid": tid})
            wait_done(h, tid, since, 40)
            verify_output(fx, sub.save)
        finally:
            sub.finish()


# (5) setFilePriority --------------------------------------------------------


@test
def set_file_priority_checks_its_arguments(ctx):
    h = ctx.start()
    fx = make_torrent.multi_file(name="m6e-prio-args")
    added = add_file(h, ctx.torrent(fx))
    tid = added["tid"]
    files = [{"index": 0, "priority": 1}]
    h.err("setFilePriority", {"tid": tid, "files": files}, {"not-found"})
    h.err("setFilePriority", {"tid": "ab" * 20, "files": files}, {"not-found"})
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save, start=False), tid=tid))
    for args in (
        {"files": files},
        {"tid": tid},
        {"tid": tid, "files": {"index": 0}},
        {"tid": tid, "files": [3]},
        {"tid": tid, "files": [{"index": 3, "priority": 1}]},
        {"tid": tid, "files": [{"index": -1, "priority": 1}]},
        {"tid": tid, "files": [{"index": 0, "priority": 4}]},
        {"tid": tid, "files": [{"index": 0, "priority": -1}]},
        {"tid": tid, "files": [{"index": 0, "priority": "high"}]},
        {"tid": tid, "files": [{"index": 0}]},
        {"tid": tid, "files": [{"index": 0, "priority": 1}, {"index": 0, "priority": 2}]},
        {"tid": "XYZ", "files": files},
    ):
        h.err("setFilePriority", args, {"bad-request", "not-found"})
    assert detail_priorities(h, tid) == [2, 2, 2], "a refused request changed priorities"


@test
def set_file_priority_shows_at_once_and_is_saved(ctx):
    h = ctx.start()
    fx = make_torrent.multi_file(name="m6e-prio-saved")
    added = add_file(h, ctx.torrent(fx))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save, start=False), tid=tid))
    st = h.call(
        "setFilePriority",
        {"tid": tid, "files": [{"index": 0, "priority": 0}, {"index": 2, "priority": 3}]},
    )["status"]
    check_status(None, st, tid)
    assert detail_priorities(h, tid) == [0, 2, 3]
    h.call("setFilePriority", {"tid": tid, "files": [{"index": 1, "priority": 1}]})
    assert detail_priorities(h, tid) == [0, 1, 3]
    h.call("setFilePriority", {"tid": tid, "files": []})
    assert detail_priorities(h, tid) == [0, 1, 3]
    wanted = len(fx.files[1][1]) + len(fx.files[2][1])
    assert wait_until(
        lambda: wanted <= state_of(h, tid)["size"] < len(fx.payload), 5
    ), "size is not that of the selected pieces: %r" % state_of(h, tid)
    assert not os.listdir(ctx.save), "a paused torrent wrote to its save folder"
    h.call("shutdown")
    assert h.wait_exit(3) == 0
    h = ctx.start()
    h.call("restore", {"start": False})
    assert detail_priorities(h, tid) == [0, 1, 3], "priorities were not saved"
    h.call("setFilePriority", {"tid": tid, "files": [{"index": 0, "priority": 2}]})
    time.sleep(1.0)
    h.kill()
    h = ctx.start()
    h.call("restore", {"start": False})
    assert detail_priorities(h, tid) == [2, 1, 3], "priorities were not saved before a crash"


@test
def set_file_priority_fetches_a_skipped_file_and_finalizes_it(ctx):
    fx = with_attrs(make_torrent.multi_file(name="m6e-prio-later"), {1: b"x"})
    tracker, seed = ctx.swarm(fx)
    h = ctx.start()
    added = add_file(h, ctx.torrent(fx, announce=tracker.url))
    tid = added["tid"]
    since = h.mark()
    h.call(
        "confirm",
        dict(confirm_args(added["metadata"], ctx.save, priorities={1: 0}), tid=tid),
    )
    h.wait_event("completed", lambda e: e.get("tid") == tid, 60, since)
    skipped = content_path(ctx.save, fx, fx.files[1][0])
    assert not os.path.exists(skipped) or os.path.getsize(skipped) < len(fx.files[1][1])
    again = h.mark()
    h.call("setFilePriority", {"tid": tid, "files": [{"index": 1, "priority": 3}]})
    assert detail_priorities(h, tid) == [2, 3, 2]
    h.wait_event("completed", lambda e: e.get("tid") == tid, 60, again)
    verify_output(fx, ctx.save)
    for rel, _ in fx.files:
        assert_finalized(content_path(ctx.save, fx, rel))
    assert state_of(h, tid)["progress"] == 1


@test
def set_file_priority_of_a_file_already_downloaded_finalizes_it(ctx):
    fx = fixture_with_tiny_file("m6e-prio-tiny")
    tracker, seed = ctx.swarm(fx)
    h = ctx.start()
    added = add_file(h, ctx.torrent(fx, announce=tracker.url))
    tid = added["tid"]
    since = h.mark()
    h.call(
        "confirm",
        dict(confirm_args(added["metadata"], ctx.save, priorities={1: 0}), tid=tid),
    )
    h.wait_event("completed", lambda e: e.get("tid") == tid, 60, since)
    tiny = content_path(ctx.save, fx, fx.files[1][0])
    assert not os.path.exists(tiny), "a skipped file was written"
    served = seed.bytes_served
    again = h.mark()
    h.call("setFilePriority", {"tid": tid, "files": [{"index": 1, "priority": 2}]})
    event = h.wait_event("completed", lambda e: e.get("tid") == tid, 20, again)
    assert "warning" not in event, event
    verify_output(fx, ctx.save)
    assert_finalized(tiny)
    assert seed.bytes_served == served, "the tiny file's data was fetched again"


def run_one(fn, root):
    ctx = Ctx(root, fn.__name__)
    status, reason, detail = "ok", "", ""
    try:
        fn(ctx)
    except Skip as e:
        status, reason = "skip", str(e)
    except Exception as e:
        status, reason = "fail", "%s: %s" % (type(e).__name__, e)
        detail = traceback.format_exc()
    try:
        ctx.finish()
    except Exception as e:
        if status != "fail":
            status, reason = "fail", "%s: %s" % (type(e).__name__, e)
    return status, reason, detail, ctx


def main():
    global OPTS
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--helper", default=os.path.join(TORRENT_DIR, "build", "m6e-torrent"))
    ap.add_argument("--query", required=True, help="the sandbox_query program")
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    OPTS = ap.parse_args()
    OPTS.helper = os.path.abspath(OPTS.helper)
    for path in (OPTS.helper, OPTS.query):
        if not os.path.exists(path):
            print("Bail out! not found: %s" % path)
            return 2
    root = os.path.realpath(tempfile.mkdtemp(prefix="m6e-torrent-sandbox-"))
    selected = [f for f in REGISTRY if not OPTS.only or any(o in f.__name__ for o in OPTS.only)]
    print("TAP version 13")
    print("1..%d" % len(selected))
    counts = {"ok": 0, "fail": 0, "skip": 0}
    for n, fn in enumerate(selected, 1):
        t0 = time.monotonic()
        status, reason, detail, ctx = run_one(fn, root)
        counts[status] += 1
        if status == "ok":
            print("ok %d - %s" % (n, fn.__name__))
        elif status == "skip":
            print("ok %d - %s # SKIP %s" % (n, fn.__name__, reason))
        else:
            print("not ok %d - %s" % (n, fn.__name__))
            print("# %s" % reason)
            for line in detail.splitlines()[-8:]:
                print("# %s" % line)
        for note in ctx.notes:
            print("# %s: %s" % (fn.__name__, note))
        print("# %s took %.1fs" % (fn.__name__, time.monotonic() - t0))
        sys.stdout.flush()
    print("# pass %d fail %d skip %d" % (counts["ok"], counts["fail"], counts["skip"]))
    if OPTS.keep:
        print("# kept %s" % root)
    else:
        shutil.rmtree(root, ignore_errors=True)
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
