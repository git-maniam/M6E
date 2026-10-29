# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess, os, difflib
files=[l.strip() for l in open('artifacts/p6/integ/ml-conflicts.txt')]
def show(ref,p): return subprocess.run(['git','show',f'{ref}:{p}'],capture_output=True,text=True).stdout.splitlines(True)
for p in files:
    base,ours,theirs=show('2cd68653d2',p),show('HEAD',p),show('m6e-p6/ml',p)
    sm=difflib.SequenceMatcher(None,base,theirs,autojunk=False)
    deleted=set(); nonpure=False
    for tag,i1,i2,j1,j2 in sm.get_opcodes():
        if tag!='equal': deleted.update(range(i1,i2))
        if tag not in ('equal','delete'): nonpure=True
    sm2=difflib.SequenceMatcher(None,base,ours,autojunk=False)
    b2o={}
    for tag,i1,i2,j1,j2 in sm2.get_opcodes():
        if tag=='equal':
            for k in range(i2-i1): b2o[i1+k]=j1+k
    drop={b2o[i] for i in deleted if i in b2o}
    res=''.join(l for k,l in enumerate(ours) if k not in drop)
    cur=open(p).read()
    if res!=cur:
        os.makedirs(os.path.dirname('/tmp/ml_ad/'+p),exist_ok=True)
        open('/tmp/ml_ad/'+p,'w').write(res)
        print(('NONPURE ' if nonpure else 'pure    ')+p)
