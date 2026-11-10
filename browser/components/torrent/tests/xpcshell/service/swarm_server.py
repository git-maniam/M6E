#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""A synthetic loopback swarm for the service tests (spec 8.9, D38): a tracker
and a seed on 127.0.0.1 that serve one generated fixture from
tools/m6e/torrent/fixtures. Nothing else is contacted and no real torrent,
magnet link or tracker appears here.

    swarm_server.py --fixtures-dir DIR --out DIR --name NAME [--kind multi|single]
                    [--stall] [--hostile]

With --stall the seed holds every block request back until "serve", without
dropping it, so a download stays active for as long as a test needs.

Writes the .torrent file into --out and prints one JSON line with the tracker,
the seed, the tid, the magnet link and the SHA-256 of every payload file
(base64). With --hostile it also writes the hostile and malformed .torrent
files and lists them under "hostile". Standard input takes one command per
line, each answered by one JSON line: "stats" (blocks requested and served),
"serve" (the seed answers the blocks it holds back and all later ones),
"stall" (it holds blocks back again). The
process exits when standard input ends.
"""

import argparse
import base64
import hashlib
import json
import os
import sys
import threading
import types

parser = argparse.ArgumentParser()
parser.add_argument("--fixtures-dir", required=True)
parser.add_argument("--out", required=True)
parser.add_argument("--name", default="m6e-test")
parser.add_argument("--kind", default="multi")
parser.add_argument("--stall", action="store_true")
parser.add_argument("--hostile", action="store_true")
args = parser.parse_args()

sys.path.insert(0, args.fixtures_dir)
import make_torrent  # noqa: E402
import swarm  # noqa: E402

gate = threading.Event()


class GateEvent:
    """Stands in for threading.Event inside swarm.Seed, whose only use of it is
    to delay a block: a delayed block waits here until "serve" arrives."""

    def wait(self, timeout=None):
        gate.wait()


shim = types.ModuleType("threading")
shim.__dict__.update(vars(threading))
shim.Event = GateEvent
swarm.threading = shim

HOSTILE_KINDS = [
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
MALFORMED_KINDS = ["truncated", "garbage", "empty", "no-info", "bad-pieces"]


def say(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def digest(data):
    return base64.b64encode(hashlib.sha256(data).digest()).decode()


def write_file(name, data):
    path = os.path.join(args.out, name)
    with open(path, "wb") as f:
        f.write(data)
    return path


os.makedirs(args.out, exist_ok=True)
tracker = swarm.Tracker()
if args.kind == "single":
    fixture = make_torrent.single_file(tracker.url, name=args.name)
else:
    fixture = make_torrent.multi_file(tracker.url, name=args.name)
seed = swarm.Seed([fixture], block_delay=1 if args.stall else 0)
if not args.stall:
    gate.set()
tracker.peers.append(("127.0.0.1", seed.port))

torrent_path = os.path.join(args.out, args.name + ".torrent")
fixture.write(torrent_path)

hostile = {}
if args.hostile:
    for kind in HOSTILE_KINDS:
        name = "hostile-" + kind
        hostile[name] = write_file(name + ".torrent", make_torrent.hostile(kind))
    for kind in MALFORMED_KINDS:
        name = "malformed-" + kind
        hostile[name] = write_file(name + ".torrent", make_torrent.malformed(kind))
    hostile["too-deep"] = write_file("too-deep.torrent", make_torrent.too_deep(100))
    oversized = os.path.join(args.out, "oversized.torrent")
    make_torrent.oversized(oversized)
    hostile["oversized"] = oversized

files = []
for rel, data in fixture.files:
    files.append(
        {
            "path": list(rel) if rel else [fixture.name],
            "length": len(data),
            "sha256": digest(data),
        }
    )

say(
    {
        "name": fixture.name,
        "tid": fixture.tid,
        "torrent": torrent_path,
        "tracker": tracker.url,
        "seed": seed.address,
        "magnet": fixture.magnet(trackers=[tracker.url], peers=[seed.address]),
        "files": files,
        "multi": args.kind != "single",
        "hostile": hostile,
    }
)

for line in sys.stdin:
    command = line.strip()
    if command == "stats":
        say({"requests": seed.request_count(), "served": seed.bytes_served})
    elif command == "serve":
        seed.block_delay = 0
        gate.set()
        say({"serving": True})
    elif command == "stall":
        seed.block_delay = 1
        gate.clear()
        say({"serving": False})
    elif command:
        say({"error": "unknown command"})

seed.close()
tracker.close()
