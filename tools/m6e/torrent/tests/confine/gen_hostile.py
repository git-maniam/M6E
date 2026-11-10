"""Writes synthetic hostile and valid .torrent files into a folder (argv[1]).

Everything is made up here: names, sizes and zero-filled hashes. No real
torrent, tracker or info hash is used or contacted (spec 8.9, D38).
"""

import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "fixtures"))
import bencode  # noqa: E402

PIECES = hashlib.sha1(b"synthetic").digest()


def v1(name, files, **extra):
    info = {"name": name, "piece length": 16384, "pieces": PIECES}
    info["files"] = files
    info.update(extra)
    return info


def f(path, length=10, **extra):
    entry = {"length": length, "path": path}
    entry.update(extra)
    return entry


def v2(name, tree, **extra):
    info = {
        "name": name,
        "piece length": 16384,
        "meta version": 2,
        "file tree": tree,
    }
    info.update(extra)
    return info


def leaf(**extra):
    node = {"length": 10, "pieces root": b"\x00" * 32}
    node.update(extra)
    return {"": node}


CASES = {
    "reject-traversal": v1("t", [f(["..", "escape.txt"])]),
    "reject-traversal-mid": v1("t", [f(["ok", "..", "escape.txt"])]),
    "reject-absolute": v1("t", [f(["/etc", "passwd"])]),
    "reject-empty-component": v1("t", [f(["a", "", "b"])]),
    "reject-dot": v1("t", [f([".", "b"])]),
    "reject-nul": v1("t", [f([b"a\x00b"])]),
    "reject-backslash": v1("t", [f(["a\\b"])]),
    "reject-colon": v1("t", [f(["C:", "b"])]),
    "reject-name-dotdot": v1("..", [f(["a"])]),
    "reject-name-slash": v1("x/y", [f(["a"])]),
    "reject-name-nul": v1(b"x\x00y", [f(["a"])]),
    "reject-name-empty": v1("", [f(["a"])]),
    "reject-name-utf8-dotdot": v1("fine", [f(["a"])], **{"name.utf-8": ".."}),
    "reject-path-utf8-traversal": v1(
        "t", [f(["a"], **{"path.utf-8": ["..", "x"]})]
    ),
    "reject-symlink-attr": v1(
        "t", [f(["link"], attr="l", **{"symlink path": ["..", "..", "etc"]})]
    ),
    "reject-symlink-attr-only": v1("t", [f(["link"], attr="l")]),
    "reject-symlink-v2": v2("t", {"link": leaf(attr="l", **{"symlink path": ["..", "x"]})}),
    "reject-symlink-single": {
        "name": "t",
        "piece length": 16384,
        "pieces": PIECES,
        "length": 1,
        "attr": "l",
    },
    "reject-case-collision": v1("t", [f(["Readme.txt"]), f(["README.TXT"])]),
    "reject-case-collision-dirs": v1("t", [f(["Dir", "a"]), f(["dir", "b"])]),
    "reject-nfc-nfd-collision": v1(
        "t", [f(["é.txt"]), f(["é.txt"])]
    ),
    "reject-duplicate-path": v1("t", [f(["a"]), f(["a"])]),
    "reject-file-and-dir": v1("t", [f(["a"]), f(["a", "b"])]),
    "reject-overlong-name": v1("t", [f(["n" * 256])]),
    "reject-overlong-torrent-name": v1("n" * 300, [f(["a"])]),
    "reject-overlong-total": v1("t", [f(["w" * 200] * 6)]),
    "reject-too-deep": v1("t", [f(["d"] * 70)]),
    "reject-control-char": v1("t", [f(["line\nbreak"])]),
    "reject-invalid-utf8": v1("t", [f([b"bad\xff"])]),
    "reject-fake-pad": v1("t", [f(["a"]), f(["movie.mkv"], attr="p")]),
    "reject-v2-traversal": v2("t", {"..": {"x": leaf()}}),
    "reject-v2-case-collision": v2("t", {"A": leaf(), "a": leaf()}),
    "reject-v2-symlink-mid": v2("t", {"d": {"s": leaf(attr="l")}}),
    "reject-utf8-name-collision": v1(
        "t", [f(["a"], **{"path.utf-8": ["B"]}), f(["b"], **{"path.utf-8": ["b"]})]
    ),
    "accept-plain": v1("t", [f(["a.txt"]), f(["sub", "b.bin"])]),
    "accept-unicode": v1(
        "日本", [f(["é.txt"]), f(["\U0001f600.txt"]), f(["sub", "näme"])]
    ),
    "accept-padding": v1(
        "t", [f(["a.bin"], length=16000), f([".pad", "384"], length=384, attr="p")]
    ),
    "accept-v2-tree": v2("t", {"dir": {"a.txt": leaf(), "b.txt": leaf()}}),
    "accept-long-name": v1("t", [f(["n" * 255])]),
    "accept-executable-attr": v1("t", [f(["tool.sh"], attr="x")]),
}


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for label, info in CASES.items():
        with open(os.path.join(out, label + ".torrent"), "wb") as fh:
            fh.write(bencode.encode({"info": info, "created by": "m6e-test"}))


if __name__ == "__main__":
    main()
