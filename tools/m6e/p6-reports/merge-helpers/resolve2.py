# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Resolve diff3 hunks where both sides only delete lines, aligning with difflib and context anchors."""
import difflib, re, sys
pat = re.compile(r'<<<<<<< [^\n]*\n(.*?)\|\|\|\|\|\|\| [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', re.S)
K = 4

def kept_mask(base, side):
    sm = difflib.SequenceMatcher(None, base, side, autojunk=False)
    mask = [False] * len(base)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for i in range(i1, i2):
                mask[i] = True
        elif tag != 'delete':
            return None
    return mask

from collections import Counter

def subseq_mask(base, side):
    mask = [False] * len(base); j = 0
    for i, l in enumerate(base):
        if j < len(side) and base[i] == side[j]:
            mask[i] = True; j += 1
    return mask if j == len(side) else None

def greedy_ok(base, ours, theirs):
    mo, mt = subseq_mask(base, ours), subseq_mask(base, theirs)
    if mo is None or mt is None:
        return None
    res = [l for l, a, c in zip(base, mo, mt) if a and c]
    cb, co, ct, cr = Counter(base), Counter(ours), Counter(theirs), Counter(res)
    for l in cb:
        hi = min(co[l], ct[l])
        lo = max(0, co[l] + ct[l] - cb[l])
        if not (lo <= cr[l] <= hi):
            return None
    return res

def resolve(path):
    s = open(path).read()
    done = left = 0
    out = []
    pos = 0
    for m in pat.finditer(s):
        before = s[pos:m.start()].splitlines(True)[-K:]
        after_text = s[m.end():]
        after = after_text.splitlines(True)[:K]
        ours, base, theirs = (m.group(i).splitlines(True) for i in (1, 2, 3))
        b = before + base + after
        masks = []
        for side in (ours, theirs):
            mk = kept_mask(b, before + side + after)
            masks.append(mk)
        ok = all(mk is not None for mk in masks)
        if ok:
            lo, hi = len(before), len(before) + len(base)
            ok = all(all(mk[i] for i in list(range(lo)) + list(range(hi, len(b)))) for mk in masks)
        out.append(s[pos:m.start()])
        if ok:
            lo, hi = len(before), len(before) + len(base)
            out.append(''.join(l for k, l in enumerate(base) if masks[0][lo + k] and masks[1][lo + k]))
            done += 1
        else:
            g = greedy_ok(base, ours, theirs)
            if g is not None:
                out.append(''.join(g))
                done += 1
            else:
                out.append(m.group(0))
                left += 1
        pos = m.end()
    out.append(s[pos:])
    open(path, 'w').write(''.join(out))
    return done, left

for f in sys.argv[1:]:
    d, l = resolve(f)
    print(f"{d} resolved, {l} left  {f}")
