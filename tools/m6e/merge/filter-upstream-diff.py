# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Drop the parts of an upstream diff that M6E deleted, before applying it.

    python3 tools/m6e/merge/filter-upstream-diff.py upstream.diff filtered.diff report-dir

upstream.diff is `git diff --binary --no-renames <base> <new>` in the shallow
upstream clone, where <base> is the commit in upstream-base.txt. Each file
diff is judged against the M6E tree as it is before the merge (--tree,
default HEAD; read with git ls-tree, nothing is checked out), so run it
before the merge is applied, or pass the commit from before it. A file diff
is left out when

- deleted_by_m6e: the file is not in the M6E tree (modified or deleted
  upstream, but M6E deleted it, so the deletion stays);
- new_in_removed_dir: upstream adds the file, and the nearest ancestor
  directory that existed upstream at <base> is gone from the M6E tree.

Writes the kept diff, byte for byte as in the input, to the second argument,
and into the report directory skipped-deleted_by_m6e.txt,
skipped-new_in_removed_dir.txt, kept-files.txt (status and path) and
warnings.txt (files that upstream adds and the tree already has: --tree is
then not the tree from before the merge). --upstream is the clone that has
<base>, whose directories decide what counts as removed. Prints the counts.
Exit status 1 if there are warnings; the files are written all the same.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HEADER = re.compile(rb"^diff --git ", re.M)
ESCAPES = {b"a": 7, b"b": 8, b"f": 12, b"n": 10, b"r": 13, b"t": 9, b"v": 11}


def git(args, cwd):
    return subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, check=True
    ).stdout


def default_base():
    path = os.path.join(HERE, "upstream-base.txt")
    with open(path) as fh:
        lines = [l.strip() for l in fh if l.strip() and not l.startswith("#")]
    return lines[0]


def unquote(name):
    """Undo git's C-style quoting of a path."""
    if not (name.startswith(b'"') and name.endswith(b'"')):
        return name
    body, out, i = name[1:-1], bytearray(), 0
    while i < len(body):
        c = body[i : i + 1]
        if c != b"\\":
            out += c
            i += 1
        elif body[i + 1 : i + 2] in ESCAPES:
            out.append(ESCAPES[body[i + 1 : i + 2]])
            i += 2
        elif re.match(rb"[0-7]{3}", body[i + 1 : i + 4]):
            out.append(int(body[i + 1 : i + 4], 8))
            i += 4
        else:
            out += body[i + 1 : i + 2]
            i += 2
    return bytes(out)


def split_diff(data):
    starts = [m.start() for m in HEADER.finditer(data)]
    if not starts or starts[0] != 0:
        sys.exit("input does not start with 'diff --git'")
    return [data[a:b] for a, b in zip(starts, starts[1:] + [len(data)])]


def describe(chunk):
    """Returns (status, path): A, D or M, and the path judged against the tree.

    With --no-renames both names are the same. A rename is judged by its
    source, since that is the file the merge would have to change.
    """
    lines = chunk.split(b"\n", 8)
    head = lines[0][len(b"diff --git ") :]
    rename = [l[len(b"rename from ") :] for l in lines if l.startswith(b"rename from ")]
    path = None
    if rename:
        path = unquote(rename[0])
    elif head.startswith(b'"'):
        first = re.match(rb'"(?:[^"\\]|\\.)*"', head)
        path = unquote(first.group(0))[2:] if first else None
    else:
        for m in re.finditer(rb" b/", head):
            left, right = head[2 : m.start()], head[m.end() :]
            if head.startswith(b"a/") and left == right:
                path = left
                break
    if path is None:
        sys.exit(f"cannot read the file name in: {head.decode(errors='replace')}")
    status = "M"
    for l in lines[1:]:
        if l.startswith(b"new file mode "):
            status = "A"
        elif l.startswith(b"deleted file mode "):
            status = "D"
    return status, path.decode("utf-8", "surrogateescape")


def tree_paths(repo, rev):
    out = git(["ls-tree", "-r", "-z", "--name-only", rev], repo).decode(
        "utf-8", "surrogateescape"
    )
    return set(out.split("\0")) - {""}


def tree_dirs(repo, rev):
    out = git(["ls-tree", "-r", "-d", "-z", "--name-only", rev], repo).decode(
        "utf-8", "surrogateescape"
    )
    return set(out.split("\0")) - {""}


def dirs_of(paths):
    dirs = set()
    for p in paths:
        d = os.path.dirname(p)
        while d and d not in dirs:
            dirs.add(d)
            d = os.path.dirname(d)
    return dirs


def nearest_dir(path, existing):
    d = os.path.dirname(path)
    while d and d not in existing:
        d = os.path.dirname(d)
    return d


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("diff", help="upstream diff (git diff --binary --no-renames)")
    parser.add_argument("output", help="where to write the filtered diff")
    parser.add_argument("report", help="directory for the skipped-file lists")
    parser.add_argument("--tree", default="HEAD", help="M6E tree before the merge")
    parser.add_argument(
        "--upstream",
        default=os.path.join(TOP, "..", "m6e-upstream.git"),
        help="upstream clone that has the base commit",
    )
    parser.add_argument(
        "--base",
        help="upstream commit the diff starts from (default: upstream-base.txt)",
    )
    args = parser.parse_args()

    base = args.base or default_base()
    with open(args.diff, "rb") as fh:
        chunks = split_diff(fh.read())

    have = tree_paths(TOP, args.tree)
    have_dirs = dirs_of(have)
    upstream_dirs = tree_dirs(args.upstream, base)

    kept, deleted, removed_dir, warnings = [], [], [], []
    for chunk in chunks:
        status, path = describe(chunk)
        if status == "A":
            top = nearest_dir(path, upstream_dirs)
            if path in have:
                warnings.append(f"{path}: added upstream but already in the M6E tree")
            elif top and top not in have_dirs:
                removed_dir.append(path)
                continue
        elif path not in have:
            deleted.append(path)
            continue
        kept.append((status, path, chunk))

    os.makedirs(args.report, exist_ok=True)
    with open(args.output, "wb") as fh:
        fh.writelines(c for _, _, c in kept)
    for name, paths in (
        ("skipped-deleted_by_m6e.txt", deleted),
        ("skipped-new_in_removed_dir.txt", removed_dir),
    ):
        with open(os.path.join(args.report, name), "w") as fh:
            fh.write("".join(f"{p}\n" for p in paths))
    with open(os.path.join(args.report, "kept-files.txt"), "w") as fh:
        fh.write("".join(f"{s}\t{p}\n" for s, p, _ in kept))
    with open(os.path.join(args.report, "warnings.txt"), "w") as fh:
        fh.write("".join(f"{w}\n" for w in warnings))

    by_status = {s: sum(1 for k in kept if k[0] == s) for s in "AMD"}
    print(f"{len(chunks)} file diffs in {args.diff}")
    print(
        f"{len(kept)} kept ({by_status['A']} added, {by_status['M']} modified, "
        f"{by_status['D']} deleted) -> {args.output}"
    )
    print(f"{len(deleted)} left out: the file is gone from the M6E tree")
    print(f"{len(removed_dir)} left out: new file in a directory M6E removed")
    for w in warnings[:10]:
        print(f"warning: {w}", file=sys.stderr)
    if warnings:
        print(
            f"{len(warnings)} warnings (all in {args.report}/warnings.txt): is --tree "
            "the tree from before the merge?",
            file=sys.stderr,
        )
    return 1 if warnings else 0


if __name__ == "__main__":
    sys.exit(main())
