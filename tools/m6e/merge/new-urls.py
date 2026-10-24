# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""List the external URLs a merge added to code that ships in the app.

    python3 tools/m6e/merge/new-urls.py [--base REV] [--head REV] [--all]

Reads the added lines of `git diff REV` (--base, default HEAD, so a merge
that is applied but not committed; --head compares with a commit instead of
the working tree) and groups the http(s), ws(s) and ftp URLs by host. Left
out unless --all: tests, documentation, CI and build tooling, vendored
libraries and dependency manifests, and URLs in comment lines. A host needs
attention when it is not one of the hosts a fresh profile may contact and is
either Mozilla's or does not appear anywhere in the base tree. Links to
bug trackers, specifications and code hosts, and new links to a site that is
already in the tree, are only listed. Exit status 1 if any host needs
attention.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# The hosts a fresh profile may contact (spec, section 3).
ALLOWED_HOSTS = {
    "firefox.settings.services.mozilla.com",
    "content-signature-2.cdn.mozilla.net",
    "firefox-settings-attachments.cdn.mozilla.net",
    "push.services.mozilla.com",
    "security.cloudflare-dns.com",
    "update.googleapis.com",
    "ciscobinary.openh264.org",
    "www.google.com",
    "dl.google.com",
    "edgedl.me.gvt1.com",
}
# Where the project keeps bugs, reviews, specifications and code. A link here
# is not an endpoint the browser contacts.
REFERENCE_HOSTS = re.compile(
    r"(^|\.)(bugzilla\.mozilla\.org|bugzil\.la|phabricator\.services\.mozilla\.com|searchfox\.org|"
    r"hg(-edge)?\.mozilla\.org|wiki\.mozilla\.org|developer\.mozilla\.org|"
    r"firefox-source-docs\.mozilla\.org|w3\.org|w3c\.github\.io|whatwg\.org|csswg\.org|"
    r"tc39\.es|github\.com|githubusercontent\.com|github\.io|chromium\.org|googlesource\.com|"
    r"crbug\.com|webkit\.org|ietf\.org|rfc-editor\.org|wikipedia\.org|creativecommons\.org|"
    r"readthedocs\.io|pythonhosted\.org|python\.org)$"
)
LICENCE_URL = re.compile(r"mozilla\.org/MPL/")
MOZILLA = re.compile(
    r"(^|\.)(mozilla\.(org|com|net)|firefox\.com|mozaws\.net|mozgcp\.net|"
    r"getpocket\.com|pocket\.com|mozit\.cloud|mozilla\.cloud|cdn\.mozilla\.net)$"
)
URL = re.compile(
    r"\b(?:https?|wss?|ftp)://([A-Za-z0-9._-]+)(?::\d+)?[^\s\"'<>)\]}\\`]*"
)
NOT_HOSTS = re.compile(
    r"(^localhost$|^127\.|^0\.0\.0\.0$|^\[|\.example(\.(com|org|net))?$|^example\.(com|org|net)$|"
    r"\.test$|^mochi\.test$|^[a-z0-9]+$|\.localhost$|^%|^\$)"
)

SKIPPED_DIRS = {
    "test",
    "tests",
    "testing",
    "gtest",
    "gtests",
    "mochitest",
    "mochitests",
    "reftest",
    "crashtests",
    "fuzztest",
    "fuzz",
    "fuzzing",
    "docs",
    "doc",
    "documentation",
    "taskcluster",
    "tools",
    "python",
    "build",
    ".github",
    ".gitlab",
    "third_party",
    "node_modules",
    "examples",
    "samples",
    "benchmarks",
    "talos",
    "raptor",
    "mozharness",
    "ci",
    "devtools-tests",
}
SKIPPED_NAMES = re.compile(
    r"(^|/)(Cargo\.(lock|toml)|package(-lock)?\.json|moz\.yaml|README[^/]*|LICENSE[^/]*|"
    r"AUTHORS|CODE_OF_CONDUCT\.md|CLOBBER|\.hgignore|[^/]*\.(md|rst|mdx|toml|lock|lic|licence))$|"
    r"(^|/)(test_[^/]*|[^/]*_test\.[^/]*|browser_[^/]*\.js|head([_-][^/]*)?\.js|"
    r"metrics\.yaml|pings\.yaml|tags\.yaml)$"
)
COMMENT = re.compile(
    r"^\s*(//|/\*|\*|#(?!\s*(include|define|if|else|elif|endif|ifdef|ifndef|expand|filter))|<!--|;)"
)


def git(args):
    r = subprocess.run(["git", *args], cwd=TOP, capture_output=True, check=False)
    if r.returncode not in (0, 1):
        sys.exit(f"git {' '.join(args)}: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout.decode("utf-8", "replace")


def skipped_path(path):
    dirs = set(path.split("/")[:-1])
    if path.startswith("tools/profiler/"):
        dirs.discard("tools")
    if SKIPPED_DIRS & dirs:
        return True
    return bool(SKIPPED_NAMES.search(path))


def added_lines(base, head):
    """Yields (path, line number, text) for each line the diff adds."""
    args = ["diff", "--no-renames", "-U0", "--no-color", base] + (
        [head] if head else []
    )
    path, number = None, 0
    for line in git(args + ["--"]).splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("@@"):
            m = re.match(r"@@ -\S+ \+(\d+)", line)
            number = int(m.group(1)) if m else 0
        elif line.startswith("+") and path:
            yield path, number, line[1:]
            number += 1


def known_hosts(hosts, rev):
    """The subset of hosts that some URL in the tree at rev uses."""
    if not hosts:
        return set()
    names = "|".join(h.replace(".", r"\.") for h in sorted(hosts))
    pattern = rf"://({names})([^A-Za-z0-9._-]|$)"
    out = git([
        "grep",
        "-h",
        "-o",
        "-I",
        "-E",
        pattern,
        rev,
        "--",
        ".",
        ":(exclude)third_party",
        ":(exclude)testing",
        ":(exclude)*/test/*",
        ":(exclude)*/tests/*",
        ":(exclude)docs",
        ":(exclude)taskcluster",
    ])
    return {
        m.group(1).lower()
        for m in re.finditer(rf"://({names})(?![A-Za-z0-9._-])", out, re.I)
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", default="HEAD", help="tree before the merge")
    parser.add_argument("--head", help="commit after the merge (default: working tree)")
    parser.add_argument(
        "--all", action="store_true", help="include tests, docs, comments"
    )
    parser.add_argument(
        "--format",
        choices=("text", "checks"),
        default="text",
        help="checks: one problem per line and note: lines, for post-merge-checks.sh",
    )
    args = parser.parse_args()

    hosts = defaultdict(list)
    left_out = defaultdict(int)
    for path, number, text in added_lines(args.base, args.head):
        found = [
            m
            for m in URL.finditer(text)
            if not NOT_HOSTS.search(m.group(1).lower())
            and not LICENCE_URL.search(m.group(0))
        ]
        if not found:
            continue
        reason = None
        if skipped_path(path):
            reason = "tests, docs, tooling or vendored files"
        elif COMMENT.match(text):
            reason = "comment lines"
        if reason and not args.all:
            left_out[reason] += len(found)
            continue
        for m in found:
            hosts[m.group(1).lower()].append((path, number, m.group(0)))

    known = known_hosts(set(hosts), args.base)
    attention, others = [], []
    for host in sorted(hosts):
        if host in ALLOWED_HOSTS:
            continue
        mozilla, new = bool(MOZILLA.search(host)), host not in known
        row = (host, mozilla, new, hosts[host])
        reference = REFERENCE_HOSTS.search(host)
        (attention if (mozilla or new) and not reference else others).append(row)

    total = sum(len(v) for v in hosts.values())
    checks = args.format == "checks"

    def show(rows):
        for host, mozilla, new, uses in rows:
            tags = ", ".join(
                t for t, on in (("Mozilla", mozilla), ("new host", new)) if on
            )
            path, number, url = uses[0]
            if checks:
                print(
                    f"{host} ({len(uses)} URLs; {tags}): {path}:{number}  {url[:100]}"
                )
                continue
            print(f"  {host} ({len(uses)}{'; ' + tags if tags else ''})")
            for path, number, url in uses[:2]:
                print(f"      {path}:{number}  {url[:110]}")

    def info(text):
        print(f"note: {text.strip()}" if checks else text)

    info(f"{total} URLs on {len(hosts)} hosts added to shipped code")
    for reason, count in sorted(left_out.items()):
        info(f"  not shown: {count} URLs in {reason}")
    for host in sorted(h for h in hosts if h in ALLOWED_HOSTS):
        path, number, url = hosts[host][0]
        info(f"  allowed host {host} ({len(hosts[host])}): {path}:{number}")
    if attention:
        if not checks:
            print(
                f"{len(attention)} host(s) need attention (Mozilla's, or not in the tree before):"
            )
        show(attention)
    if others:
        if checks:
            for host, mozilla, new, uses in others:
                info(
                    f"{host} ({len(uses)}), reference link or known site: {uses[0][0]}:{uses[0][1]}"
                )
        else:
            print(
                f"{len(others)} other host(s) (reference links, or sites already in the tree):"
            )
            show(others)
    return 1 if attention else 0


if __name__ == "__main__":
    sys.exit(main())
