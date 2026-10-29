# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re, sys
pat = re.compile(r'<<<<<<< [^\n]*\n(.*?)\|\|\|\|\|\|\| [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', re.S)
for f in sys.argv[1:]:
    s = open(f).read()
    left = []
    def rep(m):
        if m.group(3).strip() == '':
            return ''
        left.append(m.group(0)[:0])
        return m.group(0)
    s2 = pat.sub(rep, s)
    open(f, 'w').write(s2)
    print(f"{len(left)} left  {f}")
