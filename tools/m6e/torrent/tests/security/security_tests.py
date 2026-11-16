#!/usr/bin/env python3
"""Security regression tests for the m6e-torrent helper (P8.5 review fixes):
finished-file protection, deleting only the engine's own files, protected
save folders, the sandbox's network, metadata and sysctl rules, the helper
under an empty environment, and hybrid torrents added by a btmh magnet link.

    python3 -I tools/m6e/torrent/tests/security/security_tests.py \
        --helper <binary> --query <sandbox_query> --probe <net_probe>

Only synthetic fixtures on 127.0.0.1 (spec 8.9, D38): a local tracker, a local
seed and helpers seeding to each other. No real torrent, magnet link, tracker
or DHT node appears here. Output is TAP; the exit status is 1 when any test
fails.
"""

import argparse
import os
import pwd
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
TORRENT_DIR = os.path.dirname(TESTS)
sys.path.insert(0, TESTS)
sys.path.insert(0, HERE)

import fixtures  # noqa: E402
import run_tests  # noqa: E402

make_torrent = fixtures.make_torrent
swarm = fixtures.swarm
Helper = run_tests.Helper
Skip = run_tests.Skip
add_file = run_tests.add_file
confirm_args = run_tests.confirm_args
content_path = run_tests.content_path
state_of = run_tests.state_of
wait_until = run_tests.wait_until

OPTS = None
REGISTRY = []
QUARANTINE = "com.apple.quarantine"
HOME = pwd.getpwuid(os.getuid()).pw_dir


def test(fn):
    REGISTRY.append(fn)
    return fn


class CleanHelper(Helper):
    """A helper started with exactly `env` as its environment."""

    def __init__(self, cmd, profile, env, stderr_path):
        saved = dict(os.environ)
        os.environ.clear()
        try:
            super().__init__(cmd, profile, env, stderr_path)
        finally:
            os.environ.update(saved)


class Ctx:
    def __init__(self, root, name):
        self.dir = os.path.join(root, name)
        self.profile = os.path.join(self.dir, "profile")
        self.save = os.path.join(self.dir, "save")
        self.inbox = os.path.join(self.dir, "in")
        self.outside = os.path.join(self.dir, "outside")
        for d in (self.save, self.inbox, self.outside):
            os.makedirs(d)
        self.helpers = []
        self.closers = []
        self.notes = []

    def args(self, sandbox):
        if not sandbox:
            return []
        return ["--sandbox", "--inbox-dir", self.inbox, "--download-dir", self.save]

    def start(self, sandbox=False, profile=None, env=None, clean=False, bind=True):
        cls = CleanHelper if clean else Helper
        h = cls(
            [OPTS.helper] + self.args(sandbox),
            profile or self.profile,
            env,
            os.path.join(self.dir, "stderr.log"),
        )
        self.helpers.append(h)
        ready = h.wait_ready()
        assert ready.get("sandbox") is sandbox, "ready event: %r" % ready
        if bind:
            h.call("setConfig", {"bindInterface": "lo0"})
        return h

    def torrent(self, fx, announce=None, name=None, data=None):
        path = os.path.join(self.inbox, name or fx.name + ".torrent")
        if data is None:
            fx.write(path, {"announce": announce} if announce else None)
        else:
            with open(path, "wb") as f:
                f.write(data)
        return path

    def partial_swarm(self, fx, have):
        tracker, seed = fixtures.partial_swarm(fx, have)
        self.closers += [tracker.close, seed.close]
        return tracker, seed

    def swarm(self, fx):
        tracker = swarm.Tracker()
        seed = swarm.Seed([fx])
        tracker.peers.append(("127.0.0.1", seed.port))
        self.closers += [tracker.close, seed.close]
        return tracker, seed

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


def quarantine(path):
    r = subprocess.run(["/usr/bin/xattr", "-p", QUARANTINE, path], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def assert_protected(path):
    mode = os.lstat(path).st_mode
    assert stat.S_ISREG(mode), "%s is not a regular file" % path
    assert not mode & 0o111, "%s has execute bits (%o)" % (path, mode & 0o777)
    assert quarantine(path), "%s has no %s" % (path, QUARANTINE)


def tamper(path):
    os.chmod(path, 0o755)
    subprocess.run(["/usr/bin/xattr", "-d", QUARANTINE, path], capture_output=True)
    assert os.stat(path).st_mode & 0o111 and not quarantine(path)


def file_progress(h, tid):
    return [f["progress"] for f in h.call("detail", {"tid": tid})["files"]]


def read(path):
    with open(path, "rb") as f:
        return f.read()


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def run_helper(args, env=None):
    try:
        return subprocess.run(
            [OPTS.helper] + args,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=10,
            env=env,
        )
    except subprocess.TimeoutExpired:
        raise AssertionError("the helper did not exit: %r" % args)


def query(pid, op, arg=None):
    r = subprocess.run(
        [OPTS.query, str(pid), op] + ([arg] if arg else []),
        capture_output=True,
        text=True,
        timeout=10,
    )
    return r.stdout.strip()


def sandbox_params(ctx):
    real = os.path.realpath
    return [
        "HELPER=" + real(OPTS.probe),
        "EXEC_HELPER=NO",
        "DOWNLOAD_1=" + real(ctx.save),
        "DOWNLOAD_2=" + real(ctx.save),
        "DOWNLOAD_3=" + real(ctx.save),
        "DOWNLOAD_4=" + real(ctx.save),
        "TORRENTS_DIR=" + real(ctx.profile),
        "INBOX_DIR=" + real(ctx.inbox),
    ]


def probe(ctx, *command):
    os.makedirs(ctx.profile, exist_ok=True)
    profile = os.path.join(TORRENT_DIR, "sandbox", "m6e-torrent.sb")
    r = subprocess.run(
        [OPTS.probe, profile] + sandbox_params(ctx) + ["--"] + list(command),
        capture_output=True,
        text=True,
        timeout=20,
    )
    return r.returncode, (r.stdout + r.stderr).strip()


# (1) Finished-file protection ---------------------------------------------


def partial_download(ctx, name, sandbox=False):
    fx = fixtures.executable_fixture(name)
    tracker, seed = ctx.partial_swarm(fx, have=[0])
    h = ctx.start(sandbox=sandbox)
    added = add_file(h, ctx.torrent(fx, tracker.url))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    paths = [content_path(ctx.save, fx, rel) for rel, _ in fx.files]
    prepared = [os.path.isfile(p) and not os.stat(p).st_mode & 0o111 and quarantine(p)
                for p in paths]
    assert wait_until(lambda: file_progress(h, tid)[:2] == [1, 1], 40), (
        "the partial seed did not deliver piece 0: %r" % file_progress(h, tid)
    )
    for (rel, data), path in zip(fx.files[:2], paths[:2]):
        assert read(path) == data, "%s has the wrong content" % path
        assert_protected(path)
    assert all(prepared), "the files were not prepared before the transfer: %r" % prepared
    return fx, h, tid, paths


@test
def executable_attribute_never_reaches_a_partial_file(ctx):
    """Without the fix libtorrent creates Play.command and X.app/.../X with
    mode 0755 (BEP 47 `x`) and, as big.bin never completes, nothing ever
    clears it or quarantines them."""
    fx, h, tid, paths = partial_download(ctx, "m6e-sec-exec")
    for path in paths:
        assert_protected(path)
    st = state_of(h, tid)
    assert st["state"] == "downloading", st
    assert not h.events_of("completed"), "the torrent cannot be complete"
    d = h.call("detail", {"tid": tid})
    assert not any(f.get("existing") for f in d["files"]), d["files"]


@test
def executable_attribute_never_reaches_a_partial_file_sandboxed(ctx):
    fx, h, tid, paths = partial_download(ctx, "m6e-sec-exec-sbx", sandbox=True)
    for path in paths:
        assert_protected(path)


@test
def pause_shutdown_and_remove_protect_the_wanted_files(ctx):
    fx, h, tid, paths = partial_download(ctx, "m6e-sec-backstop")
    play = paths[0]
    tamper(play)
    h.call("pause", {"tid": tid})
    assert_protected(play)

    h.call("resume", {"tid": tid})
    tamper(play)
    h.call("pauseAll")
    assert_protected(play)
    h.call("resumeAll")

    tamper(play)
    h.close_stdin()
    assert h.wait_exit(5) == 0
    assert_protected(play)

    again = ctx.start()
    listed = again.call("restore", {"start": False})["torrents"]
    assert [t["tid"] for t in listed] == [tid], listed
    tamper(play)
    again.call("remove", {"tid": tid, "deleteFiles": False})
    for path in paths:
        assert os.path.isfile(path), "remove without deleting lost %s" % path
    assert_protected(play)


# (11) Deleting files and existing files ------------------------------------


@test
def delete_removes_only_the_files_the_engine_wrote(ctx):
    """libtorrent's delete_files removes every path of the torrent, so the
    user's own sub/b.bin (never selected) would go too."""
    fx = make_torrent.multi_file(name="m6e-sec-delete")
    top = os.path.join(ctx.save, fx.name)
    write(os.path.join(top, "a.txt"), fx.files[0][1])
    write(os.path.join(top, "sub", "b.bin"), b"the user's own file")
    h = ctx.start()
    added = add_file(h, ctx.torrent(fx))
    tid = added["tid"]
    h.call(
        "confirm",
        dict(confirm_args(added["metadata"], ctx.save, priorities={1: 0}), tid=tid),
    )
    files = h.call("detail", {"tid": tid})["files"]
    assert [bool(f.get("existing")) for f in files] == [True, False, False], files
    assert wait_until(lambda: state_of(h, tid)["state"] != "checking", 20)
    assert os.path.isfile(os.path.join(top, "sub", "deep", "c.bin"))
    h.call("remove", {"tid": tid, "deleteFiles": True})
    assert not os.path.exists(os.path.join(top, "a.txt")), "a.txt was not deleted"
    assert not os.path.exists(os.path.join(top, "sub", "deep")), "the empty folder stayed"
    assert read(os.path.join(top, "sub", "b.bin")) == b"the user's own file", (
        "a file the torrent never selected was deleted"
    )
    assert not [n for n in os.listdir(ctx.save) if n.endswith(".parts")]


@test
def existing_is_reported_and_kept_across_restore(ctx):
    fx = make_torrent.multi_file(name="m6e-sec-existing")
    write(os.path.join(ctx.save, fx.name, "sub", "deep", "c.bin"), b"older data")
    h = ctx.start()
    added = add_file(h, ctx.torrent(fx))
    tid = added["tid"]
    h.call(
        "confirm",
        dict(confirm_args(added["metadata"], ctx.save, start=False, priorities={1: 0}), tid=tid),
    )
    flags = lambda helper: [bool(f.get("existing")) for f in helper.call("detail", {"tid": tid})["files"]]
    assert flags(h) == [False, False, True], flags(h)
    write(os.path.join(ctx.save, fx.name, "sub", "b.bin"), b"user data")
    h.call("setFilePriority", {"tid": tid, "files": [{"index": 1, "priority": 2}]})
    assert flags(h) == [False, True, True], flags(h)
    h.call("shutdown")
    h.wait_exit(3)
    again = ctx.start()
    again.call("restore", {"start": False})
    assert flags(again) == [False, True, True], flags(again)


@test
def delete_never_follows_a_link(ctx):
    """libtorrent's delete_files follows a folder replaced by a symbolic link
    and deletes the files of the same names outside the save folder."""
    fx = make_torrent.multi_file(name="m6e-sec-link")
    h = ctx.start()
    added = add_file(h, ctx.torrent(fx))
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
    assert wait_until(lambda: state_of(h, tid)["state"] != "checking", 20)
    h.call("pause", {"tid": tid})
    top = os.path.join(ctx.save, fx.name)
    elsewhere = os.path.join(ctx.outside, "sub")
    write(os.path.join(elsewhere, "b.bin"), b"outside b")
    write(os.path.join(elsewhere, "deep", "c.bin"), b"outside c")
    shutil.rmtree(os.path.join(top, "sub"))
    os.symlink(elsewhere, os.path.join(top, "sub"))
    h.call("remove", {"tid": tid, "deleteFiles": True})
    assert read(os.path.join(elsewhere, "b.bin")) == b"outside b"
    assert read(os.path.join(elsewhere, "deep", "c.bin")) == b"outside c"
    assert not os.path.exists(os.path.join(top, "a.txt"))


# (12) Protected save folders -----------------------------------------------


@test
def download_folders_that_hold_home_or_the_profile_exit_2(ctx):
    os.makedirs(ctx.profile)
    other_profile = os.path.join(ctx.dir, "p2", "profile")
    box = os.path.join(ctx.save, "box")
    os.makedirs(box)
    base = ["--protocol", "1", "--profile-dir", ctx.profile, "--sandbox"]
    cases = [
        base + ["--download-dir", HOME],
        base + ["--download-dir", os.path.join(HOME, "Library")],
        base + ["--download-dir", os.path.dirname(HOME)],
        base + ["--download-dir", ctx.dir],
        base + ["--download-dir", ctx.profile],
        ["--protocol", "1", "--profile-dir", other_profile, "--sandbox", "--inbox-dir", box,
         "--download-dir", ctx.save],
    ]
    for args in cases:
        r = run_helper(args)
        assert r.returncode == 2, "%r exited with %r: %s" % (args, r.returncode, r.stderr)
        assert "usage:" in r.stderr and r.stdout == "", (args, r.stderr, r.stdout)
    r = run_helper(base + ["--download-dir", ctx.save])
    assert r.returncode == 0, "a plain download folder must start: %s" % r.stderr


@test
def confirm_refuses_protected_save_folders(ctx):
    for sandbox in (False, True):
        profile = os.path.join(ctx.dir, "profile-%s" % sandbox)
        h = ctx.start(sandbox=sandbox, profile=profile)
        inbox = ctx.inbox if sandbox else os.path.join(profile, "inbox")
        os.makedirs(inbox, exist_ok=True)
        fx = make_torrent.multi_file(name="m6e-sec-protected-%s" % sandbox)
        added = add_file(h, ctx.torrent(fx))
        tid = added["tid"]
        for folder in (HOME, os.path.join(HOME, "Library"), ctx.dir, profile, inbox):
            error = h.err(
                "confirm",
                dict(confirm_args(added["metadata"], folder, start=False), tid=tid),
                {"unsafe-path"},
            )
            assert folder in error["message"] or os.path.realpath(folder) in error["message"], error
            assert not os.path.exists(os.path.join(folder, fx.name))
        h.call("confirm", dict(confirm_args(added["metadata"], ctx.save, start=False), tid=tid))
        h.call("remove", {"tid": tid, "deleteFiles": False})


# Hybrid torrents added by a btmh magnet link --------------------------------


@test
def btmh_magnet_of_a_hybrid_torrent_survives_restore(ctx):
    """Without the fix the torrent is saved as <v2 id>.resume and restore
    skips it, because the info hash it computes is the v1 one."""
    data = make_torrent.deterministic_bytes("m6e-sec-hybrid", 100000)
    fx = fixtures.HybridFixture("m6e-sec-hybrid.bin", data)
    seeder = ctx.start(profile=os.path.join(ctx.dir, "seeder"))
    seed_save = os.path.join(ctx.dir, "seed-save")
    os.makedirs(seed_save)
    fx.write_content(seed_save)
    since = seeder.mark()
    added = add_file(seeder, ctx.torrent(fx, data=fx.torrent_bytes()))
    assert added["tid"] == fx.tid, (added["tid"], fx.tid)
    seeder.call("confirm", dict(confirm_args(added["metadata"], seed_save), tid=fx.tid))
    port = seeder.wait_event("listening", timeout=15, since=since)["port"]
    assert wait_until(lambda: state_of(seeder, fx.tid)["state"] == "seeding", 20)

    tracker = swarm.Tracker()
    tracker.peers.append(("127.0.0.1", port))
    ctx.closers.append(tracker.close)
    h = ctx.start()
    r = h.call("add", {"magnet": fx.btmh_magnet(tracker.url, ["127.0.0.1:%d" % port])})
    assert r["tid"] == fx.v2_tid and r["state"] == "fetching-metadata", r
    md = h.wait_event("metadata", lambda e: e.get("tid") == fx.v2_tid, timeout=40)["metadata"]
    h.call("confirm", dict(confirm_args(md, ctx.save, start=False), tid=fx.v2_tid))
    dup = h.err("add", {"file": ctx.torrent(fx, data=fx.torrent_bytes(), name="again.torrent")},
                {"duplicate"})
    assert dup.get("tid") == fx.v2_tid, dup
    h.call("shutdown")
    h.wait_exit(3)

    again = ctx.start()
    listed = again.call("restore", {"start": False})["torrents"]
    assert [t["tid"] for t in listed] == [fx.v2_tid], listed
    again.call("resume", {"tid": fx.v2_tid})
    assert wait_until(lambda: state_of(again, fx.v2_tid)["state"] in ("seeding", "completed"), 60)
    assert read(os.path.join(ctx.save, fx.name)) == data


# (6) The sandbox profile and the environment --------------------------------


class UnixServer:
    def __init__(self, path):
        self.path = path
        self.accepted = 0
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.bind(path)
        self.sock.listen(4)
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            self.accepted += 1
            conn.close()

    def close(self):
        self.sock.close()
        try:
            os.unlink(self.path)
        except OSError:
            pass


@test
def sandbox_allows_ip_but_no_unix_socket(ctx):
    """With `(allow network*)` a sandboxed helper could connect to any AF_UNIX
    socket: ssh-agent, gpg-agent, the Docker socket."""
    path = os.path.join(tempfile.gettempdir(), "m6e-sec-%d.sock" % os.getpid())
    server = UnixServer(path)
    ctx.closers.append(server.close)
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    ctx.closers.append(listener.close)
    rc, out = probe(ctx, "unix", path)
    assert rc == 1, "connect to a unix socket: %s" % out
    assert server.accepted == 0
    rc, out = probe(ctx, "unix", "/private/var/run/mDNSResponder")
    assert rc == 0, "the mDNSResponder socket: %s" % out
    rc, out = probe(ctx, "tcp", str(listener.getsockname()[1]))
    assert rc == 0, "loopback TCP: %s" % out
    rc, out = probe(ctx, "udp")
    assert rc == 0, "UDP bind and send: %s" % out

    h = ctx.start(sandbox=True)
    assert query(h.pid, "network-outbound", path) == "deny"
    assert query(h.pid, "network-outbound", "/private/var/run/mDNSResponder") == "allow"
    assert query(h.pid, "network-outbound") == "allow"
    assert query(h.pid, "network-inbound") == "allow"


@test
def sandbox_narrows_metadata_and_sysctl(ctx):
    h = ctx.start(sandbox=True)
    save = os.path.realpath(ctx.save)
    expect = [
        ("file-read-metadata", save, "allow"),
        ("file-read-metadata", os.path.dirname(save), "allow"),
        ("file-read-metadata", "/", "allow"),
        ("file-read-metadata", "/usr/lib/libSystem.B.dylib", "allow"),
        ("file-read-metadata", os.path.realpath(ctx.outside), "deny"),
        ("file-read-metadata", os.path.join(HOME, "Library"), "deny"),
        ("file-read-metadata", os.path.join(HOME, ".ssh"), "deny"),
    ]
    wrong = []
    for op, arg, want in expect:
        got = query(h.pid, op, arg)
        if got != want:
            wrong.append("%s %s: %s, expected %s" % (op, arg, got, want))
    assert not wrong, "; ".join(wrong)
    for command, want in (
        (["sysctl", "hw.ncpu"], 0),
        (["sysctl", "kern.osversion"], 0),
        (["sysctl-proc"], 1),
        (["sysctl-procargs"], 1),
    ):
        rc, out = probe(ctx, *command)
        assert rc == want, "%s: exit %d, expected %d: %s" % (command, rc, want, out)
    rc, out = probe(ctx, "stat", os.path.join(HOME, "Library"))
    assert rc == 1, "stat of ~/Library: %s" % out
    rc, out = probe(ctx, "stat", ctx.save)
    assert rc == 0, "stat of the download folder: %s" % out


@test
def helper_works_with_an_empty_environment(ctx):
    """The service starts the helper with only the M6E_TORRENT_* variables."""
    for sandbox in (False, True):
        fx = make_torrent.multi_file(name="m6e-sec-clean-%s" % sandbox)
        tracker, seed = ctx.swarm(fx)
        profile = os.path.join(ctx.dir, "profile-%s" % sandbox)
        h = ctx.start(sandbox=sandbox, profile=profile, clean=True,
                      env={"M6E_TORRENT_IDLE_SECONDS": "0"})
        added = add_file(h, ctx.torrent(fx, tracker.url))
        tid = added["tid"]
        h.call("confirm", dict(confirm_args(added["metadata"], ctx.save), tid=tid))
        run_tests.wait_done(h, tid, 0, 40)
        run_tests.verify_output(fx, ctx.save, skip=())
        for rel, _ in fx.files:
            assert_protected(content_path(ctx.save, fx, rel))
        h.call("remove", {"tid": tid, "deleteFiles": True})
        assert not os.path.exists(os.path.join(ctx.save, fx.name))


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
    ap.add_argument("--query", required=True)
    ap.add_argument("--probe", required=True)
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    OPTS = ap.parse_args()
    OPTS.helper = os.path.realpath(OPTS.helper)
    if not os.path.exists(OPTS.helper):
        print("Bail out! helper not found: %s" % OPTS.helper)
        return 2
    run_tests.TARGET["cmd"] = [OPTS.helper]
    root = os.path.realpath(tempfile.mkdtemp(prefix="m6e-torrent-security-"))
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
