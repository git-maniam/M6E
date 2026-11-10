#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check the m6e-torrent helper of a signed M6E.app (spec 8.4, 8.8 item 8).

    python3 -I tools/m6e/package/check-helper.py [--no-runtime] <M6E.app or helper binary>

Static checks: a regular executable, arm64 only, minimum macOS 11.0, linked
only to /usr/lib and /System/Library, a valid ad-hoc signature, the hardened
runtime flag, and no entitlements (the helper needs none: it loads no code, has
no JIT and links only system libraries, and the app is not sandboxed).
Dynamic checks, with a scratch profile folder: the first output line is the
`ready` event; it opens no socket at idle; it exits with status 0 within 2
seconds of `shutdown` and within 2 seconds of the end of standard input.
Nothing here adds a torrent or touches a network. The exit status is 1 if any
check fails. acceptance.py imports check_helper().
"""

import contextlib
import json
import os
import re
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

HELPER_NAME = "m6e-torrent"
MINOS = "11.0"
EXIT_SECONDS = 2.0


def run(args, timeout=30):
    proc = subprocess.run(
        args, capture_output=True, text=True, timeout=timeout, check=False
    )
    return proc.returncode, proc.stdout, proc.stderr


def helper_path(target):
    if os.path.isdir(target):
        return os.path.join(target, "Contents", "MacOS", HELPER_NAME)
    return target


def minos_of(path):
    _, out, _ = run(["otool", "-l", path])
    match = re.search(
        r"cmd LC_BUILD_VERSION.*?minos (\S+)|cmd LC_VERSION_MIN_MACOSX.*?version (\S+)",
        out,
        re.S,
    )
    return (match.group(1) or match.group(2)) if match else None


def listening(pid):
    code, out, _ = run(["lsof", "-nP", "-iTCP", "-iUDP", "-a", "-p", str(pid)])
    return [line for line in out.splitlines()[1:] if line.strip()]


class Session:
    def __init__(self, path, profile):
        self.proc = subprocess.Popen(
            [path, "--protocol", "1", "--profile-dir", profile],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )

    def line(self, timeout):
        selector = selectors.DefaultSelector()
        selector.register(self.proc.stdout, selectors.EVENT_READ)
        buffered = b""
        deadline = time.monotonic() + timeout
        while b"\n" not in buffered:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not selector.select(remaining):
                return None
            chunk = os.read(self.proc.stdout.fileno(), 65536)
            if not chunk:
                break
            buffered += chunk
        return buffered.split(b"\n", 1)[0]

    def stop(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()
        with contextlib.suppress(OSError):
            self.proc.stdin.close()
        self.proc.stdout.close()


def dynamic_checks(path, add):
    scratch = tempfile.mkdtemp(prefix="m6e-helper-check-")
    try:
        session = Session(path, os.path.join(scratch, "torrents"))
        try:
            first = session.line(15)
            try:
                ready = json.loads(first)
            except (TypeError, ValueError):
                ready = {}
            add(
                "first output line is the ready event",
                ready.get("event") == "ready"
                and ready.get("protocol") == 1
                and ready.get("pid") == session.proc.pid,
                (first or b"no output")[:200].decode("utf-8", "replace"),
            )
            sockets = listening(session.proc.pid)
            add(
                "no socket is open at idle",
                not sockets,
                sockets or "none",
            )
            started = time.monotonic()
            with contextlib.suppress(OSError):
                session.proc.stdin.close()
            try:
                status = session.proc.wait(EXIT_SECONDS)
            except subprocess.TimeoutExpired:
                status = None
            took = time.monotonic() - started
            add(
                f"exits within {EXIT_SECONDS:g} s of the end of standard input",
                status == 0,
                f"status {status}, {took:.2f} s",
            )
        finally:
            session.stop()

        session = Session(path, os.path.join(scratch, "torrents"))
        try:
            session.line(15)
            with contextlib.suppress(OSError):
                session.proc.stdin.write(b'{"id":1,"op":"shutdown","args":{}}\n')
                session.proc.stdin.flush()
            started = time.monotonic()
            reply = session.line(EXIT_SECONDS)
            try:
                ok = json.loads(reply).get("ok") is True
            except (TypeError, ValueError):
                ok = False
            try:
                status = session.proc.wait(max(0.1, EXIT_SECONDS - (time.monotonic() - started)))
            except subprocess.TimeoutExpired:
                status = None
            took = time.monotonic() - started
            add(
                f"replies to shutdown and exits within {EXIT_SECONDS:g} s",
                ok and status == 0,
                f"reply ok={ok}, status {status}, {took:.2f} s",
            )
        finally:
            session.stop()
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def check_helper(target, runtime=True, dynamic=True):
    """Return a list of (name, ok, detail)."""
    items = []

    def add(name, ok, detail=""):
        items.append((name, bool(ok), str(detail)))

    path = helper_path(target)
    exists = os.path.isfile(path) and not os.path.islink(path)
    add("the helper is a regular file", exists, path)
    add("the helper is executable", exists and os.access(path, os.X_OK))
    if not exists:
        return items

    code, out, err = run(["lipo", "-archs", path])
    add("arm64 only", code == 0 and out.split() == ["arm64"], out.strip() or err.strip())
    minos = minos_of(path)
    add(f"minimum macOS is {MINOS}", minos == MINOS, minos)
    _, out, _ = run(["otool", "-L", path])
    foreign = [
        line.split()[0]
        for line in out.splitlines()[1:]
        if line.strip()
        and not line.split()[0].startswith(("/usr/lib/", "/System/Library/"))
    ]
    add("links only to /usr/lib and /System/Library", not foreign, foreign or "yes")

    code, _, err = run(["codesign", "--verify", "--strict", "--verbose=2", path])
    add("codesign verifies", code == 0, (err.strip().splitlines() or [""])[-1])
    _, _, info = run(["codesign", "-dv", path])
    flags = re.search(r"flags=0x[0-9a-f]+\((.*?)\)", info)
    flags = flags.group(1) if flags else ""
    add("signed ad hoc", "Signature=adhoc" in info, flags or "no signature")
    if runtime:
        add("hardened runtime flag", "runtime" in flags.split(","), flags)
    _, ents, _ = run(["codesign", "-d", "--entitlements", "-", "--xml", path])
    keys = re.findall(r"<key>(.*?)</key>", ents)
    add("no entitlements", not keys, keys or "none")

    if dynamic:
        dynamic_checks(path, add)
    return items


def main():
    args = sys.argv[1:]
    runtime = True
    if args[:1] == ["--no-runtime"]:
        runtime = False
        args = args[1:]
    if len(args) != 1:
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    failed = 0
    for name, ok, detail in check_helper(args[0], runtime):
        print(f"  {'PASS' if ok else 'FAIL'}: {name}" + (f" ({detail})" if detail else ""))
        failed += not ok
    print(f"{'Helper checks failed' if failed else 'Helper checks passed'}: {args[0]}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
