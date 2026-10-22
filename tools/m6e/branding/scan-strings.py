# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""List user-visible en-US strings that still name Firefox or Mozilla.

    python3 tools/m6e/branding/scan-strings.py [--json out.json] [paths...]

Scans Fluent (.ftl) message values and attributes, and .properties and .dtd
values, under every locales/en-US directory (or the given paths). Comments,
message identifiers and text inside URLs are ignored.
"""

import argparse
import json
import re
import subprocess
import sys

NAME = re.compile(r"\b(Firefox|Mozilla)\b")
URL = re.compile(r"https?://\S+|[a-z0-9.-]+\.(?:mozilla|firefox)\.(?:org|com)\S*", re.I)
FTL_ENTRY = re.compile(r"^(-?[a-zA-Z][\w-]*)\s*=(.*)$")
FTL_ATTR = re.compile(r"^\s+\.([\w-]+)\s*=(.*)$")
# Licence attribution, which must name Firefox and Mozilla.
ATTRIBUTION_KEYS = {"aboutdialog-m6e-based-on", "rights-m6e-based-on"}


def visible(text):
    """Return the names found in `text` once URLs and placeables are removed."""
    text = URL.sub("", text)
    text = re.sub(r"\{[^}]*\}", "", text)
    return NAME.findall(text)


def scan_ftl(path, lines):
    entry = None
    for n, line in enumerate(lines, 1):
        if line.startswith("#") or not line.strip():
            continue
        if m := FTL_ENTRY.match(line):
            entry, value = m.group(1), m.group(2)
        elif m := FTL_ATTR.match(line):
            value = m.group(2)
        elif line[:1].isspace():
            value = line
        else:
            continue
        if names := visible(value):
            yield n, entry, names, line.strip()


def scan_properties(path, lines):
    for n, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", "!")) or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        if names := visible(value):
            yield n, key.strip(), names, stripped


def scan_dtd(path, lines):
    for n, line in enumerate(lines, 1):
        if m := re.search(r'<!ENTITY\s+(\S+)\s+"([^"]*)"', line):
            if names := visible(m.group(2)):
                yield n, m.group(1), names, line.strip()


SCANNERS = {".ftl": scan_ftl, ".properties": scan_properties, ".dtd": scan_dtd}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--json")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()

    files = args.paths or [
        f
        for f in subprocess.check_output(["git", "ls-files"], text=True).split("\n")
        if "/locales/en-US/" in f and f.endswith(tuple(SCANNERS)) and "/test" not in f
    ]
    hits = []
    for path in files:
        ext = path[path.rfind(".") :]
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        for n, key, names, text in SCANNERS[ext](path, lines):
            if key in ATTRIBUTION_KEYS:
                continue
            hits.append({
                "file": path,
                "line": n,
                "key": key,
                "names": names,
                "text": text,
            })

    by_file = {}
    for h in hits:
        by_file[h["file"]] = by_file.get(h["file"], 0) + 1
    for f, count in sorted(by_file.items(), key=lambda kv: -kv[1]):
        print(f"{count:5}  {f}")
    print(f"{len(hits)} strings in {len(by_file)} files")
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(hits, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
