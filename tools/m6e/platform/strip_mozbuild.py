# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Strip Windows, Linux and Android conditions from moz.build files (D21).

Evaluates each `if` in first-party moz.build, *.mozbuild and *.build files
against the macOS arm64 configuration. Conditions that are decided only by
platform keys (OS_ARCH, OS_TARGET, MOZ_WIDGET_TOOLKIT and so on) are
replaced by the branch that applies, keeping comments. Anything else,
including conditions that mix platform keys with other settings in a way
the platform doesn't decide, is left alone. Vendored directories are
skipped, as in strip_ifdefs.py.

    python3 tools/m6e/platform/strip_mozbuild.py            # dry run: counts
    python3 tools/m6e/platform/strip_mozbuild.py --apply    # rewrite files
"""

import argparse
import ast
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from strip_ifdefs import tracked_files, vendored_dirs  # noqa: E402

MACOS = {
    "OS_ARCH": "Darwin",
    "OS_TARGET": "Darwin",
    "HOST_OS_ARCH": "Darwin",
    "TARGET_KERNEL": "Darwin",
    "HOST_KERNEL": "Darwin",
    "TARGET_OS": "OSX",
    "HOST_OS": "OSX",
    "MOZ_WIDGET_TOOLKIT": "cocoa",
    "CC_TYPE": "clang",
    "HOST_CC_TYPE": "clang",
    "MOZ_WIDGET_GTK": None,
    "MOZ_WAYLAND": None,
    "MOZ_X11": None,
    "MOZ_ENABLE_DBUS": None,
    "MOZ_WIDGET_ANDROID": None,
    "GNU_CC": None,
}

UNKNOWN = object()


def config_key(node):
    """The key of a CONFIG["KEY"] subscript, or None."""
    if (
        isinstance(node, ast.Subscript)
        and isinstance(node.value, ast.Name)
        and node.value.id == "CONFIG"
        and isinstance(node.slice, ast.Constant)
        and isinstance(node.slice.value, str)
    ):
        return node.slice.value
    return None


def value(node):
    key = config_key(node)
    if key is not None:
        return MACOS.get(key, UNKNOWN)
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, (ast.Tuple, ast.List, ast.Set)):
        items = [value(e) for e in node.elts]
        return UNKNOWN if UNKNOWN in items else tuple(items)
    return UNKNOWN


def evaluate(node):
    """True, False or UNKNOWN for a condition under the macOS configuration."""
    if isinstance(node, ast.BoolOp):
        results = [evaluate(v) for v in node.values]
        if isinstance(node.op, ast.And):
            if False in results:
                return False
            return UNKNOWN if UNKNOWN in results else True
        if True in results:
            return True
        return UNKNOWN if UNKNOWN in results else False
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
        r = evaluate(node.operand)
        return r if r is UNKNOWN else not r
    if isinstance(node, ast.Compare) and len(node.ops) == 1:
        left, right = value(node.left), value(node.comparators[0])
        if left is UNKNOWN or right is UNKNOWN:
            return UNKNOWN
        op = node.ops[0]
        if isinstance(op, ast.Eq):
            return left == right
        if isinstance(op, ast.NotEq):
            return left != right
        if isinstance(op, (ast.In, ast.NotIn)) and isinstance(right, tuple):
            return (left in right) == isinstance(op, ast.In)
        return UNKNOWN
    key = config_key(node)
    if key is not None and key in MACOS:
        return bool(MACOS[key])
    return UNKNOWN


def indent_of(line):
    return len(line) - len(line.lstrip(" "))


def dedent(lines, amount):
    return [l[amount:] if l[:amount].strip() == "" else l.lstrip(" ") for l in lines]


def rewrite_once(source):
    """Rewrites the first decidable `if` in the file. Returns the new source or None."""
    tree = ast.parse(source)
    lines = source.splitlines(keepends=True)
    for node in ast.walk(tree):
        if not isinstance(node, ast.If):
            continue
        result = evaluate(node.test)
        if result is UNKNOWN:
            continue
        start = node.lineno - 1
        end = node.end_lineno
        base = indent_of(lines[start])
        body_start = node.test.end_lineno
        if node.orelse:
            else_line = next(
                i
                for i in range(node.body[-1].end_lineno, node.orelse[0].lineno)
                if re.match(r"\s*(else\s*:|elif\b)", lines[i])
                and indent_of(lines[i]) == base
            )
        else:
            else_line = end
        if re.match(r"\s*elif\b", lines[start]):
            # An elif belongs to its chain: drop the clause, or make it the
            # chain's else, without touching the earlier clauses.
            if result:
                block = [" " * base + "else:\n"] + lines[body_start:else_line]
            else:
                block = []
                end = else_line
            return "".join(lines[:start] + block + lines[end:])
        if result:
            block = dedent(lines[body_start:else_line], indent_of(lines[node.body[0].lineno - 1]) - base)
        elif not node.orelse:
            block = []
        elif re.match(r"\s*elif\b", lines[else_line]):
            block = lines[else_line:end]
            block[0] = re.sub(r"^(\s*)elif\b", r"\1if", block[0])
        else:
            block = dedent(lines[else_line + 1 : end], indent_of(lines[node.orelse[0].lineno - 1]) - base)
        return "".join(lines[:start] + block + lines[end:])
    return None


def process(path, apply):
    with open(path) as fh:
        original = fh.read()
    try:
        ast.parse(original)
    except SyntaxError:
        return "unparsable"
    source, changes = original, 0
    while True:
        new = rewrite_once(source)
        if new is None:
            break
        try:
            ast.parse(new)
        except SyntaxError:
            return f"error after {changes} changes: rewrite would not parse"
        source, changes = new, changes + 1
        if changes > 500:
            return "error: too many rewrites"
    if changes and apply:
        with open(path, "w") as fh:
            fh.write(source)
    return changes


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    files = tracked_files()
    vendored = vendored_dirs(files)
    names = re.compile(r"(^|/)(moz\.build|[^/]+\.mozbuild|[^/]+\.build)$")
    candidates = [f for f in files if names.search(f) and not f.startswith(vendored)]
    changed, total, problems = 0, 0, []
    for f in candidates:
        r = process(f, args.apply)
        if isinstance(r, int):
            if r:
                changed += 1
                total += r
                if args.list:
                    print(f"{r}\t{f}")
        else:
            problems.append((f, r))
    for f, r in problems:
        print(f"{f}: {r}", file=sys.stderr)
    print(
        f"{len(candidates)} files, {changed} {'changed' if args.apply else 'would change'}"
        f" ({total} conditions), {len(problems)} skipped",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
