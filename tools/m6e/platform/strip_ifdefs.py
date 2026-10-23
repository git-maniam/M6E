# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Strip Windows, Linux, Android and other non-macOS preprocessor branches (D21).

Runs /usr/bin/unifdef over tracked first-party C, C++ and Objective-C files,
skipping vendored code (directories with a moz.yaml, third_party/ and a few
unmarked imports), whose #ifdefs stay so that security updates still apply.

    python3 tools/m6e/platform/strip_ifdefs.py            # dry run: counts
    python3 tools/m6e/platform/strip_ifdefs.py --apply    # rewrite files
    python3 tools/m6e/platform/strip_ifdefs.py --list     # files that change
"""

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

EXTENSIONS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm", ".inc")

UNDEFINED = [
    "XP_WIN", "XP_WIN32", "XP_WIN64", "_WIN32", "_WIN64", "WIN32", "_WINDOWS",
    "__MINGW32__", "__MINGW64__", "__CYGWIN__", "_MSC_VER",
    "MOZ_WIDGET_GTK", "MOZ_WAYLAND", "MOZ_X11", "MOZ_ENABLE_DBUS",
    "XP_LINUX", "__linux__", "__linux", "__gnu_linux__", "__GLIBC__",
    "MOZ_WIDGET_ANDROID", "ANDROID", "__ANDROID__",
    "XP_IOS", "MOZ_WIDGET_UIKIT",
    "XP_FREEBSD", "XP_OPENBSD", "XP_NETBSD", "XP_DRAGONFLY", "XP_SOLARIS",
    "__FreeBSD__", "__OpenBSD__", "__NetBSD__", "__DragonFly__", "__sun",
    "OS_WIN", "OS_LINUX", "OS_ANDROID", "OS_BSD", "OS_FREEBSD", "OS_NETBSD",
    "OS_OPENBSD", "OS_DRAGONFLY", "OS_SOLARIS",
]

DEFINED = [
    "XP_MACOSX", "XP_DARWIN", "XP_UNIX", "__APPLE__", "__MACH__",
    "MOZ_WIDGET_COCOA", "OS_MACOSX", "OS_POSIX",
]

UNMARKED_VENDORED = ("third_party/", "intl/icu/", "gfx/skia/", "media/ffvpx/")


def tracked_files():
    out = subprocess.run(
        ["git", "ls-files", "-z"], capture_output=True, check=True
    ).stdout.decode()
    return [f for f in out.split("\0") if f]


def vendored_dirs(files):
    dirs = {os.path.dirname(f) + "/" for f in files if f.endswith("/moz.yaml")}
    return tuple(sorted(dirs)) + UNMARKED_VENDORED


def candidates(files):
    vendored = vendored_dirs(files)
    return [
        f
        for f in files
        if f.endswith(EXTENSIONS) and not f.startswith(vendored)
        and not os.path.islink(f)
    ]


def unifdef_args():
    return (
        ["/usr/bin/unifdef"]
        + [f"-U{s}" for s in UNDEFINED]
        + [f"-D{s}" for s in DEFINED]
    )


def process(path, apply):
    """Returns (path, status): status is "changed", "unchanged" or an error.

    unifdef writes to stdout and the file is only rewritten when it succeeds,
    since unifdef can leave truncated output on syntax it can't parse (for
    example C++ raw string literals).
    """
    r = subprocess.run(unifdef_args() + [path], capture_output=True)
    if r.returncode == 0:
        return path, "unchanged"
    if r.returncode != 1:
        return path, "error: " + r.stderr.decode(errors="replace").strip()[:200]
    if apply:
        mode = os.stat(path).st_mode
        with open(path, "wb") as fh:
            fh.write(r.stdout)
        os.chmod(path, mode)
    return path, "changed"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    files = candidates(tracked_files())
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        results = list(pool.map(lambda f: process(f, args.apply), files))

    changed = [p for p, s in results if s == "changed"]
    errors = [(p, s) for p, s in results if s.startswith("error")]
    if args.list:
        print("\n".join(changed))
    for p, s in errors:
        print(f"{p}: {s}", file=sys.stderr)
    print(
        f"{len(files)} candidate files, {len(changed)} "
        f"{'changed' if args.apply else 'would change'}, {len(errors)} errors",
        file=sys.stderr,
    )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
