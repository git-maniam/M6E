# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""The checks of post-merge-checks.sh that need more than a shell command.

    python3 tools/m6e/merge/check-helpers.py <check> [--base REV] [--head REV]

--base is the M6E tree before the merge (default HEAD, so a merge that is
applied but not committed) and --head the tree after it (default: the working
tree and index). Every check only reads. It prints what needs attention and
exits 1, or prints nothing and exits 0. Lines that start with "note:" are
information for the reader and never fail a check.

Checks: fox-art, gmp-sources, doh, lint-paths, remote-settings, about-pages,
frontend-ifdefs.
"""

import argparse
import base64
import json
import os
import re
import subprocess
import sys

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path[:0] = [os.path.join(TOP, "third_party", "python", "pyyaml", "lib")]

import yaml  # noqa: E402

problems = []


def attention(message):
    problems.append(message)
    print(message)


def note(message):
    print(f"note: {message}")


def git(args, check=True):
    r = subprocess.run(["git", *args], cwd=TOP, capture_output=True, check=False)
    if check and r.returncode:
        sys.exit(f"git {' '.join(args)}: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout.decode("utf-8", "surrogateescape")


def read(path, rev):
    """A file's text at rev (None: the working tree), or None if missing."""
    if rev is None:
        try:
            with open(
                os.path.join(TOP, path), encoding="utf-8", errors="replace"
            ) as fh:
                return fh.read()
        except OSError:
            return None
    r = subprocess.run(
        ["git", "show", f"{rev}:{path}"], cwd=TOP, capture_output=True, check=False
    )
    return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else None


class Tree:
    """The tracked files and directories at a revision (None: the index)."""

    def __init__(self, rev):
        if rev is None:
            out = git(["ls-files", "-z"])
        else:
            out = git(["ls-tree", "-r", "-z", "--name-only", rev])
        self.files = {f for f in out.split("\0") if f}
        self.dirs = set()
        for f in self.files:
            d = os.path.dirname(f)
            while d and d not in self.dirs:
                self.dirs.add(d)
                d = os.path.dirname(d)

    def has(self, path):
        path = path.rstrip("/")
        return path in self.files or path in self.dirs


def changes(base, head):
    """path -> A, M or D, for the files that differ between base and head.

    post-merge-checks.sh passes the list it already made in M6E_MERGE_CHANGES.
    """
    saved = os.environ.get("M6E_MERGE_CHANGES")
    if saved and os.path.exists(saved):
        with open(saved, encoding="utf-8", errors="surrogateescape") as fh:
            out = fh.read()
    else:
        out = git(
            ["diff", "--name-status", "--no-renames", "-z", base]
            + ([head] if head else [])
        )
    parts = out.split("\0")
    return {p: s[0] for s, p in zip(parts[0::2], parts[1::2])}


# ---------------------------------------------------------------- fox-art

IMAGE = re.compile(r"\.(svg|png|gif|jpe?g|webp|avif|apng)$", re.I)
FOX_NAME = re.compile(r"fox|mascot|(^|[-_.])kit([-_.]|$)", re.I)
NOT_SHIPPED = re.compile(r"(^|/)(tests?|testing|docs?|third_party|mobile|taskcluster)/")
NEW_IMAGES_SHOWN = 50


def check_fox_art(base, head):
    with open(os.path.join(TOP, "tools/m6e/branding/fox-art.txt")) as fh:
        listed = [l.strip() for l in fh if l.strip() and not l.startswith("#")]
    try:
        with open(os.path.join(TOP, "browser/branding/m6e/default256.png"), "rb") as fh:
            icon = base64.b64encode(fh.read()).decode()
    except OSError:
        attention(
            "browser/branding/m6e/default256.png is missing; run tools/m6e/branding/make-icons.sh"
        )
        return
    tree = Tree(head)
    changed = changes(base, head)
    for path in listed:
        if path not in tree.files:
            attention(
                f"{path}: listed in fox-art.txt but gone; replace-fox-art.sh stops on it"
            )
        elif path in changed:
            attention(f"{path}: changed by the merge; rerun replace-fox-art.sh")
        elif path.endswith(".svg") and icon not in (read(path, head) or ""):
            attention(f"{path}: an SVG in fox-art.txt that does not hold the M6E icon")
    known = set(listed)
    same_dirs = {os.path.dirname(p) for p in listed}
    new_images = []
    for path, status in sorted(changed.items()):
        if (
            status != "A"
            or path in known
            or not IMAGE.search(path)
            or NOT_SHIPPED.search(path)
        ):
            continue
        if FOX_NAME.search(os.path.basename(path)):
            attention(
                f"{path}: new image named like fox artwork; add it to fox-art.txt"
            )
        else:
            new_images.append(path)
    if new_images:
        beside = sum(1 for p in new_images if os.path.dirname(p) in same_dirs)
        note(
            f"{len(new_images)} other new images ({beside} next to listed fox artwork, marked *); "
            "only names with fox, mascot or kit are caught automatically, so look at these and "
            "add any mascot, fox illustration or Firefox logo to fox-art.txt:"
        )
        for path in new_images[:NEW_IMAGES_SHOWN]:
            mark = "*" if os.path.dirname(path) in same_dirs else " "
            print(f"note:  {mark} {path}")
        if len(new_images) > NEW_IMAGES_SHOWN:
            note(f"  ... and {len(new_images) - NEW_IMAGES_SHOWN} more")
    bitmaps = sum(1 for p in listed if not p.endswith(".svg"))
    note(
        f"{len(listed)} images listed; the {bitmaps} PNG and GIF files cannot be checked for content"
    )


# ------------------------------------------------------------ gmp-sources

GMP_FILES = ["openh264", "widevinecdm", "widevinecdm_l1"]
GMP_HOSTS = {
    "ciscobinary.openh264.org",
    "www.google.com",
    "dl.google.com",
    "edgedl.me.gvt1.com",
}
GMP_DIR = "toolkit/content/gmp-sources"


def check_gmp_sources(base, head, upstream, upstream_new):
    changed = changes(base, head)
    if upstream_new:
        have = subprocess.run(
            ["git", "-C", upstream, "cat-file", "-e", f"{upstream_new}^{{commit}}"],
            capture_output=True,
            check=False,
        )
        if have.returncode:
            attention(
                f"upstream commit {upstream_new[:10]} is not in {upstream}; fetch it first"
            )
            upstream_new = None
    for name in GMP_FILES:
        path = f"{GMP_DIR}/{name}.json"
        text = read(path, head)
        if text is None:
            attention(f"{path}: missing")
            continue
        data = json.loads(text)
        hosts = set(re.findall(r"https?://([^/\"]+)", text))
        for host in sorted(hosts - GMP_HOSTS):
            attention(f"{path}: downloads from {host}, which is not Google or Cisco")
        if name != "widevinecdm_l1":
            for vendor, info in data["vendors"].items():
                if "Darwin_aarch64-gcc3" not in info["platforms"]:
                    attention(f"{path}: {vendor} has no Darwin_aarch64-gcc3 download")
        if path in changed:
            note(f"{path} changed in the merge: now {data['name']}")
        else:
            note(f"{path} is {data['name']} (unchanged by the merge)")
        if upstream_new:
            new = subprocess.run(
                ["git", "-C", upstream, "show", f"{upstream_new}:{path}"],
                capture_output=True,
                check=False,
            )
            if new.returncode:
                note(f"{path} is not in upstream {upstream_new[:10]}")
            elif new.stdout.decode("utf-8", "replace") != text:
                attention(
                    f"{path}: differs from upstream {upstream_new[:10]}; copy it from there"
                )
    note(
        "the lists are plain copies of upstream's; to refresh one by hand: "
        f"git -C {upstream} show <upstream sha>:{GMP_DIR}/<name>.json > {GMP_DIR}/<name>.json"
    )


# -------------------------------------------------------------------- doh


def pref_string(text, name):
    m = re.search(rf'pref\(\s*"{re.escape(name)}"\s*,\s*"([^"]*)"', text or "")
    return m.group(1) if m else None


def check_doh(base, head):
    providers = read("services/settings/static-dumps/main/doh-providers.json", head)
    uri = pref_string(
        read("browser/app/profile/m6e.js", head), "network.trr.default_provider_uri"
    )
    if providers is None or uri is None:
        attention(
            "doh-providers.json or the network.trr.default_provider_uri pref is missing"
        )
        return
    defaults = [r for r in json.loads(providers)["data"] if r.get("autoDefault")]
    if len(defaults) != 1:
        attention(
            f"doh-providers.json has {len(defaults)} providers with autoDefault; expected 1"
        )
        return
    default = defaults[0]
    if default["uri"] != uri:
        attention(f"the default provider is {default['uri']}, but m6e.js sets {uri}")
    if "security.cloudflare-dns.com" not in default["uri"]:
        attention(
            f"the default provider is {default['uri']}, not Cloudflare for Families (D25)"
        )
    if default.get("UIName") != "Cloudflare for Families (security)":
        attention(
            f"the default provider is named {default.get('UIName')!r} in Settings"
        )
    changed = changes(base, head)
    for name in ("doh-providers.json", "doh-config.json"):
        path = f"services/settings/static-dumps/main/{name}"
        if path in changed:
            note(f"{path} changed in the merge; check its rollout and provider lists")
    note(f"default provider: {default['UIName']}, {default['uri']}")


# ------------------------------------------------------------- lint-paths

GLOB = re.compile(r"[*?\[{]")
PATH_LIKE = re.compile(r"^[A-Za-z0-9_.@+*?\[\]{},/-]+$")
REMOVED_ROOTS = {
    "mobile",
    "gradle",
    ".github",
    ".agents",
    ".claude",
    ".idea",
    ".vscode",
    ".zed",
}
LINT_FILES = [
    ".prettierignore",
    ".prettierignore-css",
    ".prettierignore-non-css",
    ".stylelintignore",
    ".clang-format-ignore",
    "eslint-subdirs.config.mjs",
]


def yaml_strings(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for value in node.values():
            yield from yaml_strings(value)
    elif isinstance(node, list):
        for value in node:
            yield from yaml_strings(value)


def lint_entries(tree, rev):
    """(config file, entry) for each repo path a lint config names."""
    roots = {p.split("/")[0] for p in tree.files} | REMOVED_ROOTS
    entries = set()

    def add(config, s):
        s = s.strip()
        if "/" in s and PATH_LIKE.match(s) and s.split("/")[0].lstrip("/") in roots:
            entries.add((config, s.lstrip("/")))

    configs = sorted(
        f
        for f in tree.files
        if f.startswith("tools/lint/") and f.count("/") == 2 and f.endswith(".yml")
    )
    for config in configs:
        try:
            for doc in yaml.safe_load_all(read(config, rev) or ""):
                for s in yaml_strings(doc):
                    add(config, s)
        except yaml.YAMLError:
            note(f"{config}: not read, it does not parse as YAML")
    for config in LINT_FILES:
        text = read(config, rev) or ""
        if config.endswith(".mjs"):
            candidates = re.findall(r"[\"']([^\"'\s]+)[\"']", text)
        else:
            candidates = [
                l for l in text.splitlines() if l.strip() and not l.startswith("#")
            ]
        for s in candidates:
            add(config, s)
    return entries


def entry_exists(tree, entry):
    m = GLOB.search(entry)
    if not m:
        return tree.has(entry)
    prefix = entry[: m.start()].rsplit("/", 1)[0] if "/" in entry[: m.start()] else ""
    return not prefix or tree.has(prefix)


def check_lint_paths(base, head):
    before, after = Tree(base), Tree(head)
    old = {e for e in lint_entries(before, base) if not entry_exists(before, e[1])}
    now = {e for e in lint_entries(after, head) if not entry_exists(after, e[1])}
    for config, entry in sorted(now - old):
        attention(f"{config}: lists {entry}, which does not exist")
    if old & now:
        example = sorted(old & now)[0][1]
        note(
            f"{len(old & now)} entries already named missing files before the merge (e.g. {example})"
        )


# -------------------------------------------------------- remote-settings

UTILS = "services/settings/Utils.sys.mjs"
CLIENT_FILES = [
    "services/settings/RemoteSettingsClient.sys.mjs",
    "services/settings/Attachments.sys.mjs",
]
PRIVACY_WORDS = re.compile(
    r"track|privacy|cookie|fingerprint|bounce|query-strip|partition|block|classif|revoc|crlite|"
    r"safebrowsing|exempt|tainted|security",
    re.I,
)
NOT_SOURCE = [
    ":(exclude)*/test/*",
    ":(exclude)*/tests/*",
    ":(exclude)testing",
    ":(exclude)third_party",
    ":(exclude)docs",
]
COLLECTION_NAME = re.compile(
    r"(?:RemoteSettings\(\s*|collection(?:Name|_name)?[\"']?\s*[:=]\s*|COLLECTION(?:_NAME)?\s*=\s*)"
    r"[\"'`]([a-z0-9][a-z0-9_-]+)[\"'`]",
    re.I,
)


def js_set(text, name):
    m = re.search(rf"const {name}\s*=\s*new Set\(\s*\[(.*?)\]\s*\)", text, re.S)
    return set(re.findall(r"\"([^\"]+)\"", m.group(1))) if m else None


def mentioned(names, rev):
    """The subset of names that some source file at rev (None: the working tree) holds."""
    if not names:
        return set()
    patterns = [arg for name in sorted(names) for arg in ("-e", name)]
    out = git(
        ["grep", "-h", "-o", "-F", "-I", *patterns]
        + ([rev] if rev else [])
        + ["--", ".", ":(exclude)services/settings/Utils.sys.mjs", *NOT_SOURCE],
        check=False,
    )
    return {name for name in names if name in out}


def added_lines(base, head, pathspecs):
    """Yields (path, text) for each line the diff adds to files matching pathspecs."""
    args = ["diff", "-U0", "--no-color", "--no-renames", base] + (
        [head] if head else []
    )
    path = None
    for line in git(args + ["--"] + pathspecs, check=False).splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("+") and path:
            yield path, line[1:]


def guard_sites(rev):
    return sum((read(p, rev) or "").count("isAllowedCollection(") for p in CLIENT_FILES)


def check_remote_settings(base, head):
    utils = read(UTILS, head) or ""
    buckets = js_set(utils, "M6E_ALLOWED_BUCKETS")
    allowed = js_set(utils, "M6E_ALLOWED_MAIN_COLLECTIONS")
    if buckets is None or allowed is None or "isAllowedCollection(" not in utils:
        attention(f"{UTILS}: the M6E allowlist is gone; restore it (D2)")
        return
    now_sites, old_sites = guard_sites(head), guard_sites(base)
    if now_sites < max(old_sites, 4):
        attention(
            f"isAllowedCollection() is called {now_sites} times in RemoteSettingsClient and "
            f"Attachments (was {old_sites}); a sync or download path lost its check"
        )
    if (
        "services/settings/test/unit/test_m6e_allowed_collections.js"
        not in Tree(head).files
    ):
        attention("test_m6e_allowed_collections.js is gone")

    for name in sorted(allowed - mentioned(allowed, head)):
        attention(
            f"allowed collection {name} appears nowhere in the tree any more; "
            "was it renamed or removed upstream?"
        )

    new = {}
    for path, line in added_lines(
        base, head, ["*.mjs", "*.js", "*.jsx", "*.rs", "*.cpp", "*.yaml"]
    ):
        if re.search(r"(^|/)(tests?|testing)/", path):
            continue
        for m in COLLECTION_NAME.finditer(line):
            new.setdefault(m.group(1), path)
    for path, status in changes(base, head).items():
        if status == "A" and path.startswith((
            "services/settings/dumps/",
            "services/settings/static-dumps/",
        )):
            if path.endswith(".json") and path.count("/") == 4:
                new.setdefault(os.path.basename(path)[:-5], path)
    known = mentioned(set(new) - allowed, base)
    for name, where in sorted(new.items()):
        if name in allowed or name in known:
            continue
        if PRIVACY_WORDS.search(name):
            attention(
                f"new collection {name} ({where}) looks like privacy or security data; "
                "decide whether to allow it"
            )
        else:
            note(
                f"new collection {name} ({where}): not allowed, so it uses bundled data only"
            )

    seen = set()
    for path, line in added_lines(base, head, ["*.mjs", "*.js", "*.rs", "*.yaml"]):
        for m in re.finditer(
            r"bucket(?:Name|_name)?[\"']?\s*[:=]\s*[\"']([a-z0-9-]+)", line, re.I
        ):
            bucket = m.group(1).replace("-preview", "")
            if bucket not in seen and bucket not in buckets | {"main", "monitor"}:
                seen.add(bucket)
                attention(
                    f"{path}: names the Remote Settings bucket {bucket}, which is not allowed"
                )
    note(
        f"{len(allowed)} main collections plus the buckets {', '.join(sorted(buckets))} are allowed. "
        "This check sees only collections the merge names in code or bundles as dumps; the "
        "server's full list (buckets/monitor/collections/changes/changeset) is not fetched."
    )


# ------------------------------------------------------------ about-pages

TABLES = [
    "docshell/base/nsAboutRedirector.cpp",
    "browser/components/about/AboutRedirector.cpp",
]
CONF_FILES = [
    "docshell/build/components.conf",
    "browser/components/about/components.conf",
    "browser/components/newtab/components.conf",
]
DORMANT_PAGES = [
    "translations",
    "aichatcontent",
    "smartpage",
    "smartwindowtasks",
    "smartformfillreview",
    "asrouter",
    "messagepreview",
    "referrals",
]
REACHABLE = ("MAKE_LINKABLE", "URI_SAFE_FOR_UNTRUSTED_CONTENT")


def strip_comments(text):
    def keep_strings(m):
        return m.group(0) if m.group(0).startswith('"') else " "

    return re.sub(
        r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', keep_strings, text, flags=re.S
    )


def redirect_tables(rev):
    """path -> {id: set of nsIAboutModule flags}, for each kRedirMap table."""
    tables = {}
    for path in TABLES:
        text = strip_comments(read(path, rev) or "")
        start = text.find("kRedirMap[]")
        entries = tables[path] = {}
        if start < 0:
            continue
        for m in re.finditer(
            r"\{\s*\"([^\"]+)\",\s*\"[^\"]*\",\s*([^{}]*?)\}\s*,", text[start:], re.S
        ):
            entries[m.group(1)] = set(re.findall(r"nsIAboutModule::(\w+)", m.group(2)))
    return tables


def redirect_table(rev):
    """id -> set of nsIAboutModule flags, over both kRedirMap tables."""
    merged = {}
    for entries in redirect_tables(rev).values():
        merged.update(entries)
    return merged


def module_ids(rev):
    """ids registered through components.conf lists or a literal contract id."""
    ids = set()
    for path in CONF_FILES:
        text = read(path, rev) or ""
        for block in re.findall(r"pages\s*\+?=\s*\[(.*?)\]", text, re.S):
            ids |= set(re.findall(r"['\"]([a-z0-9-]+)['\"]", block))
    return ids


def literal_ids(rev):
    out = git(
        ["grep", "-h", "-o", "-I", "-E", r"protocol/about;1\?what=[a-z0-9-]+"]
        + ([rev] if rev else [])
        + ["--", "*.conf", "*.js", "*.mjs", *NOT_SOURCE],
        check=False,
    )
    return set(re.findall(r"what=([a-z0-9-]+)", out))


def check_about_pages(base, head):
    now_tables, old_tables = redirect_tables(head), redirect_tables(base)
    now_table, old_table = redirect_table(head), redirect_table(base)
    registered = module_ids(head) | literal_ids(head)
    now_ids = registered | set(now_table)
    old_ids = module_ids(base) | literal_ids(base) | set(old_table)
    for path in TABLES:
        if not now_tables[path]:
            attention(
                f"kRedirMap not found or empty in {path}; the check cannot read its pages"
            )
            continue
        for page in sorted(set(old_tables[path]) - set(now_table)):
            if page in registered:
                attention(
                    f"about:{page} is gone from the table in {path} but still registered; "
                    "was the table's format changed? Its flags can no longer be checked"
                )
    for page in sorted(now_ids - old_ids):
        flags = now_table.get(page, set())
        reach = sorted(flags & set(REACHABLE))
        detail = f"; flags {', '.join(reach)}" if reach else ""
        attention(f"new about:{page} page{detail}")
    for page, flags in sorted(now_table.items()):
        gained = (flags & set(REACHABLE)) - old_table.get(page, set())
        if page in old_ids and gained:
            attention(
                f"about:{page} became reachable from web content: {', '.join(sorted(gained))}"
            )
    for page in DORMANT_PAGES:
        if page in now_ids:
            attention(
                f"about:{page} is registered again (P4.8g unregistered it); remove it"
            )
    flags = re.compile("|".join(REACHABLE))
    for path, line in added_lines(
        base, head, ["*.cpp", "*.h", "*.mm", "*.js", "*.mjs"]
    ):
        if (
            flags.search(line)
            and path not in TABLES
            and not re.search(r"(^|/)(tests?|testing)/", path)
        ):
            note(
                f"{path} adds a line that makes an about: page reachable: {line.strip()[:90]}"
            )
    linkable = sum(1 for f in now_table.values() if "MAKE_LINKABLE" in f)
    safe = sum(1 for f in now_table.values() if "URI_SAFE_FOR_UNTRUSTED_CONTENT" in f)
    note(
        f"{len(now_table)} pages in the redirect tables, {safe} safe for untrusted content, {linkable} linkable"
    )


# --------------------------------------------------------- frontend-ifdefs

PLATFORM_IF = (
    r"^#\s*(if|ifdef|ifndef|elif)\b.*\b"
    r"(XP_WIN|XP_LINUX|MOZ_WIDGET_GTK|MOZ_WIDGET_ANDROID|ANDROID|MOZ_X11|MOZ_WAYLAND|XP_IOS)\b"
)
FRONTEND_GLOBS = [
    "*.js",
    "*.mjs",
    "*.jsm",
    "*.xhtml",
    "*.xul",
    "*.html",
    "*.inc",
    "*.css",
    "*.ftl",
    "*.properties",
    "*.dtd",
    "*.manifest",
    "*.mn",
    "*.in",
    "*.yaml",
    "*.json",
    "*.svg",
]


def platform_if_counts(rev):
    """path -> number of preprocessor lines that test a non-macOS platform."""
    args = ["grep", "-c", "-I", "-P", PLATFORM_IF] + ([rev] if rev else [])
    out = git(
        args
        + [
            "--",
            *FRONTEND_GLOBS,
            ":(exclude)third_party",
            ":(exclude)*/test/*",
            ":(exclude)*/tests/*",
            ":(exclude)testing",
            ":(exclude)js/src",
            ":(exclude)taskcluster",
            ":(exclude)mobile",
            ":(exclude)build",
            ":(exclude)docs",
        ],
        check=False,
    )
    counts = {}
    for entry in out.splitlines():
        path, _, number = (entry[len(rev) + 1 :] if rev else entry).rpartition(":")
        counts[path] = int(number)
    return counts


def check_frontend_ifdefs(base, head):
    """Some front-end files keep such lines for good (StaticPrefList.yaml, all.js, jar.mn
    files), so only the files where the merge added to the count are flagged."""
    before, after = platform_if_counts(base), platform_if_counts(head)
    for path, number in sorted(after.items()):
        gained = number - before.get(path, 0)
        if gained > 0:
            attention(
                f"{path}: {gained} new non-macOS #if lines in a preprocessed front-end file; "
                "strip them (strip_ifdefs.py --frontend with a list holding this file)"
            )
    note(
        f"{len(after)} front-end files still have non-macOS #if lines ({sum(after.values())} lines)"
    )


CHECKS = {
    "fox-art": lambda a: check_fox_art(a.base, a.head),
    "gmp-sources": lambda a: check_gmp_sources(
        a.base, a.head, a.upstream, a.upstream_new
    ),
    "doh": lambda a: check_doh(a.base, a.head),
    "lint-paths": lambda a: check_lint_paths(a.base, a.head),
    "remote-settings": lambda a: check_remote_settings(a.base, a.head),
    "about-pages": lambda a: check_about_pages(a.base, a.head),
    "frontend-ifdefs": lambda a: check_frontend_ifdefs(a.base, a.head),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("check", choices=sorted(CHECKS))
    parser.add_argument("--base", default="HEAD")
    parser.add_argument("--head", default=None)
    parser.add_argument(
        "--upstream",
        default=os.path.abspath(os.path.join(TOP, "..", "m6e-upstream.git")),
    )
    parser.add_argument("--upstream-new", default=None)
    args = parser.parse_args()
    CHECKS[args.check](args)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
