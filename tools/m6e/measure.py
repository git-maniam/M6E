# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Measure startup time, memory and idle network activity of a browser build.

Run with the mach Python environment, which provides marionette_driver:

    ./mach python tools/m6e/measure.py --binary <app>/Contents/MacOS/<exe> --out <dir>

Writes results.json and summary.md into --out.
"""

import argparse
import glob
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import tempfile
import time

from marionette_driver.marionette import Marionette

SITES = [
    "https://www.google.com/",
    "https://www.wikipedia.org/",
    "https://github.com/",
    "https://www.bbc.com/",
    "https://www.youtube.com/",
]

STARTUP_JS = """
const info = Services.startup.getStartupInfo();
const out = {};
for (const key in info) {
  if (info[key]) {
    out[key] = info[key].getTime();
  }
}
return out;
"""

MEMORY_JS = """
const resolve = arguments[arguments.length - 1];
const mgr = Cc["@mozilla.org/memory-reporter-manager;1"].getService(
  Ci.nsIMemoryReporterManager
);
mgr.minimizeMemoryUsage(async () => {
  const procs = await ChromeUtils.requestProcInfo();
  const total = procs.children.reduce((sum, p) => sum + p.memory, procs.memory);
  resolve({
    parentExplicit: mgr.explicit,
    parentResident: mgr.resident,
    allProcessesMemory: total,
    processCount: procs.children.length + 1,
  });
});
"""


def use_chrome(client):
    client.set_context(client.CONTEXT_CHROME)
    client.switch_to_window(client.chrome_window_handles[0])


def use_content(client):
    client.set_context(client.CONTEXT_CONTENT)
    client.switch_to_window(client.window_handles[0])


def measure_startup(client, runs):
    """Restart in place `runs` times, so every sample is a warm start on the same profile."""
    samples = []
    for _ in range(runs):
        client.restart(in_app=True)
        time.sleep(5)
        use_chrome(client)
        info = client.execute_script(STARTUP_JS)
        start = info["process"]
        samples.append({k: v - start for k, v in info.items() if k != "process"})
    keys = sorted(set().union(*samples))
    return {
        "runs": samples,
        "median_ms": {
            k: statistics.median(s[k] for s in samples if k in s) for k in keys
        },
    }


def measure_memory(client):
    result = {}
    client.restart(in_app=True)
    time.sleep(15)
    use_chrome(client)
    result["blank"] = client.execute_async_script(MEMORY_JS, script_timeout=120000)
    use_content(client)
    for i, url in enumerate(SITES):
        if i:
            client.open(type="tab", focus=True)
            client.switch_to_window(client.window_handles[-1])
        try:
            client.navigate(url)
        except Exception as e:
            result.setdefault("navigation_errors", {})[url] = str(e)[:200]
    time.sleep(15)
    use_chrome(client)
    result["five_sites"] = client.execute_async_script(
        MEMORY_JS, script_timeout=120000
    )
    return result


RESOLVED_HOST = re.compile(r"Resolving host \[([^\]\s]+)\]")
REQUESTED_HOST = re.compile(r"uri=https?://([^/:\s]+)")


def parse_network_logs(log_prefix):
    """Return hosts with HTTP requests, and hosts only looked up over DNS (prefetch)."""
    resolved, requested = {}, {}
    for path in glob.glob(log_prefix + "*"):
        with open(path, errors="replace") as fh:
            for line in fh:
                for pattern, hosts in ((RESOLVED_HOST, resolved), (REQUESTED_HOST, requested)):
                    for host in pattern.findall(line):
                        host = host.lower().rstrip(".")
                        hosts[host] = hosts.get(host, 0) + 1
    return {
        "http_requests": dict(sorted(requested.items())),
        "dns_only": sorted(set(resolved) - set(requested)),
    }


def measure_idle_network(binary, seconds, out_dir):
    """Run a plain (non-Marionette) instance on a fresh profile and log every host it contacts.

    Marionette sets test prefs that disable update, Safe Browsing and telemetry
    traffic, so it must not be used here.
    """
    profile = tempfile.mkdtemp(prefix="m6e-net-")
    log_prefix = os.path.join(out_dir, "net-idle.log")
    env = dict(
        os.environ,
        MOZ_LOG="timestamp,nsHostResolver:5,nsHttp:3",
        MOZ_LOG_FILE=log_prefix,
    )
    proc = subprocess.Popen(
        [binary, "-no-remote", "-profile", profile, "--headless"],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(seconds)
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(profile, ignore_errors=True)

    return {"seconds": seconds, **parse_network_logs(log_prefix)}


def write_summary(results, path):
    s = results["startup"]["median_ms"]
    lines = [
        "# Measurement summary",
        "",
        f"Binary: `{results['binary']}`",
        "",
        "## Startup (median of warm runs, ms after process start)",
        "",
    ]
    lines += [f"- {k}: {v:.0f}" for k, v in s.items()]
    lines += ["", "## Memory (MB, after minimizeMemoryUsage)", ""]
    for label, m in results["memory"].items():
        if label == "navigation_errors":
            continue
        mb = {k: "n/a" if v is None else f"{v / 1e6:.0f}" for k, v in m.items()}
        lines.append(
            f"- {label}: all processes {mb['allProcessesMemory']}"
            f" ({m['processCount']} processes), parent explicit"
            f" {mb['parentExplicit']}, parent resident {mb['parentResident']}"
        )
    for url, err in results["memory"].get("navigation_errors", {}).items():
        lines.append(f"- navigation error {url}: {err}")
    net = results["network"]
    lines += [
        "",
        f"## Network: fresh profile idle for {net['seconds']} s (headless)",
        "",
        f"### Hosts with HTTP requests ({len(net['http_requests'])})",
        "",
    ]
    lines += [f"- {h} ({n})" for h, n in net["http_requests"].items()] or ["- none"]
    lines += ["", f"### Hosts only looked up over DNS ({len(net['dns_only'])})", ""]
    lines += [f"- {h}" for h in net["dns_only"]] or ["- none"]
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--startup-runs", type=int, default=3)
    parser.add_argument("--idle-seconds", type=int, default=300)
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    client = Marionette(bin=args.binary, startup_timeout=120)
    client.start_session()
    try:
        results = {
            "binary": args.binary,
            "startup": measure_startup(client, args.startup_runs),
            "memory": measure_memory(client),
        }
    finally:
        client.quit(in_app=True)
        client.cleanup()
    results["network"] = measure_idle_network(
        args.binary, args.idle_seconds, args.out
    )

    with open(os.path.join(args.out, "results.json"), "w") as fh:
        json.dump(results, fh, indent=2)
    write_summary(results, os.path.join(args.out, "summary.md"))
    print(open(os.path.join(args.out, "summary.md")).read())


if __name__ == "__main__":
    main()
