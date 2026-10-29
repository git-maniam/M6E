# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Merge a CSS file where HEAD and the branch both only removed/edited top-level rules.
usage: css_merge.py BASE THEIRS path [--write]"""
import re, subprocess, sys
from collections import defaultdict
base_ref, their_ref, path = sys.argv[1:4]
def show(ref):
    return subprocess.run(['git', 'show', f'{ref}:{path}'], capture_output=True, text=True).stdout
def chunks(text):
    out, buf, depth, i = [], '', 0, 0
    in_comment = False
    lines = text.split('\n')
    cur = []
    for l in lines:
        cur.append(l)
        if not in_comment and '/*' in l and '*/' not in l.split('/*', 1)[1]:
            in_comment = True
        elif in_comment and '*/' in l:
            in_comment = False
        if in_comment:
            continue
        code = re.sub(r'/\*.*?\*/', '', l)
        code = re.sub(r'"[^"]*"|\'[^\']*\'', '', code)
        depth += code.count('{') - code.count('}')
        if depth == 0 and (code.strip().endswith('}') or code.strip().endswith(';') or l.strip() == ''):
            if l.strip() == '' and len(cur) == 1:
                out.append('\n'.join(cur)); cur = []
            elif code.strip().endswith('}') or code.strip().endswith(';'):
                out.append('\n'.join(cur)); cur = []
    if cur:
        out.append('\n'.join(cur))
    return out
def key(chunk):
    body = re.sub(r'/\*.*?\*/', '', chunk, flags=re.S)
    sel = body.split('{', 1)[0].strip() if '{' in body else body.strip()
    sel = re.sub(r'\s+', ' ', sel)
    return sel
def body(c):
    c = re.sub(r'/\*.*?\*/', '', c, flags=re.S)
    return re.sub(r'\s+', ' ', c.split('{', 1)[1]) if '{' in c else ''
def keyed(cs):
    seen = defaultdict(int); res = {}
    for c in cs:
        k = key(c)
        if not k: continue
        res[(k, seen[k])] = c; seen[k] += 1
    return res
B, T, H = chunks(show(base_ref)), chunks(show(their_ref)), chunks(show('HEAD'))
kb, kt = keyed(B), keyed(T)
def sels(c):
    return {x.strip() for x in key(c).split(',') if x.strip()}
out = []; manual = []
for c in H:
    k = key(c)
    if not k or '{' not in re.sub(r'/\*.*?\*/', '', c, flags=re.S):
        out.append(c); continue
    bc, sc = body(c), sels(c)
    preserved = any(body(b) == bc and sels(b) >= sc for b in B if key(b))
    if not preserved:
        out.append(c); continue
    ts = [t for t in T if key(t) and body(t) == bc and sels(t) & sc]
    if not ts:
        continue
    keep = set().union(*[sels(t) for t in ts])
    if sc <= keep:
        out.append(c); continue
    head, rest = c.split('{', 1)
    lead = re.match(r'(\s*(?:/\*.*?\*/\s*)*)', head, re.S).group(1)
    selpart = head[len(lead):]
    parts = [x.strip() for x in selpart.split(',')]
    parts = [x for x in parts if x in keep]
    out.append(lead + ',\n'.join(parts) + ' {' + rest)
res = '\n'.join(out)
res = re.sub(r'\n{3,}', '\n\n', res)
print(f"{len(H)} HEAD chunks -> {len(out)}; both-edited (kept HEAD): {manual}")
depth = 0
for l in res.split('\n'):
    depth += l.count('{') - l.count('}')
print('final brace depth', depth)
if '--write' in sys.argv:
    open(path, 'w').write(res)
