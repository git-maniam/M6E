#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Replace Firefox's fox mascot and logo artwork with the M6E icon.
#
#   tools/m6e/branding/replace-fox-art.sh
#
# Every image listed in fox-art.txt keeps its name, so nothing that refers to
# it changes. An SVG becomes the 256 px icon embedded and centred in its
# original viewBox; a PNG or GIF is redrawn at its original pixel size. Rerun
# it after upstream merges, and add new fox artwork to fox-art.txt.

set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../../.." && pwd)"
icon="$top/browser/branding/m6e/default256.png"
cd "$top"

grep -v '^#' "$here/fox-art.txt" | while read -r path; do
  [ -n "$path" ] || continue
  case "$path" in
    *.svg) python3 - "$path" "$icon" <<'PY'
import base64, re, sys

path, icon = sys.argv[1], sys.argv[2]
root = re.search(r"<svg\b[^>]*>", open(path, encoding="utf-8").read(), re.S).group(0)
attr = lambda name: (re.search(rf'\s{name}="([^"]*)"', root) or [None, None])[1]
width, height, view_box = attr("width"), attr("height"), attr("viewBox")
if not view_box:
    view_box = f"0 0 {float(width or 16):g} {float(height or 16):g}"
x, y, w, h = (float(v) for v in view_box.replace(",", " ").split())
side = min(w, h)
x, y = x + (w - side) / 2, y + (h - side) / 2
size = "".join(f' {k}="{v}"' for k, v in (("width", width), ("height", height)) if v)
data = base64.b64encode(open(icon, "rb").read()).decode()
open(path, "w", encoding="utf-8").write(
    "<!-- This Source Code Form is subject to the terms of the Mozilla Public\n"
    "   - License, v. 2.0. If a copy of the MPL was not distributed with this\n"
    "   - file, You can obtain one at http://mozilla.org/MPL/2.0/. -->\n"
    f'<svg xmlns="http://www.w3.org/2000/svg"{size} viewBox="{view_box}">'
    f'<image x="{x:g}" y="{y:g}" width="{side:g}" height="{side:g}" '
    f'href="data:image/png;base64,{data}"/>'
    "</svg>\n"
)
PY
      ;;
    *.png | *.gif) swift "$here/icon-tool.swift" fit "$icon" "$path" ;;
    *) echo "skipping $path: unsupported format" >&2 ;;
  esac
  echo "replaced $path"
done
