# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Observe the runtime facts behind tools/m6e/p6-reports/unreachability.md.

    ./mach python tools/m6e/unreachability_check.py --app <path to M6E.app> --out <dir> [--headless]

Starts one browser on a fresh profile through the helpers of acceptance.py
(Marionette launcher, run_js, Result, run_check), reads the state of the
dormant features that stay in the tree, holds the session idle, then lists the
listening sockets of the app's process tree. Prints a PASS/FAIL table and
writes results.json into --out. The exit status is 1 if any check fails.

The idle network capture is acceptance.py check 3 and is not repeated here.
Only one browser may use the Marionette port at a time.
  --idle-seconds  idle time before the socket snapshot (default 20)
  --only a,b      run some checks (--list names them)
  --lock-dir DIR  take DIR (mkdir/rmdir) around the browser run
  --headless      run the Marionette session headless
"""

import argparse
import datetime
import importlib.util
import json
import os
import re
import subprocess
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))

# Locked by browser/app/profile/m6e.js. Each must have this value and be locked.
LOCKED_PREFS = {
    "Mozilla accounts": {"identity.fxaccounts.enabled": False},
    "Relay": {"signon.firefoxRelay.feature": "", "browser.promo.relay.enabled": False},
    "VPN": {"browser.ipProtection.enabled": False, "browser.vpn_promo.enabled": False},
    "Nimbus and studies": {
        "app.normandy.enabled": False,
        "app.normandy.api_url": "",
        "app.shield.optoutstudies.enabled": False,
    },
    "Telemetry and pingsender": {
        "datareporting.healthreport.uploadEnabled": False,
        "datareporting.usage.uploadEnabled": False,
        "datareporting.policy.dataSubmissionEnabled": False,
        "toolkit.telemetry.enabled": False,
        "toolkit.telemetry.unified": False,
        "toolkit.telemetry.server": "",
        "toolkit.telemetry.archive.enabled": False,
        "toolkit.telemetry.shutdownPingSender.enabled": False,
    },
    "Firefox Suggest": {
        "browser.urlbar.quicksuggest.enabled": False,
        "browser.urlbar.quicksuggest.rustEnabled": False,
        "browser.urlbar.suggest.quicksuggest.sponsored": False,
        "browser.urlbar.suggest.quicksuggest.nonsponsored": False,
        "browser.urlbar.merino.endpointURL": "",
        "toolkit.contentRelevancy.enabled": False,
    },
    "New Tab ads": {
        "browser.newtabpage.activity-stream.unifiedAds.endpoint": "",
        "browser.newtabpage.activity-stream.unifiedAds.tiles.enabled": False,
        "browser.newtabpage.activity-stream.unifiedAds.spocs.enabled": False,
    },
    "AI Controls": {
        "browser.ai.control.default": "blocked",
        "browser.ai.control.linkPreviewKeyPoints": "blocked",
        "browser.ai.control.sidebarChatbot": "blocked",
        "browser.ai.control.smartWindow": "blocked",
        "browser.preferences.aiControls": False,
    },
    "wasm intgemm": {"javascript.options.wasm_moz_intgemm": False},
    "Updates": {
        "extensions.systemAddon.update.enabled": False,
        "extensions.update.enabled": False,
    },
}

# Defaults (not locks) that must be off in a fresh profile.
DEFAULT_OFF_PREFS = {"browser.backup.scheduled.enabled": False}

# Prefs of deleted features: no file defines them, so a fresh profile has no type.
ABSENT_PREFS = [
    "app.update.url",
    "browser.translations.enable",
    "browser.ml.enable",
    "browser.smartwindow.enabled",
    "browser.tabs.groups.smart.enabled",
    "browser.ipProtection.userEnabled",
    "browser.crashReports.unsubmittedCheck.enabled",
    "toolkit.crashreporter.infoURL",
    "identity.fxaccounts.remote.root",
    "identity.fxaccounts.autoconfig.uri",
    "services.sync.engine.bookmarks",
    "services.sync.username",
]

# Contract IDs of removed components. Missing ones read as undefined on Cc.
ABSENT_CONTRACTS = [
    "@mozilla.org/bits;1",
    "@mozilla.org/updates/update-checker;1",
    "@mozilla.org/updates/update-manager;1",
    "@mozilla.org/updates/update-service;1",
]
PRESENT_CONTRACTS = ["@mozilla.org/toolkit/crashmonitor;1"]

# Modules of removed features. fetch() rejects for a missing file; importing a
# missing module would abort in automation (CheckForBrokenChromeURL), so it is
# never used here. Each existed before P6.1a (git ls-tree bb2f27efbb~1).
ABSENT_RESOURCES = [
    "resource://nimbus/lib/ExperimentManager.sys.mjs",
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
    "resource://gre/modules/FxAccounts.sys.mjs",
    "resource://gre/modules/FxAccountsCommon.sys.mjs",
    "resource://services-sync/service.sys.mjs",
    "resource://gre/modules/UpdateService.sys.mjs",
    "resource://gre/modules/CrashSubmit.sys.mjs",
    "chrome://global/content/ml/EngineProcess.sys.mjs",
    "resource://gre/actors/TranslationsParent.sys.mjs",
]
# fetch() must see these, otherwise the absence results above prove nothing.
CONTROL_RESOURCES = [
    "resource://nimbus/ExperimentAPI.sys.mjs",
    "resource://gre/modules/ContentRelevancyManager.sys.mjs",
]

ABSENT_ABOUT = ["crashes", "sync-log", "inference", "signin", "accounts"]

# Process names that must not appear in the app's tree.
FORBIDDEN_PROCESS = re.compile(
    r"/(pingsender|crashreporter|crash-helper|updater|minidump[^/ ]*)(\.app)?(/|\s|$)", re.I
)
# Executables or bundles that must not ship in the app.
FORBIDDEN_BUNDLE_ITEMS = ["crashreporter.app", "updater.app", "crashhelper", "pingsender"]

JS_STATE = """
const P = Services.prefs;
const kinds = { [P.PREF_INVALID]: "invalid", [P.PREF_BOOL]: "bool", [P.PREF_INT]: "int", [P.PREF_STRING]: "string" };
const read = name => {
  const type = P.getPrefType(name);
  let value = null;
  if (type == P.PREF_BOOL) {
    value = P.getBoolPref(name);
  } else if (type == P.PREF_INT) {
    value = P.getIntPref(name);
  } else if (type == P.PREF_STRING) {
    value = P.getStringPref(name);
  }
  return { type: kinds[type], value, locked: P.prefIsLocked(name) };
};
const out = { prefs: {}, absentPrefs: {}, contracts: {}, presentContracts: {}, resources: {}, about: {} };
for (const name of __PREFS__) {
  out.prefs[name] = read(name);
}
for (const name of __ABSENT_PREFS__) {
  out.absentPrefs[name] = kinds[P.getPrefType(name)];
}
for (const id of __ABSENT_CONTRACTS__) {
  out.contracts[id] = Cc[id] !== undefined;
}
for (const id of __PRESENT_CONTRACTS__) {
  out.presentContracts[id] = Cc[id] !== undefined;
}
for (const url of [...__ABSENT_RESOURCES__, ...__CONTROL_RESOURCES__]) {
  try {
    const response = await win.fetch(url);
    out.resources[url] = "PRESENT " + response.status;
  } catch (e) {
    out.resources[url] = "absent";
  }
}
const registered = [];
for (const cid in Cc) {
  const m = cid.match(/^@mozilla\\.org\\/network\\/protocol\\/about;1\\?what=(.+)$/);
  if (m) {
    registered.push(m[1]);
  }
}
out.about = Object.fromEntries(__ABSENT_ABOUT__.map(n => [n, registered.includes(n)]));

const { ExperimentAPI, NimbusFeatures } = ChromeUtils.importESModule("resource://nimbus/ExperimentAPI.sys.mjs");
out.nimbus = {
  enabled: ExperimentAPI.enabled,
  labsEnabled: ExperimentAPI.labsEnabled,
  rolloutsEnabled: ExperimentAPI.rolloutsEnabled,
  studiesEnabled: ExperimentAPI.studiesEnabled,
  init: await ExperimentAPI.init(),
  enrollments: NimbusFeatures.urlbar.getAllEnrollments().length,
  metadata: NimbusFeatures.urlbar.getEnrollmentMetadata(),
};
const { Utils } = ChromeUtils.importESModule("resource://services-settings/Utils.sys.mjs");
out.allowlist = {
  nimbus: Utils.isAllowedCollection("main", "nimbus-desktop-experiments"),
  relayAllow: Utils.isAllowedCollection("main", "fxrelay-allowlist"),
  relayDeny: Utils.isAllowedCollection("main", "fxrelay-denylist"),
  control: Utils.isAllowedCollection("main", "query-stripping"),
};

const { QuickSuggest } = ChromeUtils.importESModule("moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs");
const { UrlbarPrefs } = ChromeUtils.importESModule("moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs");
out.suggest = {
  quickSuggestEnabled: UrlbarPrefs.get("quickSuggestEnabled"),
  enabledBackends: QuickSuggest.enabledBackends.map(b => b.name),
  enabledRustFeatures: [...QuickSuggest.rustFeatures].filter(f => f.isEnabled).map(f => f.name),
  rustBackendEnabled: !!QuickSuggest.rustBackend?.isEnabled,
};
const { ContentRelevancyManager } = ChromeUtils.importESModule("resource://gre/modules/ContentRelevancyManager.sys.mjs");
out.relevancy = { initialized: ContentRelevancyManager.initialized, enabled: ContentRelevancyManager.enabled };

const { FirefoxRelay } = ChromeUtils.importESModule("resource://gre/modules/FirefoxRelay.sys.mjs");
out.relay = {
  isAvailable: FirefoxRelay.isAvailable,
  isEnabled: FirefoxRelay.isEnabled,
  profileInfo: await FirefoxRelay.getRelayProfileInfo(),
};

let crash = {};
try {
  crash.enabled = Services.appinfo.QueryInterface(Ci.nsICrashReporter).enabled;
} catch (e) {
  crash.unavailable = String(e);
}
out.crash = crash;
out.greBinDir = Services.dirsvc.get("GreBinD", Ci.nsIFile).path;

const procs = await ChromeUtils.requestProcInfo();
out.pids = [procs.pid, ...procs.children.map(c => c.pid)];
return out;
"""

acc = None


def load_acceptance():
    """Import acceptance.py without running it (it needs marionette_driver)."""
    global acc
    if acc is None:
        spec = importlib.util.spec_from_file_location(
            "m6e_acceptance", os.path.join(HERE, "acceptance.py")
        )
        acc = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(acc)
    return acc


CHECKS = []


def check(key, label, title):
    def register(fn):
        CHECKS.append({"key": key, "label": label, "title": title, "fn": fn})
        return fn

    return register


def lsof(flags, pids):
    """Run lsof in field mode; returns [(pid, command, name)]."""
    proc = subprocess.run(
        ["lsof", "-nP", *flags, "-a", "-p", ",".join(map(str, pids)), "-Fpcn"],
        capture_output=True,
        text=True,
        check=False,
    )
    rows, pid, command = [], None, None
    for line in proc.stdout.splitlines():
        if line.startswith("p"):
            pid = int(line[1:])
        elif line.startswith("c"):
            command = line[1:]
        elif line.startswith("n"):
            rows.append((pid, command, line[1:]))
    return rows


def process_tree(root_pids):
    table = subprocess.run(
        ["ps", "-axo", "pid=,ppid=,command="],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    commands, children = {}, {}
    for line in table.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        pid, ppid = int(parts[0]), int(parts[1])
        commands[pid] = parts[2]
        children.setdefault(ppid, []).append(pid)
    seen, queue = [], list(root_pids)
    while queue:
        pid = queue.pop()
        if pid in seen:
            continue
        seen.append(pid)
        queue += children.get(pid, [])
    return {pid: commands.get(pid, "?") for pid in seen}


def local_port(name):
    local = name.split("->")[0].split(" ")[0]
    match = re.search(r":(\d+)$", local)
    return int(match.group(1)) if match else None


def collect(ctx):
    """One browser session: read the state, stay idle, snapshot the sockets."""
    a = load_acceptance()
    with a.browser(ctx) as client:
        data = a.run_js(
            client,
            JS_STATE,
            timeout=180,
            PREFS=[
                n
                for group in (*LOCKED_PREFS.values(), DEFAULT_OFF_PREFS)
                for n in group
            ],
            ABSENT_PREFS=ABSENT_PREFS,
            ABSENT_CONTRACTS=ABSENT_CONTRACTS,
            PRESENT_CONTRACTS=PRESENT_CONTRACTS,
            ABSENT_RESOURCES=ABSENT_RESOURCES,
            CONTROL_RESOURCES=CONTROL_RESOURCES,
            ABSENT_ABOUT=ABSENT_ABOUT,
        )
        marionette_port = getattr(client, "port", None) or 2828
        time.sleep(ctx.idle_seconds)
        tree = process_tree(data["pids"])
        pids = sorted(tree)
        data["marionettePort"] = marionette_port
        data["idleSeconds"] = ctx.idle_seconds
        data["tree"] = tree
        data["tcpListeners"] = lsof(["-iTCP", "-sTCP:LISTEN"], pids)
        data["udpSockets"] = lsof(["-iUDP"], pids)
        data["openFiles"] = len(lsof([], pids))
    return data


def session(ctx):
    return ctx.once("session", lambda: collect(ctx))


@check("prefs", "1", "Locked prefs of dormant features")
def check_prefs(ctx, r):
    data = session(ctx)
    for feature, prefs in LOCKED_PREFS.items():
        bad = []
        for name, want in prefs.items():
            got = data["prefs"][name]
            if got["value"] != want or not got["locked"]:
                bad.append(
                    f"{name}={got['value']!r}{'' if got['locked'] else ' (not locked)'}"
                )
        r.add(f"locked: {feature}", not bad, bad or f"{len(prefs)} prefs set and locked")
    bad = [n for n, w in DEFAULT_OFF_PREFS.items() if data["prefs"][n]["value"] != w]
    r.add("defaults: scheduled backups off", not bad, bad or DEFAULT_OFF_PREFS)
    defined = {n: t for n, t in data["absentPrefs"].items() if t != "invalid"}
    r.add(
        "prefs of deleted features are undefined",
        not defined,
        defined or f"{len(ABSENT_PREFS)} prefs have no type",
    )


@check("xpcom", "2", "Removed components and modules are absent")
def check_xpcom(ctx, r):
    data = session(ctx)
    present = [i for i, v in data["contracts"].items() if v]
    r.add(
        "contract IDs of the updater and BITS are unregistered",
        not present,
        present or f"{len(ABSENT_CONTRACTS)} absent",
    )
    missing = [i for i, v in data["presentContracts"].items() if not v]
    r.add("crash monitor contract is registered", not missing, missing or PRESENT_CONTRACTS)
    controls = {u: data["resources"][u] for u in CONTROL_RESOURCES}
    r.add(
        "fetch() sees files that exist (control)",
        all(v.startswith("PRESENT") for v in controls.values()),
        controls,
    )
    found = {u: data["resources"][u] for u in ABSENT_RESOURCES if data["resources"][u] != "absent"}
    r.add(
        "modules of removed features cannot be fetched",
        not found,
        found or f"{len(ABSENT_RESOURCES)} rejected",
    )
    registered = [n for n, v in data["about"].items() if v]
    r.add(
        "about: pages of removed features are unregistered",
        not registered,
        registered or ABSENT_ABOUT,
    )


@check("nimbus", "3", "Nimbus stub is inert, collections stay off the allowlist")
def check_nimbus(ctx, r):
    n = session(ctx)["nimbus"]
    r.add(
        "stub never enables or enrols",
        not any([n["enabled"], n["labsEnabled"], n["rolloutsEnabled"], n["studiesEnabled"], n["init"]])
        and n["enrollments"] == 0
        and n["metadata"] is None,
        n,
    )
    a = session(ctx)["allowlist"]
    r.add(
        "Remote Settings allowlist refuses Nimbus and Relay collections",
        a["control"] and not (a["nimbus"] or a["relayAllow"] or a["relayDeny"]),
        a,
    )


@check("suggest", "4", "Firefox Suggest and the Rust consumers are off")
def check_suggest(ctx, r):
    data = session(ctx)
    s = data["suggest"]
    r.add("quickSuggestEnabled is false", s["quickSuggestEnabled"] is False, s["quickSuggestEnabled"])
    r.add(
        "no Suggest backend or Rust feature is enabled",
        not s["enabledBackends"] and not s["enabledRustFeatures"] and not s["rustBackendEnabled"],
        s,
    )
    rel = data["relevancy"]
    r.add("content relevancy is disabled", not rel["enabled"], rel)


@check("relay", "5", "Firefox Relay is disabled and has no account")
def check_relay(ctx, r):
    relay = session(ctx)["relay"]
    r.add(
        "feature unavailable, not enabled, no profile info",
        relay["isAvailable"] is False and relay["isEnabled"] is False and relay["profileInfo"] is None,
        relay,
    )


@check("crash", "6", "Crash reporter stub and bundle contents")
def check_crash(ctx, r):
    data = session(ctx)
    crash = data["crash"]
    r.add(
        "nsICrashReporter reports disabled",
        not crash.get("enabled") or "unavailable" in crash,
        crash,
    )
    bundle = os.path.join(ctx.app, "Contents")
    names = set()
    for root, dirs, files in os.walk(bundle):
        names.update(d.lower() for d in dirs)
        names.update(f.lower() for f in files)
        dirs[:] = [d for d in dirs if not d.endswith(".lproj")]
    shipped = [n for n in FORBIDDEN_BUNDLE_ITEMS if n in names]
    r.add(
        "no crash reporter, updater or pingsender in the bundle",
        not shipped,
        shipped or "none",
    )


@check("sockets", "7", "No listening socket in the process tree")
def check_sockets(ctx, r):
    data = session(ctx)
    port = data["marionettePort"]
    r.add(
        "the snapshot saw the whole tree (not vacuous)",
        len(data["tree"]) > 1 and data["openFiles"] > 0,
        f"{len(data['tree'])} processes, {data['openFiles']} open files, after {data['idleSeconds']} s idle",
    )
    foreign = [
        f"{c}[{p}] {n}" for p, c, n in data["tcpListeners"] if local_port(n) != port
    ]
    r.add(
        f"TCP: only Marionette (port {port}) listens",
        not foreign,
        foreign or f"{len(data['tcpListeners'])} listener(s), all on {port}",
    )
    fixed = [
        f"{c}[{p}] {n}"
        for p, c, n in data["udpSockets"]
        if (local_port(n) or 65535) < 49152
    ]
    r.add(
        "UDP: no socket bound to a fixed (non-ephemeral) port",
        not fixed,
        fixed or f"{len(data['udpSockets'])} ephemeral UDP socket(s)",
    )
    r.facts["udp sockets"] = [f"{c} {n}" for _, c, n in data["udpSockets"]]
    helpers = [f"{pid}: {cmd}" for pid, cmd in data["tree"].items() if FORBIDDEN_PROCESS.search(cmd)]
    r.add(
        "no pingsender, crash reporter, updater or minidump process",
        not helpers,
        helpers or sorted({os.path.basename(c.split(" ")[0]) for c in data["tree"].values()}),
    )


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", help="path to M6E.app")
    parser.add_argument("--out", help="directory for results.json")
    parser.add_argument("--idle-seconds", type=int, default=20)
    parser.add_argument("--only", help="comma-separated check keys")
    parser.add_argument("--list", action="store_true", help="list the checks")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--lock-dir", help="mkdir/rmdir mutex around the browser run")
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    if args.list:
        for c in CHECKS:
            print(f"{c['key']:<10}{c['label']:<4}{c['title']}")
        return 0
    if not args.app or not args.out:
        parser.error("--app and --out are required")
    keys = {c["key"] for c in CHECKS}
    wanted = set(args.only.split(",")) if args.only else keys
    if wanted - keys:
        parser.error(f"unknown checks: {sorted(wanted - keys)}")

    a = load_acceptance()
    args.startup_runs = 0
    ctx = a.Context(args)
    if not os.access(ctx.binary, os.X_OK):
        parser.error(f"no executable at {ctx.binary}")
    os.makedirs(ctx.out, exist_ok=True)

    meta = {
        "app": ctx.app,
        "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": f"{os.uname().machine}, {os.cpu_count()} cores",
    }
    results = []
    try:
        for entry in (c for c in CHECKS if c["key"] in wanted):
            print(f"[{entry['label']}] {entry['title']} ...", file=sys.stderr, flush=True)
            results.append(a.run_check(ctx, entry))
            print(
                f"[{entry['label']}] {results[-1]['status']}: {results[-1]['summary']}",
                file=sys.stderr,
                flush=True,
            )
    except KeyboardInterrupt:
        traceback.print_exc()
    finally:
        ctx.close()
        session_data = ctx.cache.get("session")
        with open(os.path.join(ctx.out, "results.json"), "w") as fh:
            json.dump(
                {
                    **meta,
                    "checks": results,
                    "session": session_data[1] if session_data and session_data[0] else None,
                },
                fh,
                indent=2,
                default=str,
            )
    print()
    print(a.render_table(results))
    for c in results:
        for item in c["items"]:
            if not item["ok"]:
                print(f"FAIL {c['label']}: {item['name']} ({item['detail'][:300]})")
    failed = [c for c in results if c["status"] != "PASS"]
    print(f"\n{len(results) - len(failed)} of {len(results)} checks passed.")
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
