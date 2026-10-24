# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Capture startup and interaction Gecko profiles of a browser build and summarise them.

Run with the mach Python environment, which provides marionette_driver:

    ./mach python tools/m6e/profile.py --binary <app>/Contents/MacOS/<exe> --out <dir>

Writes startup.json.gz and interaction.json.gz (load them in profiler.firefox.com)
and summary.md, which ranks the parent process main thread's functions and JS
modules by sample count.
"""

import argparse
import bisect
import gzip
import json
import os
import re
import subprocess
import tempfile
import time
from collections import Counter

from marionette_driver import Wait
from marionette_driver.by import By
from marionette_driver.errors import TimeoutException
from marionette_driver.keys import Keys
from marionette_driver.marionette import Marionette

PAGES = ["https://www.wikipedia.org/", "https://github.com/"]
FEATURES = ["js", "stackwalk", "screenshots", "processcpu"]
THREADS = ["GeckoMain", "Compositor", "Renderer", "DOM Worker"]
ENTRIES = 16 * 1024 * 1024
INTERVAL_MS = 1
TOP = 40
URLBAR_TEXT = "wikipedia"
JS_MODULE = re.compile(r"^(resource|chrome|moz-src)://")

STARTUP_ENV = {
    "MOZ_PROFILER_STARTUP": "1",
    "MOZ_PROFILER_STARTUP_INTERVAL": str(INTERVAL_MS),
    "MOZ_PROFILER_STARTUP_FEATURES": ",".join(FEATURES),
    "MOZ_PROFILER_STARTUP_FILTERS": ",".join(THREADS),
}

ASYNC_WRAPPER = """
const resolve = arguments[arguments.length - 1];
const args = arguments;
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
%s
})().then(resolve, e => resolve({ error: String(e), stack: e.stack }));
"""

WAIT_STARTUP_JS = """
const [seconds] = args;
if (!Services.profiler.IsActive()) {
  return { error: "the startup profiler is not running" };
}
const start = Services.startup.getStartupInfo().process.getTime();
await sleep(Math.max(0, start + seconds * 1000 - Date.now()));
return {};
"""

START_JS = """
const [entries, interval, features, filters] = args;
await Services.profiler.StartProfiler(entries, interval, features, filters);
return {};
"""

COLLECT_JS = """
const [path] = args;
const { profile } = await Services.profiler.getProfileDataAsGzippedArrayBuffer();
await IOUtils.write(path, new Uint8Array(profile));
await Services.profiler.StopProfiler();
return { bytes: profile.byteLength };
"""

LOAD_PAGES_JS = """
const [pages, settleMs] = args;
const gBrowser = window.gBrowser;
const { STATE_STOP, STATE_IS_NETWORK } = Ci.nsIWebProgressListener;
function loaded(browser) {
  let listener;
  const done = new Promise(r => {
    listener = {
      onStateChange(b, webProgress, request, flags) {
        if (
          b == browser &&
          webProgress.isTopLevel &&
          flags & STATE_STOP &&
          flags & STATE_IS_NETWORK &&
          browser.currentURI.spec != "about:blank"
        ) {
          r(true);
        }
      },
    };
    gBrowser.addTabsProgressListener(listener);
  });
  return Promise.race([done, sleep(30000).then(() => false)]).finally(() =>
    gBrowser.removeTabsProgressListener(listener)
  );
}
const tab = gBrowser.addTrustedTab("about:blank");
gBrowser.selectedTab = tab;
const browser = tab.linkedBrowser;
const errors = {};
for (const url of pages) {
  const done = loaded(browser);
  browser.fixupAndLoadURIString(url, {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });
  if (!(await done)) {
    errors[url] = "did not finish loading within 30 s";
  }
  await sleep(settleMs);
}
return { errors };
"""

PANEL_JS = """
const [holdMs] = args;
const doc = window.document;
const button = doc.getElementById("m6e-page-telemetry-button");
const panel = doc.getElementById("m6e-page-telemetry-panel");
if (!button || !panel) {
  return { error: "the page telemetry button or panel is missing" };
}
const shown = new Promise(r => panel.addEventListener("popupshown", r, { once: true }));
button.click();
if (!(await Promise.race([shown.then(() => true), sleep(5000).then(() => false)]))) {
  return { error: "the page telemetry panel did not open" };
}
await sleep(holdMs);
const hidden = new Promise(r => panel.addEventListener("popuphidden", r, { once: true }));
panel.hidePopup();
await hidden;
return {};
"""


def use_chrome(client):
    client.set_context(client.CONTEXT_CHROME)
    client.switch_to_window(client.chrome_window_handles[0])


def run_js(client, body, *args, timeout=120):
    result = client.execute_async_script(
        ASYNC_WRAPPER % body, script_args=args, script_timeout=timeout * 1000
    )
    if isinstance(result, dict) and "error" in result:
        raise RuntimeError(f"{result['error']}\n{result.get('stack', '')}")
    return result


def collect(client, path):
    return run_js(client, COLLECT_JS, os.path.abspath(path), timeout=300)


def profile_startup(client, seconds, path):
    """Restart in place so the profiled start is warm; the restarted process
    inherits the MOZ_PROFILER_STARTUP* environment set before launch."""
    client.restart(in_app=True)
    use_chrome(client)
    run_js(client, WAIT_STARTUP_JS, seconds, timeout=seconds + 120)
    return collect(client, path)


def profile_interaction(client, pages, path):
    use_chrome(client)
    run_js(client, START_JS, ENTRIES, INTERVAL_MS, FEATURES, THREADS)
    result = run_js(client, LOAD_PAGES_JS, pages, 2000, timeout=60 * len(pages) + 60)

    client.execute_script("window.gURLBar.focus(); window.gURLBar.select();")
    urlbar = client.find_element(By.ID, "urlbar-input")
    urlbar.send_keys(URLBAR_TEXT)
    try:
        Wait(client, timeout=10).until(
            lambda m: m.execute_script("return window.gURLBar.view.isOpen;")
        )
        time.sleep(1)
        result["urlbar_opened"] = True
    except TimeoutException:
        result["urlbar_opened"] = False
    urlbar.send_keys(Keys.ESCAPE)
    urlbar.send_keys(Keys.ESCAPE)
    client.execute_script("window.gBrowser.selectedBrowser.focus();")

    run_js(client, PANEL_JS, 2500)
    time.sleep(1)
    result.update(collect(client, path))
    return result


def columns(table):
    """Return (columns by field name, row count) for the Gecko array-of-rows
    form ({schema, data}) or the processed columnar form ({field: [...], length})."""
    if "schema" in table:
        data = table["data"]
        cols = {
            name: [row[i] if i < len(row) else None for row in data]
            for name, i in table["schema"].items()
        }
        return cols, len(data)
    length = table.get("length")
    if length is None:
        length = max((len(v) for v in table.values() if isinstance(v, list)), default=0)
    return table, length


def column(cols, name, length):
    return cols.get(name) or [None] * length


JS_LABEL = re.compile(
    r"^(?:(?P<name>.*) \((?P<url>.+):(?P<line>\d+):\d+\)|(?P<url2>.+):(?P<line2>\d+):\d+)$"
)
SOURCE_INDEX = re.compile(r"\[\d+\]$")


def js_url(url):
    return re.sub(r" line \d+ > .*$", "", url.split(" -> ")[-1])


def parse_js_label(label):
    m = JS_LABEL.match(SOURCE_INDEX.sub("", label))
    if not m:
        return None
    if m["url"]:
        return m["name"] or "(anonymous)", m["url"], int(m["line"])
    if "://" not in m["url2"] and not m["url2"].startswith("self-hosted"):
        return None
    return "(top-level)", m["url2"], int(m["line2"])


def symbolicate(addresses, libs):
    """Map native addresses to function names with atos, falling back to [library]."""
    libs = sorted(libs, key=lambda lib: lib["start"])
    starts = [lib["start"] for lib in libs]
    by_lib = {}
    names = {}
    for addr in addresses:
        i = bisect.bisect_right(starts, addr) - 1
        if i < 0 or addr >= libs[i]["end"]:
            names[addr] = "[unknown]"
            continue
        by_lib.setdefault(i, []).append(addr)
    for i, addrs in by_lib.items():
        lib = libs[i]
        fallback = f"[{lib['name']}]"
        for addr in addrs:
            names[addr] = fallback
        if not os.path.exists(lib["path"]):
            continue
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as fh:
            fh.write("\n".join(hex(a) for a in addrs))
        try:
            out = subprocess.run(
                [
                    "atos",
                    "-o",
                    lib["path"],
                    "-arch",
                    "arm64",
                    "-l",
                    hex(lib["start"]),
                    "-f",
                    fh.name,
                ],
                capture_output=True,
                check=False,
                text=True,
                timeout=900,
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        finally:
            os.unlink(fh.name)
        lines = out.stdout.splitlines()
        if out.returncode or len(lines) != len(addrs):
            continue
        for addr, line in zip(addrs, lines):
            symbol = line.strip()
            if symbol and not symbol.startswith("0x"):
                names[addr] = re.sub(r" \(in [^)]*\).*$", "", symbol)
    return names


class Thread:
    """A thread reduced to frames (key, name, where, module, is_js, category), stacks and sample weights."""

    def __init__(self, profile, thread):
        stacks, nstacks = columns(thread["stackTable"])
        self.prefix = column(stacks, "prefix", nstacks)
        self.frame = column(stacks, "frame", nstacks)
        samples, nsamples = columns(thread["samples"])
        weights = column(samples, "weight", nsamples)
        self.samples = Counter()
        for stack, weight in zip(column(samples, "stack", nsamples), weights):
            self.samples[stack] += 1 if weight is None else weight
        if "funcTable" in thread:
            self.frames = self._processed_frames(profile, thread)
        else:
            self.frames = self._gecko_frames(profile, thread)
        self.categories = [
            c["name"] for c in profile.get("meta", {}).get("categories", [])
        ]
        self.stack_category = self._inherit_categories(
            column(stacks, "category", nstacks)
        )

    @staticmethod
    def _frame(name, url, line, category):
        url = js_url(url)
        module = url if JS_MODULE.match(url) else None
        return (f"{name} ({url}:{line})", name, f"{url}:{line}", module, True, category)

    def _gecko_frames(self, profile, thread):
        strings = thread["stringTable"]
        cols, n = columns(thread["frameTable"])
        locations = [strings[i] for i in cols["location"]]
        categories = column(cols, "category", n)
        addresses = {int(loc, 16) for loc in locations if loc.startswith("0x")}
        names = symbolicate(addresses, profile.get("libs", [])) if addresses else {}
        frames = []
        for loc, category in zip(locations, categories):
            js = parse_js_label(loc)
            if js:
                frames.append(self._frame(*js, category))
            elif loc.startswith("0x"):
                name = names[int(loc, 16)]
                frames.append((name, name, "native", None, False, category))
            else:
                frames.append((loc, loc, "label", None, False, category))
        return frames

    def _processed_frames(self, profile, thread):
        strings = (
            thread.get("stringArray")
            or profile.get("shared", {}).get("stringArray")
            or thread.get("stringTable")
        )
        cols, n = columns(thread["frameTable"])
        funcs, nfuncs = columns(thread["funcTable"])
        func_name = funcs["name"]
        is_js = column(funcs, "isJS", nfuncs)
        file_name = column(funcs, "fileName", nfuncs)
        line_number = column(funcs, "lineNumber", nfuncs)
        frames = []
        for func, category in zip(cols["func"], column(cols, "category", n)):
            name = strings[func_name[func]]
            if is_js[func] and file_name[func] is not None:
                frames.append(
                    self._frame(
                        name, strings[file_name[func]], line_number[func], category
                    )
                )
            else:
                frames.append((name, name, "native", None, False, category))
        return frames

    def _inherit_categories(self, stack_categories):
        result = [None] * len(self.frame)
        for i in range(len(self.frame)):
            own = stack_categories[i]
            if own is None:
                own = self.frames[self.frame[i]][5]
            if own is None and self.prefix[i] is not None:
                own = result[self.prefix[i]] if self.prefix[i] < i else None
            result[i] = own
        return result

    def category_name(self, stack):
        c = self.stack_category[stack]
        return (
            self.categories[c]
            if c is not None and c < len(self.categories)
            else "Other"
        )


def find_parent_main(profile):
    threads = profile["threads"]
    for t in threads:
        if t.get("name") == "GeckoMain" and t.get("processType") in ("default", None):
            return t
    raise RuntimeError("no parent process GeckoMain thread in the profile")


def analyse(path):
    with gzip.open(path, "rt", encoding="utf-8") as fh:
        profile = json.load(fh)
    thread = Thread(profile, find_parent_main(profile))
    pre_xul = sum(
        columns(t["samples"])[1]
        for t in profile["threads"]
        if t.get("name") == "GeckoMain (pre-xul)"
    )
    r = {
        "self": Counter(),
        "running": Counter(),
        "module_running": Counter(),
        "module_self": Counter(),
        "categories": Counter(),
        "frames": {f[0]: f for f in thread.frames},
        "total": 0,
        "idle": 0,
        "no_stack": 0,
        "js_leaf": 0,
        "js_any": 0,
        "pre_xul": pre_xul,
    }
    for stack, weight in thread.samples.items():
        r["total"] += weight
        if stack is None:
            r["no_stack"] += weight
            continue
        category = thread.category_name(stack)
        if category == "Idle":
            r["idle"] += weight
            continue
        r["categories"][category] += weight
        leaf = thread.frames[thread.frame[stack]]
        r["self"][leaf[0]] += weight
        if leaf[4]:
            r["js_leaf"] += weight
        keys, modules, nearest_js = set(), set(), None
        s = stack
        while s is not None:
            key, _, _, module, is_js, _ = thread.frames[thread.frame[s]]
            keys.add(key)
            if module:
                modules.add(module)
            if is_js and nearest_js is None:
                nearest_js = module or ""
            s = thread.prefix[s]
        for key in keys:
            r["running"][key] += weight
        for module in modules:
            r["module_running"][module] += weight
        if nearest_js is not None:
            r["js_any"] += weight
            if nearest_js:
                r["module_self"][nearest_js] += weight
    return r


def md(text):
    return str(text).replace("|", "\\|")


def pct(n, total):
    return f"{100 * n / total:.1f}%" if total else "n/a"


def summary_section(title, r):
    busy = r["total"] - r["idle"] - r["no_stack"]
    lines = [
        f"## {title}",
        "",
        "Parent process main thread (GeckoMain). Percentages are of non-idle samples.",
        "",
        f"- Samples: {r['total']} ({r['idle']} idle, {r['no_stack']} without a stack, {busy} busy, ~{INTERVAL_MS} ms each)",
        f"- Leaf frame in JS: {pct(r['js_leaf'], busy)}; native: {pct(busy - r['js_leaf'], busy)}",
        f"- Samples with JS anywhere on the stack: {pct(r['js_any'], busy)}",
    ]
    if r["pre_xul"]:
        lines.append(
            f"- Samples before XUL loaded (GeckoMain (pre-xul), not ranked): {r['pre_xul']}"
        )
    lines.append(
        "- Categories: "
        + ", ".join(
            f"{name} {pct(n, busy)}" for name, n in r["categories"].most_common()
        )
    )
    for label, counter in (("self", r["self"]), ("running (inclusive)", r["running"])):
        lines += [
            "",
            f"### Top {TOP} functions by {label} samples",
            "",
            "| # | Samples | % | Function | Where |",
            "|---|---|---|---|---|",
        ]
        for i, (key, n) in enumerate(counter.most_common(TOP), 1):
            _, name, where = r["frames"][key][:3]
            lines.append(f"| {i} | {n} | {pct(n, busy)} | `{md(name)}` | {md(where)} |")
    lines += [
        "",
        f"### Top {TOP} JS modules by inclusive samples",
        "",
        "Self counts samples whose nearest JS frame is in the module, native callees included."
        " chrome://remote/ is the Marionette harness.",
        "",
        "| # | Inclusive | % | Self | % | Module |",
        "|---|---|---|---|---|---|",
    ]
    for i, (module, n) in enumerate(r["module_running"].most_common(TOP), 1):
        s = r["module_self"][module]
        lines.append(
            f"| {i} | {n} | {pct(n, busy)} | {s} | {pct(s, busy)} | {md(module)} |"
        )
    return lines + [""]


def write_summary(args, results, path):
    lines = [
        "# Profile summary",
        "",
        f"Binary: `{args.binary}`",
        "",
        f"- Startup: warm start (after one in-app restart), first {args.startup_seconds} s after process start; `startup.json.gz`",
        f"- Interaction: new tab, {', '.join(args.pages)}, address bar typed and closed"
        f" (dropdown opened: {results['interaction']['urlbar_opened']}), page telemetry panel opened;"
        " `interaction.json.gz`",
        f"- Features: {', '.join(FEATURES)}; threads: {', '.join(THREADS)}; interval {INTERVAL_MS} ms",
    ]
    for url, err in results["interaction"].get("errors", {}).items():
        lines.append(f"- Load error {url}: {err}")
    lines.append("")
    lines += summary_section("Startup", results["startup_analysis"])
    lines += summary_section("Interaction", results["interaction_analysis"])
    with open(path, "w") as fh:
        fh.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--pages", nargs="+", default=PAGES)
    parser.add_argument("--startup-seconds", type=int, default=10)
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    startup_path = os.path.join(args.out, "startup.json.gz")
    interaction_path = os.path.join(args.out, "interaction.json.gz")

    # The desktop GeckoInstance launches the browser with a copy of os.environ.
    os.environ.update(STARTUP_ENV)
    client = Marionette(bin=args.binary, startup_timeout=120)
    client.start_session()
    try:
        results = {
            "startup": profile_startup(client, args.startup_seconds, startup_path),
            "interaction": profile_interaction(client, args.pages, interaction_path),
        }
    finally:
        client.quit(in_app=True)
        client.cleanup()
        for key in STARTUP_ENV:
            os.environ.pop(key, None)

    results["startup_analysis"] = analyse(startup_path)
    results["interaction_analysis"] = analyse(interaction_path)
    summary = os.path.join(args.out, "summary.md")
    write_summary(args, results, summary)
    print(open(summary).read())


if __name__ == "__main__":
    main()
