# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check that two source trees generate the same macOS build backend.

Used after the P4.6 build-file rewrites to show they don't change what the
macOS build compiles. Make the baseline with `git worktree add --detach
<dir> <commit>` and `./mach configure` in it, then:

    python3 tools/m6e/platform/compare_backends.py <base-tree> <this-tree>

Compares every backend.mk, Makefile, unified source and install manifest in
the two obj-m6e directories, after replacing the base tree's path, and
lists the files that differ.
"""

import glob
import os
import re
import sys

PATTERNS = [
    "**/backend.mk",
    "**/Makefile",
    "**/Unified_*.c",
    "**/Unified_*.cpp",
    "**/Unified_*.mm",
    "_build_manifests/install/*",
]
VIRTUALENV = re.compile(r"/\.mozbuild/srcdirs/[^/]+/_virtualenvs/")


def backend_files(objdir):
    out = {}
    for pattern in PATTERNS:
        for f in glob.iglob(os.path.join(objdir, pattern), recursive=True):
            if "/instrumented/" not in f:
                out[os.path.relpath(f, objdir)] = f
    return out


def normalised(path, tree):
    with open(path, errors="replace") as fh:
        text = fh.read().replace(tree, "@TREE@")
    return VIRTUALENV.sub("/.mozbuild/srcdirs/@VENV@/_virtualenvs/", text)


def main(base, this):
    base, this = os.path.abspath(base), os.path.abspath(this)
    a = backend_files(os.path.join(base, "obj-m6e"))
    b = backend_files(os.path.join(this, "obj-m6e"))
    differ = sorted(set(a) ^ set(b))
    differ += sorted(
        k for k in set(a) & set(b) if normalised(a[k], base) != normalised(b[k], this)
    )
    for k in differ:
        print(k)
    print(f"{len(set(a) | set(b))} backend files, {len(differ)} differ", file=sys.stderr)
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
