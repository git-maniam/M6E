#!/usr/bin/env python3
"""Checks the helper's `renamed` and `nameRenamed` metadata flags.

    python3 -I tools/m6e/torrent/tests/risk/renamed_test.py \
        [--helper tools/m6e/torrent/build/m6e-torrent]

libtorrent silently drops direction and zero-width characters from names
and replaces C1 controls, so the Shady Torrent check (spec 8.3) learns about
them only through these flags. Every torrent here is synthetic metadata built
in memory with no tracker; it is only added (pending, never confirmed), so
nothing is downloaded and nothing is contacted (spec 8.9, D38). TAP output;
the exit status is 1 when a check fails.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TORRENT_DIR = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(TORRENT_DIR, "fixtures"))

import bencode  # noqa: E402

PIECE = 16384


def v1(name, files=None, length=None, utf8_paths=None):
    """A v1 info dictionary; files are (path list, length, pad).

    The expected flags of a case are for the files that are not padding.
    """
    total = length if files is None else sum(size for _, size, _ in files)
    info = {
        "name": name,
        "piece length": PIECE,
        "pieces": b"\0" * 20 * max(1, -(-total // PIECE)),
    }
    if files is None:
        info["length"] = length
        return info
    entries = []
    for index, (path, size, pad) in enumerate(files):
        entry = {"length": size, "path": path}
        if pad:
            entry["attr"] = "p"
        if utf8_paths and index in utf8_paths:
            entry["path.utf-8"] = utf8_paths[index]
        entries.append(entry)
    info["files"] = entries
    return info


def v2(name, tree):
    """A v2 info dictionary; tree maps names to lengths or to subtrees."""

    def build(node):
        out = {}
        for key, value in node.items():
            if isinstance(value, dict):
                out[key] = build(value)
            else:
                data = b"\0" * value
                out[key] = {
                    "": {
                        "length": value,
                        "pieces root": hashlib.sha256(data).digest(),
                    }
                }
        return out

    return {
        "name": name,
        "piece length": PIECE,
        "meta version": 2,
        "file tree": build(tree),
    }


CASES = [
    (
        "v1 folder: dropped and replaced characters, joiners, padding",
        v1(
            "Photos",
            [
                (["a.jpg"], 100, False),
                (["Photo\u202egpj.exe"], 100, False),
                ([".pad", str(PIECE - 200)], PIECE - 200, True),
                (["dir\u200b", "b.txt"], 100, False),
                (["\u0645\u06cc\u200c\u062e\u0648\u0627\u0647\u0645.mp3"], 100, False),
                (["family \U0001f468\u200d\U0001f469.jpg"], 100, False),
                (["x\u0085y.txt"], 100, False),
                (["c\u00a0d.txt"], 100, False),
            ],
        ),
        False,
        [False, True, True, False, False, True, False],
    ),
    (
        "v1 folder whose name has a direction override",
        v1("Holiday\u202egpj", [(["a.jpg"], 100, False), (["b.jpg"], 100, False)]),
        True,
        [False, False],
    ),
    (
        "v1 single file with an isolate in its name",
        v1("Clip\u2066.mkv", length=1000),
        True,
        [True],
    ),
    (
        "v1 path.utf-8 is what libtorrent uses",
        v1(
            "Docs",
            [(["plain.txt"], 100, False), (["other.txt"], 100, False)],
            utf8_paths={0: ["pl\u202eain.txt"]},
        ),
        False,
        [True, False],
    ),
    (
        "v2 single file with a direction override",
        v2("Movie\u202evkm.exe", {"Movie\u202evkm.exe": 1000}),
        True,
        [True],
    ),
    (
        "v2 folder with a marked subfolder",
        v2("Album", {"01.mp3": 1000, "Bonus\u200f": {"02.mp3": 1000}}),
        False,
        [False, True],
    ),
    (
        "an ordinary torrent has no flags",
        v1("Plain", [(["a", "b.txt"], 100, False), (["c.txt"], 100, False)]),
        False,
        [False, False],
    ),
]


class Helper:
    def __init__(self, cmd, profile):
        self.proc = subprocess.Popen(
            cmd + ["--protocol", "1", "--profile-dir", profile],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.next_id = 1
        ready = self.read()
        if ready.get("event") != "ready":
            raise AssertionError("the first line is not ready: %r" % ready)

    def read(self):
        line = self.proc.stdout.readline()
        if not line:
            raise AssertionError("the helper ended")
        return json.loads(line)

    def call(self, op, args):
        rid = self.next_id
        self.next_id += 1
        message = {"id": rid, "op": op, "args": args}
        self.proc.stdin.write((json.dumps(message) + "\n").encode())
        self.proc.stdin.flush()
        while True:
            obj = self.read()
            if obj.get("id") == rid:
                return obj

    def close(self):
        try:
            self.call("shutdown", {})
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def check(helper, folder, number, info):
    path = os.path.join(folder, "case%d.torrent" % number)
    with open(path, "wb") as f:
        f.write(bencode.encode({"info": info, "created by": "m6e-test"}))
    reply = helper.call("add", {"file": path})
    if not reply.get("ok"):
        raise AssertionError("add failed: %r" % reply.get("error"))
    metadata = reply["result"]["metadata"]
    helper.call("cancel", {"tid": reply["result"]["tid"]})
    return metadata


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--helper", default=os.path.join(TORRENT_DIR, "build", "m6e-torrent")
    )
    options = parser.parse_args()
    root = tempfile.mkdtemp(prefix="m6e-renamed-")
    failures = 0
    print("1..%d" % len(CASES))
    try:
        helper = Helper([os.path.abspath(options.helper)], os.path.join(root, "p"))
        try:
            for number, (title, info, name_flag, flags) in enumerate(CASES, 1):
                problems = []
                try:
                    md = check(helper, root, number, info)
                    if md.get("nameRenamed", False) is not name_flag:
                        problems.append("nameRenamed is %r" % md.get("nameRenamed"))
                    data = [f for f in md["files"] if not f.get("pad")]
                    pads = [f for f in md["files"] if f.get("pad")]
                    if len(data) != len(flags):
                        problems.append("%d files" % len(data))
                    for file in pads:
                        if "renamed" in file:
                            problems.append("padding %r is marked" % file["path"])
                    for file, flag in zip(data, flags):
                        if file.get("renamed", False) is not flag:
                            problems.append(
                                "%r renamed %r" % (file["path"], file.get("renamed"))
                            )
                except AssertionError as e:
                    problems.append(str(e))
                status = "not ok" if problems else "ok"
                print("%s %d - %s" % (status, number, title))
                for problem in problems:
                    print("#   " + problem)
                failures += bool(problems)
        finally:
            helper.close()
    finally:
        shutil.rmtree(root, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
