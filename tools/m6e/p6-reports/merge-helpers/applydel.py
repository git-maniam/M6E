# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Apply the deletions the branch made (base->theirs) to HEAD, using whole-file alignment.
usage: applydel.py BASE THEIRS_REF path... [--write]"""
import difflib, subprocess, sys
base_ref, their_ref = sys.argv[1], sys.argv[2]
write = '--write' in sys.argv
paths = [a for a in sys.argv[3:] if a != '--write']
def show(ref, p):
    return subprocess.run(['git', 'show', f'{ref}:{p}'], capture_output=True, text=True).stdout.splitlines(True)
for p in paths:
    base, ours, theirs = show(base_ref, p), show('HEAD', p), show(their_ref, p)
    sm = difflib.SequenceMatcher(None, base, theirs, autojunk=False)
    deleted = set(); other = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'delete':
            deleted.update(range(i1, i2))
        elif tag != 'equal':
            other.append((tag, i1, i2, j1, j2))
            deleted.update(range(i1, i2))
    # map base index -> ours index
    sm2 = difflib.SequenceMatcher(None, base, ours, autojunk=False)
    b2o = {}
    for tag, i1, i2, j1, j2 in sm2.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                b2o[i1 + k] = j1 + k
    drop = {b2o[i] for i in deleted if i in b2o}
    res = [l for k, l in enumerate(ours) if k not in drop]
    print(f"{p}: theirs non-delete ops {len(other)}; dropped {len(drop)} of {len(ours)} lines")
    for o in other[:6]:
        tag, i1, i2, j1, j2 = o
        print("   ", tag, "BASE", [l.rstrip()[:70] for l in base[i1:i2][:3]], "->", [l.rstrip()[:70] for l in theirs[j1:j2][:3]])
    if write:
        open(p, 'w').write(''.join(res))
