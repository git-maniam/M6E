# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re, sys
def ext(p):
    s = set()
    for l in open(p, errors="replace"):
        l = re.sub(r"^\s*[\d:.]+\s+", "", l.rstrip("\n")).strip()
        if not l.startswith("FAIL "):
            continue
        for part in re.split(r"(?=FAIL )", l):
            part = re.sub(r"finished in \d+ms", "finished", part.strip())
            part = re.sub(r"Got \d+, expected \d+", "Got N, expected N", part)
            if part:
                s.add(part)
    return s
a, b = ext(sys.argv[1]), ext(sys.argv[2])
print(f"baseline {len(a)}  new {len(b)}  only-new {len(b-a)}  only-baseline {len(a-b)}")
for x in sorted(b - a)[: int(sys.argv[3]) if len(sys.argv) > 3 else 15]:
    print("  NEW", x[:230])
