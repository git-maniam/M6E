# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Limit the bundled search configuration to Google (default) and Bing.

    python3 tools/m6e/search/prune-search-config.py

Rewrites the Remote Settings dumps in services/settings/dumps/main, which M6E
never syncs (see Utils.isAllowedCollection):

- search-config-v2.json and search-config-v3.json keep only the google and
  bing engines, each with a single variant for every region and locale and
  without Mozilla partner or attribution parameters. Google is the default
  everywhere, there are no special engine orders and no engine names a
  locale.
- search-config-icons.json keeps the icons of those engines whose images are
  bundled; other image files are deleted and moz.build is updated.
- search-default-override-allowlist.json is emptied, so no add-on may
  replace the default engine.

Safe to run again after merging upstream changes.
"""

import json
import os
import re
import sys

ENGINES = ("google", "bing")
# Parameters that identify Firefox as a Mozilla partner or attribute traffic
# to it. Google's suggestions "client=firefox" is kept: it selects the JSON
# response format the browser parses.
ATTRIBUTION_PARAMS = {"pc", "form", "channel", "ep"}


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def save(path, data):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


def strip_params(url):
    params = [
        p
        for p in url.get("params", [])
        if p.get("name") not in ATTRIBUTION_PARAMS
        and "{partnerCode}" not in str(p.get("value", ""))
    ]
    if params:
        url["params"] = params
    else:
        url.pop("params", None)
    url.pop("excludePartnerCodeFromTelemetry", None)


def prune_engine(record):
    base = record["base"]
    base.pop("partnerCode", None)
    for url in base.get("urls", {}).values():
        strip_params(url)
    record["variants"] = [{"environment": {"allRegionsAndLocales": True}}]
    return record


def prune_config(path):
    config = load(path)
    kept = []
    for record in config["data"]:
        kind = record.get("recordType")
        if kind == "engine":
            if record["identifier"] in ENGINES:
                kept.append(prune_engine(record))
        elif kind == "defaultEngines":
            record["globalDefault"] = "google"
            record["specificDefaults"] = []
            record.pop("globalDefaultPrivate", None)
            kept.append(record)
        elif kind == "engineOrders":
            record["orders"] = []
            kept.append(record)
        elif kind == "availableLocales":
            # Lists the locales named by engine variants, and they name none.
            record["locales"] = []
            kept.append(record)
        else:
            kept.append(record)
    config["data"] = kept
    save(path, config)
    engines = [r["identifier"] for r in kept if r.get("recordType") == "engine"]
    print(f"{os.path.basename(path)}: engines {engines}")


def prune_icons(dumps):
    path = os.path.join(dumps, "search-config-icons.json")
    folder = os.path.join(dumps, "search-config-icons")
    icons = load(path)
    bundled = {name for name in os.listdir(folder) if not name.endswith(".meta.json")}
    kept = [
        r
        for r in icons["data"]
        if set(r.get("engineIdentifiers", [])) & set(ENGINES) and r["id"] in bundled
    ]
    keep_ids = {r["id"] for r in kept}
    for name in os.listdir(folder):
        if name.split(".")[0] not in keep_ids:
            os.remove(os.path.join(folder, name))
    icons["data"] = kept
    save(path, icons)

    build = os.path.join(dumps, "moz.build")
    with open(build, encoding="utf-8") as fh:
        text = fh.read()
    entries = "".join(
        f'        "search-config-icons/{i}",\n        "search-config-icons/{i}.meta.json",\n'
        for i in sorted(keep_ids)
    )
    text, count = re.subn(
        r'(FINAL_TARGET_FILES\.defaults\.settings\.main\["search-config-icons"\] \+= \[\n)(.*?)(    \])',
        lambda m: m.group(1) + entries + m.group(3),
        text,
        flags=re.S,
    )
    if count != 1:
        sys.exit("search-config-icons list not found in moz.build")
    with open(build, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"search-config-icons: {len(kept)} icons kept")


def main():
    top = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    dumps = os.path.join(top, "services", "settings", "dumps", "main")
    for name in ("search-config-v2.json", "search-config-v3.json"):
        prune_config(os.path.join(dumps, name))
    prune_icons(dumps)
    allowlist = os.path.join(dumps, "search-default-override-allowlist.json")
    overrides = load(allowlist)
    overrides["data"] = []
    save(allowlist, overrides)
    print("search-default-override-allowlist: emptied")


if __name__ == "__main__":
    main()
