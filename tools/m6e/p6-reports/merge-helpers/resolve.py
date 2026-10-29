# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""usage: resolve.py file idx=<text> ... ; idx is the 0-based remaining hunk number; text 'HEAD', 'THEIRS', 'BASE', 'NONE' or '@file' contents"""
import re, sys
pat = re.compile(r'<<<<<<< [^\n]*\n(.*?)\|\|\|\|\|\|\| [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', re.S)
f = sys.argv[1]
choices = {}
for a in sys.argv[2:]:
    k, v = a.split('=', 1)
    choices[int(k)] = v
s = open(f).read()
n = [0]
def rep(m):
    i = n[0]; n[0] += 1
    c = choices.get(i)
    if c is None:
        return m.group(0)
    if c == 'HEAD': return m.group(1)
    if c == 'THEIRS': return m.group(3)
    if c == 'BASE': return m.group(2)
    if c == 'NONE': return ''
    if c == 'BOTH': return m.group(3) + m.group(1)
    if c.startswith('@'): return open(c[1:]).read()
    raise SystemExit('bad choice ' + c)
open(f, 'w').write(pat.sub(rep, s))
