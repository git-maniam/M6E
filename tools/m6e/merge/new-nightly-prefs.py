# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Find prefs that default differently on Nightly and Release, and that M6E
has not dealt with.

    python3 tools/m6e/merge/new-nightly-prefs.py [--since REV] [--all] [--pref NAME]

M6E is built from Nightly, which switches on experimental features that
Firefox Release keeps off (P4.8a). This evaluates modules/libpref/init/
StaticPrefList.yaml and every default-prefs file with Nightly conditions
(all.js, firefox.js and others) with Mozilla's own preprocessor, once as
this build (macOS, Nightly) and once with the channel switched to Release,
and lists the prefs whose value differs. A pref is dealt with when
browser/app/profile/m6e.js sets it, when it is in nightly-prefs-reviewed.txt
(a Nightly value that was kept on purpose), or when it only exists on
Nightly and is off there. Anything else is NEW and needs a decision: add it
to m6e.js, or to the reviewed list with a reason.

--since REV reads the same files at REV and marks the prefs that are new
or changed since then (what a merge brought in); --head REV reads the tree
at a commit instead of the working tree. --pref NAME prints how one pref
resolves on Nightly and Release, before m6e.js, and exits. It is a quick
check of the #if handling: dom.media.webcodecs.enabled is Nightly-only on
Android only, so both values must be true here. Exit status 1 if any pref is
NEW.

Limits: the defines are the objdir's ACDEFINES plus the ones that moz.build
files add for the pref files (MOZBUILD_DEFINES, MOZ_WIDEVINE_EME); any other
define that a moz.build adds for one directory is not modelled. The YAML
result was compared with the build's generated StaticPrefList headers; the
JS pref files were compared with the build's greprefs.js and firefox.js
(P5.3). The background-task prefs are left out, as they never apply to the
browser.
"""

import argparse
import os
import re
import shlex
import subprocess
import sys
from io import StringIO

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path[:0] = [
    os.path.join(TOP, "python", "mozbuild"),
    os.path.join(TOP, "third_party", "python", "pyyaml", "lib"),
]

import yaml  # noqa: E402
from mozbuild.preprocessor import Preprocessor  # noqa: E402

YAML_FILE = "modules/libpref/init/StaticPrefList.yaml"
ALL_JS = "modules/libpref/init/all.js"
FIREFOX_JS = "browser/app/profile/firefox.js"
M6E_JS = "browser/app/profile/m6e.js"
REVIEWED = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "nightly-prefs-reviewed.txt"
)
CHANNEL_CONDITION = (
    "^#(if|ifdef|ifndef|elif).*"
    "(NIGHTLY_BUILD|EARLY_BETA_OR_EARLIER|RELEASE_OR_BETA|MOZ_UPDATE_CHANNEL)"
)
IGNORED_DIRS = {"test", "tests", "testing", "third_party", "node_modules", "obj-m6e"}
# Applied only when the app runs as a background task, never to the browser.
BACKGROUND_TASK_PREFS = "toolkit/components/backgroundtasks/defaults/"
# Directories without default-prefs files, so git grep can skip them.
SKIPPED_PATHS = [
    f":(exclude){d}"
    for d in (
        "testing",
        "third_party",
        "js",
        "tools",
        "python",
        "taskcluster",
        "docs",
        "build",
        "gfx",
        "media",
        "*/test/*",
        "*/tests/*",
    )
]

# Defines that moz.build files add when they preprocess the pref files, for
# the config variables named here (modules/libpref/moz.build) and for each EME
# module (browser/moz.build: MOZ_WIDEVINE_EME). They are not in ACDEFINES.
MOZBUILD_DEFINES = (
    "MOZ_WEBEXT_WEBIDL_ENABLED",
    "MOZ_ARTIFACT_BUILDS",
)

# What M6E's macOS arm64 build defines (ACDEFINES in the objdir's
# config/autoconf.mk, plus MOZBUILD_DEFINES), for when there is no objdir to
# read. Only the names that the default-prefs files test are listed.
BUILTIN_DEFINES = {
    "XP_DARWIN": 1,
    "XP_MACOSX": 1,
    "XP_UNIX": 1,
    "MOZ_WIDGET_COCOA": 1,
    "MOZ_AARCH64": 1,
    "HAVE_64BIT_BUILD": 1,
    "NIGHTLY_BUILD": 1,
    "EARLY_BETA_OR_EARLIER": 1,
    "MOZ_UPDATE_CHANNEL": "default",
    "MOZ_BACKGROUNDTASKS": 1,
    "MOZ_SANDBOX": 1,
    "MOZ_WEBRTC": 1,
    "MOZ_WEBSPEECH": 1,
    "MOZ_JXL": 1,
    "MOZ_FONTATIONS": 1,
    "MOZ_APPLEMEDIA": 1,
    "MOZ_FFMPEG": 1,
    "MOZ_PROXY_DIRECT_FAILOVER": 1,
    "MOZ_WEBEXT_WEBIDL_ENABLED": 1,
    "MOZ_WIDEVINE_EME": 1,
    "ENABLE_JIT_SIMD": 1,
    "ENABLE_TESTS": 1,
    "ENABLE_WASM_BRANCH_HINTING": 1,
    "ENABLE_WASM_COMPACT_IMPORTS": 1,
    "ENABLE_WASM_MOZ_INTGEMM": 1,
    "ENABLE_WASM_RELAXED_SIMD": 1,
    "ENABLE_WEBDRIVER": 1,
}
ABSENT = None
PREF_CALL = re.compile(r"(?:sticky_|user_)?pref\(")


def git(args, check=True):
    r = subprocess.run(["git", *args], cwd=TOP, capture_output=True, check=False)
    if check and r.returncode:
        sys.exit(f"git {' '.join(args)}: {r.stderr.decode().strip()}")
    return r.stdout.decode("utf-8", "surrogateescape")


def read(path, rev):
    if rev is None:
        with open(os.path.join(TOP, path), encoding="utf-8") as fh:
            return fh.read()
    return git(["show", f"{rev}:{path}"])


def exists(path, rev):
    if rev is None:
        return os.path.exists(os.path.join(TOP, path))
    return (
        subprocess.run(
            ["git", "cat-file", "-e", f"{rev}:{path}"],
            cwd=TOP,
            capture_output=True,
            check=False,
        ).returncode
        == 0
    )


def build_defines(objdir):
    """The defines of the configured build, from objdir's autoconf.mk."""
    path = os.path.join(objdir, "config", "autoconf.mk")
    try:
        with open(path) as fh:
            lines = fh.read().splitlines()
        line = next(l for l in lines if l.startswith("ACDEFINES = "))
    except (OSError, StopIteration):
        return dict(BUILTIN_DEFINES), "built-in defines"
    defines = {}
    for token in shlex.split(line[len("ACDEFINES = ") :]):
        if token.startswith("-D"):
            name, _, value = token[2:].partition("=")
            defines[name] = int(value) if value.isdigit() else value
    defines["MOZ_AARCH64"] = 1
    config = dict(l.split(" = ", 1) for l in lines if " = " in l)
    for name in MOZBUILD_DEFINES:
        if config.get(name):
            defines[name] = 1
    for module in config.get("MOZ_EME_MODULES", "").split():
        defines[f"MOZ_{module.upper()}_EME"] = 1
    return defines, path


def release_defines(nightly):
    """The same build on the Release channel."""
    defines = dict(nightly)
    for name in ("NIGHTLY_BUILD", "EARLY_BETA_OR_EARLIER"):
        defines.pop(name, None)
    defines["RELEASE_OR_BETA"] = 1
    defines["MOZ_UPDATE_CHANNEL"] = "release"
    return defines


class Source(StringIO):
    def __init__(self, text, name):
        super().__init__(text)
        self.name = os.path.join(TOP, name)


def preprocess(text, name, defines, substitute=False):
    pp = Preprocessor()
    pp.context.update(defines)
    pp.out = StringIO()
    if substitute:
        pp.do_filter("substitution")
    pp.do_include(Source(text, name))
    return pp.out.getvalue()


def canonical(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    return str(value)


def yaml_prefs(text, defines):
    entries = yaml.safe_load(preprocess(text, YAML_FILE, defines, substitute=True))
    return {e["name"]: canonical(e.get("value")) for e in entries}


def skip_string(text, i):
    quote = text[i]
    i += 1
    while i < len(text) and text[i] != quote:
        i += 2 if text[i] == "\\" else 1
    return i + 1


def skip_blank(text, i):
    while i < len(text):
        if text[i].isspace():
            i += 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = len(text) if i < 0 else i
        elif text.startswith("/*", i):
            i = text.find("*/", i)
            i = len(text) if i < 0 else i + 2
        else:
            break
    return i


def js_value(raw):
    raw = raw.strip()
    if len(raw) >= 2 and raw[0] in "\"'" and raw[-1] == raw[0]:
        body = raw[1:-1]
        return re.sub(
            r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), body
        )
    return re.sub(r"\s+", " ", raw)


def js_prefs(text):
    """name -> value for every pref(), sticky_pref() and user_pref() call."""
    prefs, i, n = {}, 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i) or text.startswith("/*", i):
            i = skip_blank(text, i)
        elif c in "\"'`":
            i = skip_string(text, i)
        else:
            m = PREF_CALL.match(text, i)
            if m and not (i and (text[i - 1].isalnum() or text[i - 1] == "_")):
                j = skip_blank(text, m.end())
                if j < n and text[j] in "\"'":
                    end = skip_string(text, j)
                    name = js_value(text[j:end])
                    j = skip_blank(text, end)
                    if j < n and text[j] == ",":
                        start, depth = j + 1, 0
                        k = start
                        while k < n:
                            ch = text[k]
                            if ch in "\"'`":
                                k = skip_string(text, k)
                                continue
                            if text.startswith("//", k) or text.startswith("/*", k):
                                k = skip_blank(text, k)
                                continue
                            if ch in "([{":
                                depth += 1
                            elif ch in ")]}" and depth:
                                depth -= 1
                            elif ch in ",)" and depth == 0:
                                break
                            k += 1
                        prefs[name] = canonical(js_value(text[start:k]))
                        i = k
                        continue
            i += 1
    return prefs


def js_files(rev):
    """Default-prefs files whose conditions depend on the channel."""
    args = ["grep", "-l", "-E", CHANNEL_CONDITION]
    if rev:
        args.append(rev)
    args += ["--", "*.js", *SKIPPED_PATHS]
    out = git(args, check=False).splitlines()
    files = []
    for line in out:
        path = line.split(":", 1)[1] if rev else line
        if IGNORED_DIRS & set(path.split("/")) or path.startswith(
            BACKGROUND_TASK_PREFS
        ):
            continue
        if re.search(r"^\s*(?:sticky_|user_)?pref\(", read(path, rev), re.M):
            files.append(path)
    return sorted(files)


def strip_m6e_include(text):
    return re.sub(r"^#include m6e\.js\s*$", "", text, flags=re.M)


def evaluate(rev, defines):
    """name -> (value or ABSENT, defining file) for the final default."""
    result = {}
    files = js_files(rev)
    ordered = [f for f in files if f not in (ALL_JS, FIREFOX_JS)]
    ordered = ([ALL_JS] if ALL_JS in files else []) + ordered
    ordered += [FIREFOX_JS] if FIREFOX_JS in files else []
    for name, value in yaml_prefs(read(YAML_FILE, rev), defines).items():
        result[name] = (value, YAML_FILE)
    for path in ordered:
        text = strip_m6e_include(read(path, rev))
        for name, value in js_prefs(preprocess(text, path, defines)).items():
            result[name] = (value, path)
    return result


def differences(rev, nightly, release):
    a, b = evaluate(rev, nightly), evaluate(rev, release)
    diff = {}
    for name in sorted(set(a) | set(b)):
        va, vb = a.get(name, (ABSENT, None)), b.get(name, (ABSENT, None))
        if va[0] != vb[0]:
            diff[name] = (va[0], vb[0], (va[1] or vb[1]))
    return diff


def m6e_prefs(rev, nightly):
    text = preprocess(read(M6E_JS, rev), M6E_JS, nightly)
    return js_prefs(text)


def reviewed_names():
    names = {}
    with open(REVIEWED, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if line and not line.startswith("#"):
                name, _, reason = line.partition("#")
                names[name.strip()] = reason.strip()
    return names


def is_inert(nightly, release, name):
    """A pref that only Nightly has and that is off there."""
    return (
        release is ABSENT
        and nightly in ("false", "0", "", "null")
        and "disable" not in name
    )


def show(value):
    return "(absent)" if value is ABSENT else value


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--since", metavar="REV", help="also mark prefs new since REV")
    parser.add_argument(
        "--head", metavar="REV", help="read the tree at REV, not the working tree"
    )
    parser.add_argument("--all", action="store_true", help="list every difference")
    parser.add_argument(
        "--pref",
        action="append",
        metavar="NAME",
        help="show how NAME resolves before m6e.js, then exit",
    )
    parser.add_argument(
        "--objdir",
        default=os.path.join(TOP, "obj-m6e"),
        help="read the build's defines",
    )
    parser.add_argument(
        "--builtin",
        action="store_true",
        help="ignore the objdir, use the built-in defines",
    )
    parser.add_argument(
        "--format",
        choices=("text", "checks"),
        default="text",
        help="checks: one problem per line and note: lines, for post-merge-checks.sh",
    )
    args = parser.parse_args()

    if args.builtin:
        nightly, source = dict(BUILTIN_DEFINES), "built-in defines"
    else:
        nightly, source = build_defines(args.objdir)
    release = release_defines(nightly)
    if args.pref:
        on_nightly, on_release = (
            evaluate(args.head, nightly),
            evaluate(args.head, release),
        )
        for name in args.pref:
            n, r = (
                on_nightly.get(name, (ABSENT, None)),
                on_release.get(name, (ABSENT, None)),
            )
            print(
                f"{name}: Nightly {show(n[0])}, Release {show(r[0])} ({n[1] or r[1]})"
            )
        return 0
    current = differences(args.head, nightly, release)
    earlier = differences(args.since, nightly, release) if args.since else {}
    handled = m6e_prefs(args.head, nightly)
    reviewed = reviewed_names()

    new, rows = [], []
    for name, (n, r, path) in current.items():
        if name in handled:
            state = f"m6e.js sets {show(handled[name])}"
        elif name in reviewed:
            state = "reviewed"
        elif is_inert(n, r, name):
            state = "inert"
        else:
            state = "NEW"
            new.append(name)
        rows.append((name, n, r, path, state))

    checks = args.format == "checks"
    lead = "note: " if checks else ""
    print(f"{lead}defines: {source}")
    print(
        f"{lead}{len(current)} prefs default differently on Nightly and Release: "
        f"{sum(1 for r in rows if r[4].startswith('m6e.js'))} set by m6e.js, "
        f"{sum(1 for r in rows if r[4] == 'reviewed')} reviewed, "
        f"{sum(1 for r in rows if r[4] == 'inert')} Nightly-only and off, {len(new)} NEW"
    )
    stale = sorted(n for n in reviewed if n not in current)
    for name, n, r, path, state in rows:
        if state == "NEW" or args.all:
            mark = ""
            if args.since and earlier.get(name) != current[name]:
                mark = f" [new or changed since {args.since}]"
            if checks and state == "NEW":
                print(
                    f"{name}: Nightly {show(n)}, Release {show(r)} ({path}){mark}; "
                    "set it in m6e.js or add it to nightly-prefs-reviewed.txt"
                )
            elif checks:
                print(f"note: {name}: Nightly {show(n)}, Release {show(r)}, {state}")
            else:
                print(
                    f"{state}\t{name}\tNightly {show(n)}\tRelease {show(r)}\t{path}{mark}"
                )
    if stale and not checks:
        print(
            f"{len(stale)} reviewed prefs no longer differ (drop them from the list):"
        )
    for name in stale:
        if checks:
            print(
                f"{name}: in nightly-prefs-reviewed.txt but no longer differs between Nightly and Release; drop it"
            )
        else:
            print(f"stale\t{name}")
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
