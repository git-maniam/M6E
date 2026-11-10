#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""A fake m6e-torrent helper for the service tests: it speaks the protocol of
tools/m6e/torrent/PROTOCOL.md without any engine.

It is steered through files in its --profile-dir:
  fake.json      settings, read again for every request: startFail, noReady,
                 crashOn (op), hangOn (op), rejectConfig, unsupported (keys),
                 magnetMetadata (default true)
  emit.jsonl     lines to write to standard output as they are; a line
                 {"fake": "exit", "code": N} exits at once,
                 {"fake": "complete", "tid": T} finishes a torrent,
                 {"fake": "state", "tid": T, "state": S} changes its state
It writes requests.jsonl (every request received) and starts.jsonl (one line
per start).
"""

import hashlib
import json
import os
import re
import select
import sys
import time

args = sys.argv[1:]
profile_dir = args[args.index("--profile-dir") + 1]
os.makedirs(profile_dir, exist_ok=True)


def path(name):
    return os.path.join(profile_dir, name)


def append(name, obj):
    with open(path(name), "a") as f:
        f.write(json.dumps(obj) + "\n")


def settings():
    try:
        with open(path("fake.json")) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def out(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def bdecode(data):
    pos = 0

    def value():
        nonlocal pos
        c = data[pos : pos + 1]
        if c == b"i":
            end = data.index(b"e", pos)
            n = int(data[pos + 1 : end])
            pos = end + 1
            return n
        if c in (b"l", b"d"):
            pos += 1
            items = []
            while data[pos : pos + 1] != b"e":
                items.append(value())
            pos += 1
            if c == b"l":
                return items
            return {items[i]: items[i + 1] for i in range(0, len(items), 2)}
        colon = data.index(b":", pos)
        length = int(data[pos:colon])
        pos = colon + 1 + length
        return data[colon + 1 : pos]

    return value()


def metadata_from_file(file_path):
    with open(file_path, "rb") as f:
        data = f.read()
    root = bdecode(data)
    info = root[b"info"]
    name = info[b"name"].decode()
    if b"files" in info:
        files = [
            {
                "index": i,
                "path": "/".join(p.decode() for p in entry[b"path"]),
                "length": entry[b"length"],
            }
            for i, entry in enumerate(info[b"files"])
        ]
    else:
        files = [{"index": 0, "path": name, "length": info[b"length"]}]
    tid = hashlib.sha1(data).hexdigest()
    return {
        "infoHash": tid,
        "name": name,
        "totalSize": sum(f["length"] for f in files),
        "pieceLength": 16384,
        "files": files,
        "trackers": [],
        "webSeeds": [],
        "isPrivate": False,
    }


def status(tid, t):
    return {
        "tid": tid,
        "name": t["metadata"]["name"] if t.get("metadata") else tid,
        "state": t["state"],
        "progress": t.get("progress", 0),
        "size": t["metadata"]["totalSize"] if t.get("metadata") else 0,
        "downloaded": 0,
        "uploaded": 0,
        "downKBps": 0,
        "upKBps": 0,
        "etaSeconds": None,
        "ratio": 0,
        "seeds": 0,
        "peers": 0,
        "isPrivate": False,
        "savePath": t.get("savePath", ""),
        "sequential": t.get("sequential", False),
        "addedAt": t.get("addedAt", 0),
        "error": None,
    }


torrents = {}
pending = {}
start_time = time.time()


def save_state():
    with open(path("state.json"), "w") as f:
        json.dump(torrents, f)


def load_state():
    try:
        with open(path("state.json")) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def reply(rid, result):
    out({"id": rid, "ok": True, "result": result})


def fail(rid, code, message, **extra):
    error = {"code": code, "message": message}
    error.update(extra)
    out({"id": rid, "ok": False, "error": error})


def find(tid):
    return torrents.get(tid) or pending.get(tid)


def handle(request):
    rid = request["id"]
    op = request["op"]
    a = request.get("args", {})
    s = settings()
    append("requests.jsonl", request)
    if s.get("crashOn") == op:
        os._exit(7)
    if s.get("hangOn") == op:
        return
    if op == "add":
        if "file" in a:
            try:
                md = metadata_from_file(a["file"])
            except Exception as e:
                fail(rid, "invalid-torrent", str(e))
                return
            tid = md["infoHash"]
            if find(tid):
                fail(rid, "duplicate", "already added", tid=tid)
                return
            pending[tid] = {"state": "pending-confirm", "metadata": md}
            reply(rid, {"tid": tid, "state": "pending-confirm", "metadata": md})
        else:
            m = re.search(r"btih:([0-9a-fA-F]{40})", a["magnet"])
            tid = (
                m.group(1).lower()
                if m
                else hashlib.sha1(a["magnet"].encode()).hexdigest()
            )
            if find(tid):
                fail(rid, "duplicate", "already added", tid=tid)
                return
            pending[tid] = {"state": "fetching-metadata", "metadata": None}
            reply(rid, {"tid": tid, "state": "fetching-metadata"})
            if s.get("magnetMetadata", True):
                pending[tid]["metadataDue"] = time.time() + 0.2
    elif op == "confirm":
        t = pending.pop(a["tid"], None)
        if not t or not t["metadata"]:
            if t:
                pending[a["tid"]] = t
            fail(rid, "not-found", "no such pending torrent")
            return
        t["state"] = "downloading" if a["start"] else "paused"
        t["savePath"] = a["savePath"]
        t["sequential"] = a["sequential"]
        t["priorities"] = {str(f["index"]): f["priority"] for f in a["files"]}
        t["addedAt"] = int(time.time() * 1000)
        torrents[a["tid"]] = t
        save_state()
        reply(rid, {"status": status(a["tid"], t)})
    elif op == "cancel":
        if pending.pop(a["tid"], None) is None:
            fail(rid, "not-found", "no such pending torrent")
        else:
            reply(rid, {})
    elif op in ("pause", "resume"):
        t = torrents.get(a["tid"])
        if not t:
            fail(rid, "not-found", "no such torrent")
            return
        t["state"] = "paused" if op == "pause" else "downloading"
        save_state()
        reply(rid, {"status": status(a["tid"], t)})
    elif op in ("pauseAll", "resumeAll"):
        for t in torrents.values():
            t["state"] = "paused" if op == "pauseAll" else "downloading"
        save_state()
        reply(rid, {})
    elif op == "remove":
        if torrents.pop(a["tid"], None) is None:
            fail(rid, "not-found", "no such torrent")
            return
        save_state()
        reply(rid, {})
    elif op == "list":
        reply(rid, {"torrents": [status(k, t) for k, t in torrents.items()]})
    elif op == "detail":
        t = torrents.get(a["tid"])
        if not t:
            fail(rid, "not-found", "no such torrent")
            return
        md = t["metadata"]
        reply(
            rid,
            {
                "status": status(a["tid"], t),
                "metadata": md,
                "trackers": [],
                "peers": [],
                "pieces": {"count": 1, "have": "AA=="},
                "files": [
                    {
                        "index": f["index"],
                        "priority": t.get("priorities", {}).get(str(f["index"]), 2),
                        "progress": t.get("progress", 0),
                    }
                    for f in md["files"]
                ],
            },
        )
    elif op == "setLimits":
        reply(rid, {})
    elif op == "setConfig":
        if s.get("rejectConfig"):
            fail(rid, "bad-request", "no")
            return
        unsupported = [k for k in a if k in s.get("unsupported", [])]
        reply(rid, {"applied": [k for k in a if k not in unsupported], "unsupported": unsupported})
    elif op == "restore":
        saved = load_state()
        torrents.clear()
        for k, t in saved.items():
            if not a["start"]:
                t["state"] = "paused"
            torrents[k] = t
        reply(rid, {"torrents": [status(k, t) for k, t in torrents.items()]})
    elif op == "shutdown":
        reply(rid, {})
        sys.exit(0)
    else:
        fail(rid, "bad-request", "unknown op")


def process_emit():
    try:
        with open(path("emit.jsonl")) as f:
            lines = f.read().splitlines()
        os.remove(path("emit.jsonl"))
    except OSError:
        return
    for line in lines:
        try:
            obj = json.loads(line)
        except ValueError:
            sys.stdout.write(line + "\n")
            sys.stdout.flush()
            continue
        directive = obj.get("fake")
        if directive == "exit":
            os._exit(obj.get("code", 1))
        elif directive == "complete":
            t = torrents.get(obj["tid"])
            if t:
                t["state"] = "seeding"
                t["progress"] = 1
                save_state()
                out({"event": "completed", "tid": obj["tid"]})
        elif directive == "state":
            t = torrents.get(obj["tid"])
            if t:
                t["state"] = obj["state"]
                save_state()
                out({"event": "progress", "torrents": [status(obj["tid"], t)]})
        else:
            out(obj)


def process_due():
    for tid, t in list(pending.items()):
        due = t.get("metadataDue")
        if due and time.time() >= due:
            t.pop("metadataDue")
            t["metadata"] = {
                "infoHash": tid,
                "name": "magnet-name",
                "totalSize": 10,
                "pieceLength": 16384,
                "files": [{"index": 0, "path": "magnet-name/file.bin", "length": 10}],
                "trackers": [],
                "webSeeds": [],
                "isPrivate": False,
            }
            t["state"] = "pending-confirm"
            out({"event": "metadata", "tid": tid, "metadata": t["metadata"]})


def main():
    s = settings()
    append("starts.jsonl", {"pid": os.getpid(), "args": args})
    if s.get("startFail"):
        sys.exit(3)
    if s.get("noReady"):
        time.sleep(3600)
    out({"event": "ready", "protocol": 1, "engine": "fake 1.0", "pid": os.getpid()})
    buf = b""
    while True:
        ready, _, _ = select.select([0], [], [], 0.05)
        if ready:
            chunk = os.read(0, 65536)
            if not chunk:
                sys.exit(0)
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.strip():
                    handle(json.loads(line))
        process_emit()
        process_due()


main()
