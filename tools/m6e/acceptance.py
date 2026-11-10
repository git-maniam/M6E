# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Run the Phase 5 acceptance checks (and, from Phase 8, the BitTorrent checks BT1 to BT9).

    ./mach python tools/m6e/acceptance.py --app <path to M6E.app> --out <dir> [--idle-seconds N]

Prints a PASS/FAIL table and writes results.json and report.md into --out. The
exit status is 1 if any check fails.

Each check starts its own browser on a fresh profile and is isolated: an
exception in one does not stop the others. Only one browser may use the
Marionette port at a time, and timings need a quiet machine.
  --only a,b      run some checks (--list names them)
  --idle-seconds  length of the idle network capture (default 300)
  --startup-runs  warm starts for the first-paint median (default 5)
  --lock-dir DIR  take DIR (mkdir/rmdir) around every browser run, for
                  machines where other tools share the Marionette port
  --headless      run the Marionette sessions headless

The checks follow the plan's P5.2 and the spec's decisions D2, D4, D5, D10,
D11, D12, D18 to D20, D22, D25 and D29. They reuse measure.py (startup,
memory, log parsing), push/push_check.py, trackers/tracker_count_check.py and
branding/scan-strings.py.

The BitTorrent checks (spec 8.8) use only synthetic fixtures served on 127.0.0.1
from tools/m6e/torrent/fixtures (spec 8.9, D38) and the helper packaged in the
app. Their titles say [automated]; the steps left to a person are printed at the
end. BT2, BT4 and BT7 share one browser session (the Add dialog, a magnet link,
a quit with an active torrent). The app must be signed and contain the helper.
"""

import argparse
import contextlib
import datetime
import hashlib
import http.server
import importlib.util
import json
import os
import plistlib
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import urllib.parse
import zlib

from marionette_driver.marionette import Marionette
from mozpack.mozjar import JarReader

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


measure = load_module("m6e_measure", os.path.join(HERE, "measure.py"))
scan_strings = load_module(
    "m6e_scan_strings", os.path.join(HERE, "branding", "scan-strings.py")
)

REMOTE_SETTINGS = (
    "firefox.settings.services.mozilla.com",
    "content-signature-2.cdn.mozilla.net",
    "firefox-settings-attachments.cdn.mozilla.net",
)
DOH_HOST = "security.cloudflare-dns.com"
WIDEVINE_HOSTS = ("www.google.com", "dl.google.com", "edgedl.me.gvt1.com")
ALLOWED_HOSTS = {
    *REMOTE_SETTINGS,
    "push.services.mozilla.com",
    DOH_HOST,
    "update.googleapis.com",
    "ciscobinary.openh264.org",
    *WIDEVINE_HOSTS,
}

# Measured by tools/m6e/measure.py: the stock Nightly baseline (P0.4,
# artifacts/baseline-P0) and the final Phase 4 build (P4.9).
PHASE0 = {"firstPaint": 785, "blank_mb": 698, "five_sites_mb": 1574}
PHASE4 = {"firstPaint": 649, "blank_mb": 486, "five_sites_mb": 1644}

BLOCKED_DOMAINS = ("malware.testcategory.com", "phishing.testcategory.com")

# Privileged pages that must stay light when the system is in Dark Mode.
THEME_ABOUT_PAGES = [
    "about:preferences",
    "about:addons",
    "about:downloads",
    "about:license",
    "about:newtab",
]

USER_VISIBLE_PLIST_KEYS = re.compile(
    r"^(CFBundleName|CFBundleDisplayName|CFBundleGetInfoString|CFBundleTypeName"
    r"|CFBundleURLName|NSHumanReadableCopyright|NS\w+UsageDescription)$"
)
BRAND_NAMES = re.compile(r"\b(Firefox|Mozilla)\b", re.I)

# Elements of the browser window that belong to removed features. Each must be
# missing, hidden, or not displayed.
REMOVED_ELEMENTS = {
    "Firefox View": [
        "firefox-view-button",
        "menu_openFirefoxView",
        "key_openFirefoxView",
    ],
    "Pocket": ["pocket-button", "save-to-pocket-button", "menu_pocket"],
    "Mozilla accounts and Sync": [
        "fxa-toolbar-menu-button",
        "appMenu-fxa-status2",
        "PanelUI-fxa",
        "sync-setup",
        "sync-syncnowitem",
        "sync-enabled",
        "sync-reauthitem",
        "syncedTabs",
        "context-sendpagetodevice",
        "context-sendlinktodevice",
        "context_sendTabToDevice",
    ],
    "VPN": ["ipprotection-button"],
    "Translations": ["translations-button"],
}

# Prefs that switch removed features off. All must have this value and be locked.
REMOVED_PREFS = {
    "Mozilla accounts and Sync": {"identity.fxaccounts.enabled": False},
    "Firefox Relay": {
        "signon.firefoxRelay.feature": "",
        "browser.promo.relay.enabled": False,
    },
    "VPN": {"browser.ipProtection.enabled": False},
    "AI": {
        "pdfjs.enableAltText": False,
        "pdfjs.enableGuessAltText": False,
        "pdfjs.enableAltTextModelDownload": False,
        "browser.ai.control.default": "blocked",
        "browser.ai.control.sidebarChatbot": "blocked",
        "browser.ai.control.smartWindow": "blocked",
    },
    "Studies": {
        "app.normandy.enabled": False,
        "app.shield.optoutstudies.enabled": False,
    },
    "Telemetry and data collection": {
        "datareporting.healthreport.uploadEnabled": False,
        "datareporting.policy.dataSubmissionEnabled": False,
        "toolkit.telemetry.enabled": False,
    },
    "Safe Browsing": {
        "browser.safebrowsing.malware.enabled": False,
        "browser.safebrowsing.phishing.enabled": False,
        "browser.safebrowsing.downloads.enabled": False,
        "browser.safebrowsing.blockedURIs.enabled": False,
    },
    "Add-on recommendations and updates": {
        "extensions.update.enabled": False,
        "extensions.getAddons.showPane": False,
        "extensions.getAddons.cache.enabled": False,
        "browser.discovery.enabled": False,
        "extensions.htmlaboutaddons.recommendations.enabled": False,
    },
    "Sponsored content": {
        "browser.newtabpage.activity-stream.showSponsored": False,
        "browser.newtabpage.activity-stream.showSponsoredTopSites": False,
        "browser.newtabpage.activity-stream.system.showSponsored": False,
        "browser.newtabpage.activity-stream.feeds.section.topstories": False,
        "browser.urlbar.quicksuggest.enabled": False,
        "browser.urlbar.quicksuggest.rustEnabled": False,
        "browser.urlbar.suggest.quicksuggest.sponsored": False,
    },
    "New Tab widgets and wallpapers": {
        "browser.newtabpage.activity-stream.newtabWallpapers.enabled": False,
        "browser.newtabpage.activity-stream.widgets.weather.enabled": False,
        "browser.newtabpage.activity-stream.widgets.stocks.enabled": False,
    },
    "Content relevancy": {"toolkit.contentRelevancy.enabled": False},
}

# Build-time switches (AppConstants) that must be off.
REMOVED_CONSTANTS = {
    "Crash reporter": "MOZ_CRASHREPORTER",
    "Studies (Normandy)": "MOZ_NORMANDY",
    "Telemetry reporting": "MOZ_TELEMETRY_REPORTING",
    "Data reporting": "MOZ_DATA_REPORTING",
}

# about: pages of removed or dormant features, which must not be registered.
REMOVED_ABOUT = [
    "firefoxview",
    "pocket-saved",
    "pocket-signup",
    "studies",
    "translations",
    "aichatcontent",
    "smartpage",
    "smartwindowtasks",
    "smartformfillreview",
    "asrouter",
    "messagepreview",
    "referrals",
    "mozilla",
    "rights",
]

# Settings groups of removed features, which must not show on any pane.
REMOVED_SETTINGS_GROUPS = [
    "updates",
    "ipprotection",
    "dataCollection",
    "translations",
    "firefoxSuggest",
    "accountDisabled",
    "assistantDefaultGroup",
    "assistantModelGroup",
    "memoriesGroup",
    "firefoxLabsFeatures",
]
REMOVED_SETTINGS_TEXT = re.compile(
    r"\b(sync|account|vpn|studies|pocket|translat\w*|telemetry|AI)\b", re.I
)

ARTICLE_PARAGRAPH = (
    "The autorickshaw is a motorised three wheeled taxi that threads through "
    "the traffic of Indian cities, small enough to find a gap that no car can "
    "use and sturdy enough to carry a family and its shopping home. Drivers "
    "know every shortcut, and passengers learn quickly that the meter, the "
    "horn and the hand signals form a language of their own."
)
ARTICLE = (
    """<!DOCTYPE html><html><head><meta charset="utf-8">
<title>Acceptance article</title>
<style>body{background:#fde;color:#036;font:14px serif}</style></head><body>
<nav id="site-nav"><a href="/">Home</a> <a href="/news">News</a></nav>
<div id="ad-slot-1">Buy our product</div>
<aside id="sidebar"><p>Sidebar promotions and related links that reading mode drops.</p></aside>
<article><h1>An article about autorickshaws</h1>
"""
    + "".join(f"<p>{ARTICLE_PARAGRAPH}</p>\n" for _ in range(6))
    + """<p style="color:#c00;background:#ffc">This paragraph carries page colours that reading mode removes.</p>
<figure><img src="/photo.png" width="600" height="400" alt="A photo"><figcaption>An essential figure</figcaption></figure>
<img src="/pixel.png" width="1" height="1" alt="">
<iframe srcdoc="<p>embedded</p>"></iframe>
<div class="advertisement">Sponsored banner</div>
<p>Advertisement</p>
</article></body></html>"""
)
TELEMETRY_PAGE = """<!DOCTYPE html><html><head><meta charset="utf-8">
<title>Telemetry test</title><script src="/app.js"></script></head><body>
<h1>Telemetry</h1><p>Some text so that the page has a contentful paint.</p>
<img src="/photo.png" width="60" height="40" alt="">
<script>
let start = performance.now(), total = 0;
while (performance.now() - start < 30) { total += Math.sqrt(total + 1); }
document.body.dataset.total = total;
</script></body></html>"""
APP_JS = "window.appLoaded = true;\n" + "".join(
    f"function f{i}(a){{return a*{i}+1;}}\n" for i in range(600)
)
PLAIN_PAGE = (
    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Plain</title></head>"
    "<body><p>Plain page.</p></body></html>"
)


def png(width, height, rgb=(120, 160, 200)):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    rows = b"".join(b"\x00" + bytes(rgb) * width for _ in range(height))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def pdf():
    """A one-page PDF with a line of text."""
    text = b"BT /F1 24 Tf 20 100 Td (Acceptance PDF) Tj ET"
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R"
        b" /Resources << /Font << /F1 5 0 R >> >> >>",
        b"<< /Length %d >>\nstream\n%s\nendstream" % (len(text), text),
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
    out, offsets = b"%PDF-1.4\n", []
    for number, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n%s\nendobj\n" % (number, body)
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    out += b"".join(b"%010d 00000 n \n" % offset for offset in offsets)
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (
        len(objects) + 1,
        xref,
    )
    return out


PAGES = {
    "/article.html": ("text/html; charset=utf-8", ARTICLE.encode()),
    "/telemetry.html": ("text/html; charset=utf-8", TELEMETRY_PAGE.encode()),
    "/plain.html": ("text/html; charset=utf-8", PLAIN_PAGE.encode()),
    "/app.js": ("text/javascript", APP_JS.encode()),
    "/photo.png": ("image/png", png(600, 400)),
    "/pixel.png": ("image/png", png(1, 1)),
    "/doc.pdf": ("application/pdf", pdf()),
}


class PageHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        entry = PAGES.get(urllib.parse.urlparse(self.path).path)
        if not entry:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", entry[0])
        self.send_header("Content-Length", str(len(entry[1])))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(entry[1])

    def log_message(self, *args):
        pass


WRAPPER = """
const resolve = arguments[arguments.length - 1];
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
const sp = Services.scriptSecurityManager.getSystemPrincipal();
const win = Services.wm.getMostRecentWindow("navigator:browser");
const doc = win.document;
const openTab = url => {
  const tab = win.gBrowser.addTab(url, { triggeringPrincipal: sp });
  win.gBrowser.selectedTab = tab;
  return tab;
};
const waitLoaded = async (tab, spec) => {
  const b = tab.linkedBrowser;
  for (let i = 0; i < 80; i++) {
    await sleep(250);
    if (!b.webProgress?.isLoadingDocument && b.currentURI.spec.startsWith(spec)) {
      await sleep(500);
      return true;
    }
  }
  return false;
};
const inContent = (browser, code) => Promise.race([
  new Promise(done => {
    const mm = browser.messageManager;
    const name = "m6e-acceptance-" + Math.random();
    const listener = m => { mm.removeMessageListener(name, listener); done(m.data); };
    mm.addMessageListener(name, listener);
    const script = "sendAsyncMessage(" + JSON.stringify(name) + ", (() => {" + code + "})())";
    mm.loadFrameScript("data:," + encodeURIComponent(script), false);
  }),
  sleep(15000).then(() => ({ __timeout: "no answer from the content process" })),
]);
const probe = async (browser, fn) => browser.contentWindow
  ? fn(browser.contentWindow)
  : inContent(browser, "return (" + fn.toString() + ")(content);");
__BODY__
})().then(resolve, e => resolve({ __error: String(e), stack: e.stack }));
"""

JS_THEME = """
const lum = c => {
  const m = c.match(/[\\d.]+/g).map(Number);
  return +((0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2]) / 255).toFixed(3);
};
const mq = content => {
  const q = s => content.matchMedia(s).matches;
  const cs = content.getComputedStyle(content.document.body || content.document.documentElement);
  const opaque = [content.document.body, content.document.documentElement]
    .map(e => e && content.getComputedStyle(e).backgroundColor)
    .find(c => c && c != "transparent" && !/, 0\\)$/.test(c));
  return {
    light: q("(prefers-color-scheme: light)"), dark: q("(prefers-color-scheme: dark)"),
    color: cs.color, background: opaque ?? null, scheme: cs.colorScheme,
  };
};
const out = {};
const nav = win.getComputedStyle(doc.getElementById("nav-bar"));
out.chrome = {
  light: win.matchMedia("(prefers-color-scheme: light)").matches,
  dark: win.matchMedia("(prefers-color-scheme: dark)").matches,
  scheme: win.getComputedStyle(doc.documentElement).colorScheme,
  toolbarTextLuminance: lum(nav.color),
};
out.about = {};
for (const url of __ABOUT__) {
  const tab = openTab(url);
  const loaded = await waitLoaded(tab, url);
  const result = await probe(tab.linkedBrowser, mq);
  result.loaded = loaded;
  result.textLuminance = result.color ? lum(result.color) : null;
  out.about[url] = result;
  win.gBrowser.removeTab(tab);
}
const page = openTab(__URL__);
await waitLoaded(page, "http://127.0.0.1");
out.content = await probe(page.linkedBrowser, mq);
const pdfViewer = content => {
  const d = content.document;
  const toolbar = d.getElementById("toolbarContainer");
  const q = s => content.matchMedia(s).matches;
  return {
    viewer: !!toolbar,
    light: q("(prefers-color-scheme: light)"), dark: q("(prefers-color-scheme: dark)"),
    scheme: content.getComputedStyle(d.documentElement).colorScheme,
    background: content.getComputedStyle(d.body).backgroundColor,
    toolbarColor: toolbar && content.getComputedStyle(toolbar).color,
  };
};
const pdf = openTab(__PDF__);
await waitLoaded(pdf, "http://127.0.0.1");
for (let i = 0; i < 12; i++) {
  await sleep(1000);
  out.pdf = await probe(pdf.linkedBrowser, pdfViewer);
  if (out.pdf.viewer) {
    break;
  }
}
return out;
"""

JS_ABOUT = """
win.openAboutDialog();
let about;
for (let i = 0; i < 40 && !about; i++) {
  await sleep(250);
  about = Services.wm.getMostRecentWindow("Browser:About");
}
if (!about) {
  return { __error: "The About dialog did not open" };
}
await sleep(500);
await Promise.race([about.document.l10n?.ready, sleep(5000)]);
await sleep(500);
const d = about.document;
const text = id => d.getElementById(id)?.textContent.replace(/\\s+/g, " ").trim() ?? null;
const out = {
  welcome: text("m6eWelcome"),
  experimental: text("m6eExperimental"),
  attribution: text("m6eAttribution"),
  licenseLink: d.querySelector("#m6eAttribution [data-l10n-name='license-link']")?.getAttribute("href"),
  otherNames: [...d.querySelectorAll("*")]
    .filter(e => e.checkVisibility() && !e.closest("#m6eAttribution"))
    .flatMap(e => [...e.childNodes]
      .filter(n => n.nodeType == 3 && /\\b(Firefox|Mozilla)\\b/.test(n.textContent))
      .map(n => (e.id || e.localName) + ": " + n.textContent.trim())),
};
about.close();
const tab = openTab("about:license");
out.licenseLoaded = await waitLoaded(tab, "about:license");
out.licenseText = await probe(tab.linkedBrowser, content => content.document.body.innerText.replace(/\\s+/g, " "));
return out;
"""

JS_SEARCH = """
const { SearchService } = ChromeUtils.importESModule("moz-src:///toolkit/components/search/SearchService.sys.mjs");
await SearchService.init();
const out = {};
const engines = await SearchService.getEngines();
out.visible = (await SearchService.getVisibleEngines()).map(e => e.name);
out.all = engines.map(e => e.name);
out.default = SearchService.defaultEngine.name;
out.defaultPrivate = SearchService.defaultPrivateEngine.name;
out.submissions = Object.fromEntries(engines.map(e => [e.name, e.getSubmission("m6e test").uri.spec]));
out.bundledOnly = Services.prefs.getBoolPref("browser.search.bundledEnginesOnly");
out.bundledOnlyLocked = Services.prefs.prefIsLocked("browser.search.bundledEnginesOnly");
out.policyAllowsInstall = Services.policies.isAllowed("installSearchEngine");
for (const [name, fn] of [
  ["userEngine", () => SearchService.addUserEngine({ name: "x", url: "https://example.com/?q={searchTerms}" })],
  ["openSearch", () => SearchService.addOpenSearchEngine("https://example.com/os.xml")],
]) {
  try {
    await fn();
    out[name] = "ADDED";
  } catch (e) {
    out[name] = e.type || String(e);
  }
}
out.afterAttempts = (await SearchService.getEngines()).map(e => e.name);
const tab = openTab("about:preferences#search");
await waitLoaded(tab, "about:preferences");
await sleep(1500);
const pd = tab.linkedBrowser.contentDocument;
out.addButtons = [...pd.querySelectorAll("#addEngineButton, [id*='addEngine'], [data-l10n-id*='add-search-engine']")]
  .filter(e => e.checkVisibility()).map(e => e.id || e.localName);
out.uiEngines = [...pd.querySelectorAll("#defaultEngine moz-option, #defaultEngine option, #defaultEngine menuitem, moz-select[id*='ngine'] moz-option")]
  .map(e => (e.label || e.textContent).trim()).filter(Boolean);
return out;
"""

JS_REMOVED = """
const ELEMENTS = __ELEMENTS__;
const PREFS = __PREFS__;
const CONSTANTS = __CONSTANTS__;
const ABOUT = __ABOUT__;
const GROUPS = __GROUPS__;
const P = Services.prefs;
const { AppConstants } = ChromeUtils.importESModule("resource://gre/modules/AppConstants.sys.mjs");
const state = id => {
  const e = doc.getElementById(id);
  if (!e) {
    return "absent";
  }
  return e.hidden || !!e.closest("[hidden]") || win.getComputedStyle(e).display == "none" ? "hidden" : "PRESENT";
};
const out = { elements: {}, prefs: {}, constants: {}, about: {}, panes: {} };
for (const [feature, ids] of Object.entries(ELEMENTS)) {
  out.elements[feature] = Object.fromEntries(ids.map(id => [id, state(id)]));
}
for (const [feature, prefs] of Object.entries(PREFS)) {
  out.prefs[feature] = {};
  for (const [name, want] of Object.entries(prefs)) {
    const type = P.getPrefType(name);
    const value = type == P.PREF_BOOL ? P.getBoolPref(name) : type == P.PREF_INT ? P.getIntPref(name)
      : type == P.PREF_STRING ? P.getStringPref(name) : null;
    out.prefs[feature][name] = { value, want, locked: P.prefIsLocked(name) };
  }
}
for (const [feature, name] of Object.entries(CONSTANTS)) {
  out.constants[feature] = { name, value: AppConstants[name] };
}
const registered = [];
for (const cid in Cc) {
  const m = cid.match(/^@mozilla\\.org\\/network\\/protocol\\/about;1\\?what=(.+)$/);
  if (m) {
    registered.push(m[1]);
  }
}
out.about = { absent: ABOUT.filter(n => !registered.includes(n)), registered: ABOUT.filter(n => registered.includes(n)), allRegistered: registered.sort() };
out.pocketPref = P.getPrefType("extensions.pocket.enabled") ? P.getBoolPref("extensions.pocket.enabled") : null;
let nimbusEnrollment = true;
try {
  ChromeUtils.importESModule("resource://nimbus/lib/ExperimentManager.sys.mjs");
} catch (e) {
  nimbusEnrollment = false;
}
out.nimbus = {
  enrollmentModule: nimbusEnrollment,
  enabled: ChromeUtils.importESModule("resource://nimbus/ExperimentAPI.sys.mjs").ExperimentAPI.enabled,
};
out.sidebarSyncTools = win.SidebarController?.toolsAndExtensions
  ? [...win.SidebarController.toolsAndExtensions.keys()].filter(k => /sync|firefoxview|pocket/i.test(k)) : [];
const tab = openTab("about:preferences");
await waitLoaded(tab, "about:preferences");
await sleep(1500);
const pd = tab.linkedBrowser.contentDocument;
const buttons = [...pd.querySelectorAll("moz-page-nav-button[view]")].filter(b => b.id && b.checkVisibility());
out.panes.categories = buttons.map(b => b.textContent.trim());
out.panes.groups = {};
for (const b of buttons) {
  b.click();
  await sleep(800);
  out.panes.groups[b.textContent.trim()] = [...pd.querySelectorAll("setting-group")]
    .filter(g => g.checkVisibility()).map(g => g.getAttribute("groupid"));
}
out.panes.forbiddenGroups = GROUPS;
return out;
"""

JS_BOOKMARKS = """
const PREF = "browser.toolbars.bookmarks.visibility";
const bar = () => doc.getElementById("PersonalToolbar");
const out = { defaultValue: Services.prefs.getCharPref(PREF), collapsedAtStart: bar().collapsed };
const tab = openTab("about:preferences#appearance");
await waitLoaded(tab, "about:preferences");
await sleep(2000);
const pd = tab.linkedBrowser.contentDocument;
const group = pd.querySelector('setting-group[groupid="bookmarksToolbar"]');
out.groupVisible = !!group && group.checkVisibility();
const toggle = pd.getElementById("showBookmarksToolbar");
out.toggleFound = !!toggle;
out.label = toggle?.label;
if (!toggle) {
  return out;
}
out.pressedAtStart = toggle.pressed;
toggle.click();
await sleep(1000);
out.afterOn = { pref: Services.prefs.getCharPref(PREF), collapsed: bar().collapsed, pressed: toggle.pressed };
toggle.click();
await sleep(1000);
out.afterOff = { pref: Services.prefs.getCharPref(PREF), collapsed: bar().collapsed, pressed: toggle.pressed };
const { MigrationUtils } = ChromeUtils.importESModule("moz-src:///browser/components/migration/MigrationUtils.sys.mjs");
out.migrators = MigrationUtils.availableMigratorKeys;
return out;
"""

JS_READER = """
const out = {};
const page = openTab(__URL__);
await waitLoaded(page, "http://127.0.0.1");
let button;
for (let i = 0; i < 40; i++) {
  await sleep(250);
  button = doc.getElementById("reader-mode-button");
  if (button && !button.hidden) {
    break;
  }
}
out.buttonVisible = !!button && !button.hidden && button.checkVisibility();
if (!out.buttonVisible) {
  return out;
}
button.click();
out.openedReader = await waitLoaded(page, "about:reader");
out.url = page.linkedBrowser.currentURI.spec;
await sleep(2000);
out.styles = await probe(page.linkedBrowser, content => {
  const d = content.document, c = d.querySelector(".moz-reader-content");
  if (!c) { return { missing: true }; }
  const style = e => content.getComputedStyle(e);
  const p = c.querySelector("p");
  const coloured = [...c.querySelectorAll("p")].find(e => e.textContent.includes("page colours"));
  return {
    bodyColor: style(d.body).color, bodyBackground: style(d.body).backgroundColor,
    bodyBackgroundImage: style(d.body).backgroundImage,
    paragraphFontSize: p && style(p).fontSize,
    colouredParagraph: coloured && { color: style(coloured).color, background: style(coloured).backgroundColor, inline: coloured.getAttribute("style") },
    counts: Object.fromEntries(["nav", "aside", "iframe", "video", "embed", "form"].map(s => [s, c.querySelectorAll(s).length])),
    images: [...c.querySelectorAll("img")].map(i => (i.getAttribute("src") || "").split("/").pop()),
    adText: ["Buy our product", "Sponsored banner", "Sidebar promotions"].filter(t => c.textContent.includes(t)),
    adLabel: [...c.querySelectorAll("p")].some(e => e.textContent.trim() == "Advertisement"),
    colorsMenuVisible: d.querySelector(".colors-dropdown")?.checkVisibility() ?? false,
  };
});
return out;
"""

JS_SHIELD = """
const P = Services.prefs;
const snap = () => ({
  enabled: P.getBoolPref("m6e.privacyShield.enabled"),
  maximum: P.getBoolPref("m6e.privacyShield.maximum"),
  gpc: P.getBoolPref("privacy.globalprivacycontrol.enabled"),
  trim: P.getIntPref("network.http.referer.XOriginTrimmingPolicy"),
  rfp: P.getBoolPref("privacy.resistFingerprinting"),
  fpp: P.getBoolPref("privacy.fingerprintingProtection"),
  fppPrivate: P.getBoolPref("privacy.fingerprintingProtection.pbmode"),
  baselineFpp: P.getBoolPref("privacy.baselineFingerprintingProtection"),
  fpTracking: P.getBoolPref("privacy.trackingprotection.fingerprinting.enabled"),
  category: P.getStringPref("browser.contentblocking.category"),
});
const out = { start: snap() };
const tab = openTab("about:preferences#privacy");
await waitLoaded(tab, "about:preferences");
await sleep(2500);
const pd = tab.linkedBrowser.contentDocument;
const group = pd.querySelector('setting-group[groupid="privacyShield"]');
const first = [...pd.querySelectorAll("setting-group")].find(g => g.checkVisibility());
const toggle = pd.getElementById("privacyShieldEnabled");
const maximum = pd.getElementById("privacyShieldMaximum");
out.ui = {
  groupVisible: !!group && group.checkVisibility(),
  firstGroupOnPane: first?.getAttribute("groupid"),
  toggleFound: !!toggle,
  maximumFound: !!maximum,
  label: toggle?.label,
  pressed: toggle?.pressed,
  maximumDisabled: maximum?.disabled,
};
if (!toggle || !maximum) {
  return out;
}
toggle.click();
await sleep(1200);
out.off = { ...snap(), maximumDisabled: maximum.disabled, pressed: toggle.pressed };
toggle.click();
await sleep(1200);
out.on = { ...snap(), maximumDisabled: maximum.disabled, pressed: toggle.pressed };
maximum.click();
await sleep(1200);
out.maximumOn = snap();
maximum.click();
await sleep(1200);
out.maximumOff = snap();
return out;
"""

JS_TELEMETRY = """
const out = { pages: {} };
const button = doc.getElementById("m6e-page-telemetry-button");
const panel = doc.getElementById("m6e-page-telemetry-panel");
const rows = () => [...panel.querySelectorAll("tr[data-metric]")].map(tr => ({
  metric: tr.dataset.metric, label: tr.querySelector("th").textContent.trim(), value: tr.querySelector("td").textContent.trim(),
}));
for (const [name, url, spec] of __PAGES__) {
  const tab = openTab(url);
  const loaded = await waitLoaded(tab, spec);
  await sleep(1000);
  const entry = { loaded, buttonVisible: button.checkVisibility(), tooltip: button.getAttribute("tooltiptext") };
  button.dispatchEvent(new MouseEvent("click", { bubbles: true, button: 0 }));
  for (let i = 0; i < 50; i++) {
    await sleep(500);
    if (panel.state == "open" && /ms$/.test(rows().find(r => r.metric == "tti")?.value ?? "")) {
      break;
    }
  }
  entry.panelState = panel.state;
  entry.rows = rows();
  entry.navigation = await probe(tab.linkedBrowser, content => {
    const n = content.performance.getEntriesByType("navigation")[0];
    return n && Object.fromEntries(["startTime", "fetchStart", "connectStart", "requestStart",
      "responseStart", "responseEnd", "domInteractive"].map(k => [k, n[k]]));
  });
  entry.title = panel.querySelector("#m6e-page-telemetry-title")?.textContent.trim();
  panel.hidePopup();
  await sleep(500);
  out.pages[name] = entry;
  win.gBrowser.removeTab(tab);
}
return out;
"""

JS_HARDENING = """
const P = Services.prefs;
const tab = openTab(__URL__);
await waitLoaded(tab, "http://127.0.0.1");
await sleep(1000);
const procs = await ChromeUtils.requestProcInfo();
return {
  sandboxLevel: P.getIntPref("security.sandbox.content.level"),
  sandboxLocked: P.prefIsLocked("security.sandbox.content.level"),
  sandboxEffective: Cc["@mozilla.org/sandbox/sandbox-settings;1"]
    .getService(Ci.mozISandboxSettings).effectiveContentSandboxLevel,
  fissionPref: P.getBoolPref("fission.autostart"),
  fissionLocked: P.prefIsLocked("fission.autostart"),
  fissionActive: Services.appinfo.fissionAutostart,
  processTypes: [...new Set(procs.children.map(c => c.type))].sort(),
  rdd: P.getIntPref("security.sandbox.rdd.level", -1),
  gpu: P.getIntPref("security.sandbox.gpu.level", -1),
  socket: P.getIntPref("security.sandbox.socket.process.level", -1),
};
"""

JS_NAV_STATE = """
const b = win.gBrowser.selectedBrowser;
return { uri: b.currentURI.spec, documentURI: b.documentURI?.spec, title: b.contentTitle };
"""


class Result:
    def __init__(self):
        self.items = []
        self.facts = {}

    def add(self, name, ok, detail=""):
        self.items.append({"name": name, "ok": bool(ok), "detail": str(detail)})
        return bool(ok)

    def info(self, name, detail=""):
        """An item that is reported but never fails the check."""
        self.items.append({
            "name": name,
            "ok": True,
            "info": True,
            "detail": str(detail),
        })


class Lock:
    """A directory used as a mutex: mkdir takes it and rmdir releases it."""

    def __init__(self, path):
        self.path = path

    @contextlib.contextmanager
    def held(self):
        if not self.path:
            yield
            return
        while True:
            try:
                os.mkdir(self.path)
                break
            except FileExistsError:
                time.sleep(5)
        try:
            yield
        finally:
            os.rmdir(self.path)


class Context:
    def __init__(self, args):
        self.app = os.path.abspath(args.app)
        self.binary = os.path.join(self.app, "Contents", "MacOS", "m6e")
        self.out = os.path.abspath(args.out)
        self.idle_seconds = args.idle_seconds
        self.startup_runs = args.startup_runs
        self.headless = args.headless
        self.lock = Lock(args.lock_dir)
        self.cache = {}
        self.server = None

    @property
    def base_url(self):
        if self.server is None:
            self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), PageHandler)
            threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return f"http://127.0.0.1:{self.server.server_address[1]}"

    def close(self):
        if self.server:
            self.server.shutdown()

    def once(self, key, fn):
        if key not in self.cache:
            try:
                self.cache[key] = (True, fn())
            except Exception:
                self.cache[key] = (False, traceback.format_exc())
        ok, value = self.cache[key]
        if not ok:
            raise RuntimeError(f"{key} failed earlier:\n{value}")
        return value


@contextlib.contextmanager
def browser(ctx, prefs=None, env=None):
    """A Marionette session on a fresh profile, in the chrome context."""
    with ctx.lock.held():
        saved = {k: os.environ.get(k) for k in env or {}}
        os.environ.update(env or {})
        client = Marionette(
            bin=ctx.binary,
            prefs=prefs or {},
            headless=ctx.headless,
            startup_timeout=120,
            socket_timeout=150,
        )
        try:
            client.start_session()
            client.set_context(client.CONTEXT_CHROME)
            client.switch_to_window(client.chrome_window_handles[0])
            yield client
        finally:
            with contextlib.suppress(Exception):
                client.quit(in_app=True)
            with contextlib.suppress(Exception):
                client.cleanup()
            for key, value in saved.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value


def run_js(client, body, timeout=120, **substitutions):
    for key, value in substitutions.items():
        body = body.replace(f"__{key}__", json.dumps(value))
    client.set_context(client.CONTEXT_CHROME)
    result = client.execute_async_script(
        WRAPPER.replace("__BODY__", body), script_timeout=timeout * 1000
    )
    if isinstance(result, dict) and "__error" in result:
        raise RuntimeError(f"{result['__error']}\n{result.get('stack', '')}")
    return result


def run_tool(args, timeout, cwd=None):
    """Run a helper script, killing its whole process group on timeout."""
    proc = subprocess.Popen(
        args,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    try:
        output, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        output, _ = proc.communicate()
        output += f"\n[timed out after {timeout} s]"
        proc.returncode = -9
    return proc.returncode, output


def lum(color):
    r, g, b = (float(x) for x in re.findall(r"[\d.]+", color)[:3])
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255


CHECKS = []


def check(key, label, title):
    def register(fn):
        CHECKS.append({"key": key, "label": label, "title": title, "fn": fn})
        return fn

    return register


MACHO_MAGIC = {
    b"\xcf\xfa\xed\xfe",
    b"\xce\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe",
    b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf",
    b"\xbf\xba\xfe\xca",
}


def mach_o_archs(app):
    """Map every Mach-O file in the bundle to its architectures (via lipo).

    Also return the files that have a Mach-O magic number but that lipo cannot
    read.
    """
    archs, skipped = {}, []
    for dirpath, _, files in os.walk(app):
        for name in files:
            path = os.path.join(dirpath, name)
            if os.path.islink(path) or not os.path.isfile(path):
                continue
            try:
                with open(path, "rb") as fh:
                    magic = fh.read(4)
            except OSError:
                continue
            if magic not in MACHO_MAGIC:
                continue
            proc = subprocess.run(
                ["lipo", "-archs", path], capture_output=True, text=True, check=False
            )
            if proc.returncode:
                skipped.append(os.path.relpath(path, app))
                continue
            archs[os.path.relpath(path, app)] = proc.stdout.split()
    return archs, skipped


def plist_strings(path):
    try:
        with open(path, "rb") as fh:
            return plistlib.load(fh)
    except Exception:
        proc = subprocess.run(
            ["plutil", "-convert", "json", "-o", "-", path],
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(proc.stdout)


def visible_plist_strings(node, key=""):
    if isinstance(node, dict):
        for k, v in node.items():
            yield from visible_plist_strings(v, k)
    elif isinstance(node, list):
        for v in node:
            yield from visible_plist_strings(v, key)
    elif isinstance(node, str) and USER_VISIBLE_PLIST_KEYS.match(key):
        yield key, node


@check("bundle", "1", "App bundle")
def check_bundle(ctx, r):
    app = ctx.app
    plist_path = os.path.join(app, "Contents", "Info.plist")
    info = plist_strings(plist_path)
    r.add(
        "bundle identifier",
        info.get("CFBundleIdentifier") == "org.m6e.browser",
        info.get("CFBundleIdentifier"),
    )
    r.add("bundle name", info.get("CFBundleName") == "M6E", info.get("CFBundleName"))
    r.add(
        "bundle directory",
        os.path.basename(app) == "M6E.app",
        os.path.basename(app),
    )
    exe = os.path.join(app, "Contents", "MacOS", str(info.get("CFBundleExecutable")))
    r.add(
        "executable m6e",
        info.get("CFBundleExecutable") == "m6e" and os.access(exe, os.X_OK),
        f"{info.get('CFBundleExecutable')} executable={os.access(exe, os.X_OK)}",
    )

    strings = list(visible_plist_strings(info))
    resources = os.path.join(app, "Contents", "Resources")
    for lproj in sorted(os.listdir(resources)):
        strings_file = os.path.join(resources, lproj, "InfoPlist.strings")
        if lproj.endswith(".lproj") and os.path.exists(strings_file):
            strings += [
                (f"{lproj}/{k}", v)
                for k, v in plist_strings(strings_file).items()
                if isinstance(v, str)
            ]
    named = [f"{k}={v}" for k, v in strings if BRAND_NAMES.search(v)]
    r.add(
        "no Firefox or Mozilla in user-visible plist strings",
        not named,
        f"{len(strings)} strings checked" + (f"; found {named}" if named else ""),
    )

    icon = info.get("CFBundleIconFile")
    icon_path = os.path.join(resources, icon or "")
    icon_ok = (
        bool(icon) and os.path.isfile(icon_path) and os.path.getsize(icon_path) > 0
    )
    detail = f"{icon} {os.path.getsize(icon_path)} bytes" if icon_ok else str(icon)
    source = os.path.join(ROOT, "browser", "branding", "m6e", "firefox.icns")
    if icon_ok and os.path.exists(source):
        same = (
            hashlib.sha256(open(icon_path, "rb").read()).digest()
            == hashlib.sha256(open(source, "rb").read()).digest()
        )
        icon_ok = icon_ok and same
        detail += f"; identical to browser/branding/m6e/firefox.icns: {same}"
    r.add("icon present", icon_ok, detail)
    r.add(
        "app icon asset catalog",
        info.get("CFBundleIconName") == "AppIcon"
        and os.path.isfile(os.path.join(resources, "Assets.car")),
        f"CFBundleIconName={info.get('CFBundleIconName')}",
    )

    archs, skipped = mach_o_archs(app)
    wrong = {p: a for p, a in archs.items() if a != ["arm64"]}
    r.add(
        "every Mach-O file is arm64 only",
        archs and not wrong and not skipped,
        f"{len(archs)} Mach-O files"
        + (f"; not arm64 only: {dict(list(wrong.items())[:10])}" if wrong else "")
        + (f"; lipo could not read: {skipped[:10]}" if skipped else ""),
    )

    proc = subprocess.run(
        ["codesign", "--verify", "--deep", "--strict", "--verbose=2", app],
        capture_output=True,
        text=True,
        check=False,
    )
    sig = subprocess.run(
        ["codesign", "-dv", app], capture_output=True, text=True, check=False
    ).stderr
    signature = re.search(r"^Signature=(.*)$", sig, re.M)
    r.add(
        "codesign verifies",
        proc.returncode == 0,
        (proc.stderr.strip().splitlines() or [""])[-1]
        + f"; Signature={signature.group(1) if signature else 'none'}",
    )


LOCALE_PATH = re.compile(r"(^|/)(en-US|locale)/")


def is_string_file(name):
    return (
        name[name.rfind(".") :] in scan_strings.SCANNERS
        and bool(LOCALE_PATH.search(name))
        and "/test" not in name
    )


def string_files(app):
    """Yield (label, name, data) for every localized string file in the app.

    A packaged app keeps them inside omni.ja, which ./mach package writes with
    a preload header that zipfile cannot open; a build's dist directory keeps
    them as plain files.
    """
    resources = os.path.join(app, "Contents", "Resources")
    for dirpath, _, files in os.walk(resources):
        for name in files:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, app)
            if name == "omni.ja":
                for entry in JarReader(path):
                    if is_string_file(entry.filename):
                        yield f"{rel}!{entry.filename}", entry.filename, entry.read()
            elif not os.path.islink(path) and is_string_file(rel):
                with open(path, "rb") as fh:
                    yield rel, rel, fh.read()


def string_hits(app):
    """Scan the app's en-US strings with scan-strings.py's scanners."""
    hits, scanned = [], 0
    for label, name, data in string_files(app):
        ext = name[name.rfind(".") :]
        scanned += 1
        lines = data.decode("utf-8", "replace").split("\n")
        for n, key, _, text in scan_strings.SCANNERS[ext](name, lines):
            if key not in scan_strings.ATTRIBUTION_KEYS:
                hits.append(f"{label}:{n} {key}: {text[:80]}")
    return hits, scanned


@check("branding", "2", "Branding and attribution")
def check_branding(ctx, r):
    out_json = os.path.join(ctx.out, "scan-strings.json")
    code, output = run_tool(
        [
            sys.executable,
            os.path.join(HERE, "branding", "scan-strings.py"),
            "--json",
            out_json,
        ],
        timeout=300,
        cwd=ROOT,
    )
    summary = (output.strip().splitlines() or ["no output"])[-1]
    count = len(json.load(open(out_json))) if os.path.exists(out_json) else None
    r.add("scan-strings.py reports 0 (source tree)", code == 0 and count == 0, summary)

    hits, scanned = string_hits(ctx.app)
    r.add(
        "no Firefox or Mozilla in the app's en-US strings",
        scanned > 0 and not hits,
        f"{scanned} string files"
        + (f"; {len(hits)} hits, first: {hits[:5]}" if hits else ""),
    )

    with browser(ctx) as client:
        data = run_js(client, JS_ABOUT)
    welcome = data.get("welcome") or ""
    r.add(
        "About dialog shows the M6E description",
        welcome.startswith("Welcome to M6E, a hyper-optimized")
        and "experimental release" in (data.get("experimental") or ""),
        welcome[:80],
    )
    attribution = data.get("attribution") or ""
    r.add(
        "About dialog states the Firefox/Mozilla/MPL 2.0 attribution",
        "M6E is based on Firefox source code from the Mozilla project" in attribution
        and "Mozilla Public License 2.0" in attribution
        and data.get("licenseLink") == "about:license",
        attribution,
    )
    r.add(
        "About dialog names Firefox and Mozilla only in the attribution",
        not data.get("otherNames"),
        data.get("otherNames") or "no other visible mentions",
    )
    text = data.get("licenseText") or ""
    r.add(
        "about:license states the attribution",
        data.get("licenseLoaded")
        and "M6E is based on Firefox source code from the Mozilla project" in text
        and "Mozilla Public License 2.0" in text,
        text[:200],
    )


def run_idle(ctx):
    """Plain (non-Marionette) headless launch: log hosts, then list the listeners."""
    log_dir = os.path.join(ctx.out, "net-idle")
    shutil.rmtree(log_dir, ignore_errors=True)
    os.makedirs(log_dir)
    prefix = os.path.join(log_dir, "net-idle.log")
    profile = tempfile.mkdtemp(prefix="m6e-acceptance-idle-")
    env = dict(
        os.environ,
        MOZ_LOG="timestamp,nsHostResolver:5,nsHttp:3",
        MOZ_LOG_FILE=prefix,
    )
    with ctx.lock.held():
        proc = subprocess.Popen(
            [ctx.binary, "-no-remote", "-profile", profile, "--headless"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        result = {"seconds": ctx.idle_seconds}
        try:
            time.sleep(ctx.idle_seconds)
            result["alive"] = proc.poll() is None
            table = subprocess.run(
                ["ps", "-axo", "pid=,ppid="],
                capture_output=True,
                text=True,
                check=False,
            ).stdout
            children = {}
            for line in table.splitlines():
                pid, ppid = (int(x) for x in line.split())
                children.setdefault(ppid, []).append(pid)
            pids, queue = [], [proc.pid]
            while queue:
                pid = queue.pop()
                pids.append(pid)
                queue += children.get(pid, [])
            result["pids"] = len(pids)
            for proto, flags in (
                ("tcp_listeners", ["-iTCP", "-sTCP:LISTEN"]),
                ("udp_sockets", ["-iUDP"]),
            ):
                lsof = subprocess.run(
                    ["lsof", "-nP", *flags, "-a", "-p", ",".join(map(str, pids))],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                result[proto] = lsof.stdout.strip().splitlines()[1:]
            everything = subprocess.run(
                ["lsof", "-nP", "-a", "-p", ",".join(map(str, pids))],
                capture_output=True,
                text=True,
                check=False,
            )
            result["open_files"] = len(everything.stdout.strip().splitlines()) - 1
            processes = app_processes(ctx)
            result["helper_processes"] = [
                pid
                for pid in pids
                if os.path.basename(processes.get(pid, "")) == HELPER_NAME
            ]
            result["helper_processes_any"] = helper_pids(ctx)
        finally:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
            shutil.rmtree(profile, ignore_errors=True)
    result.update(measure.parse_network_logs(prefix))
    return result


@check("network", "3", "Network: idle fresh profile")
def check_network(ctx, r):
    idle = ctx.once("idle", lambda: run_idle(ctx))
    requested = set(idle["http_requests"])
    looked_up = set(idle["dns_only"])
    r.add(
        "app still running after the idle period", idle["alive"], f"{idle['seconds']} s"
    )
    unexpected = sorted((requested | looked_up) - ALLOWED_HOSTS)
    r.add(
        "only the allowed hosts were contacted",
        requested and not unexpected,
        f"{len(requested)} hosts with requests: {sorted(requested)}"
        + (f"; NOT ALLOWED: {unexpected}" if unexpected else ""),
    )
    r.info(
        "Widevine hosts contacted (any of the three is allowed; runs have shown one)",
        [h for h in WIDEVINE_HOSTS if h in requested | looked_up] or "none",
    )
    r.add(
        "no host was only looked up over DNS",
        not looked_up,
        sorted(looked_up) or "none",
    )
    r.add(
        f"DNS over HTTPS resolver is {DOH_HOST}",
        DOH_HOST in requested,
        f"{DOH_HOST} requests: {idle['http_requests'].get(DOH_HOST, 0)}",
    )
    r.add(
        "Remote Settings reached",
        all(h in requested for h in REMOTE_SETTINGS),
        {h: idle["http_requests"].get(h, 0) for h in REMOTE_SETTINGS},
    )
    r.facts["requests"] = idle["http_requests"]


@check("theme", "4", "Theme: UI stays light in Dark Mode")
def check_theme(ctx, r):
    with browser(ctx, prefs={"ui.systemUsesDarkTheme": 1}) as client:
        data = run_js(
            client,
            JS_THEME,
            timeout=240,
            URL=ctx.base_url + "/plain.html",
            PDF=ctx.base_url + "/doc.pdf",
            ABOUT=THEME_ABOUT_PAGES,
        )
    chrome, content = data["chrome"], data["content"]
    r.add(
        "Dark Mode is simulated: web content sees prefers-color-scheme: dark",
        content["dark"] and not content["light"],
        f"dark={content['dark']} light={content['light']}",
    )
    r.add(
        "browser window stays light",
        chrome["light"] and not chrome["dark"] and chrome["toolbarTextLuminance"] < 0.4,
        f"light={chrome['light']} dark={chrome['dark']} scheme={chrome['scheme']}"
        f" toolbar text luminance={chrome['toolbarTextLuminance']}",
    )
    for url, page in data["about"].items():
        text = page.get("textLuminance")
        background = page.get("background")
        r.add(
            f"{url} stays light",
            page["loaded"]
            and page.get("light")
            and not page.get("dark")
            and text is not None
            and text < 0.4
            and (background is None or lum(background) > 0.6),
            f"loaded={page['loaded']} light={page.get('light')} dark={page.get('dark')}"
            f" text={page.get('color')} background={background or 'transparent'}"
            + (f" {page}" if "__timeout" in page else ""),
        )
    pdf = data.get("pdf") or {}
    r.add(
        "the PDF viewer stays light",
        pdf.get("viewer")
        and lum(pdf["toolbarColor"]) < 0.4
        and lum(pdf["background"]) > 0.6,
        f"viewer loaded={pdf.get('viewer')} color-scheme={pdf.get('scheme')}"
        f" background={pdf.get('background')} toolbar text={pdf.get('toolbarColor')}"
        f" prefers-color-scheme dark={pdf.get('dark')}"
        if pdf.get("viewer")
        else f"the PDF viewer did not load: {pdf}",
    )
    r.info(
        "not covered: real macOS Dark Mode",
        "ui.systemUsesDarkTheme overrides the LookAndFeel value only, not"
        " NSApp.effectiveAppearance, so native menus, dialogs, context menus and the"
        " title bar need one manual look under System Settings > Appearance > Dark",
    )


@check("search", "5", "Search: Google and Bing only")
def check_search(ctx, r):
    with browser(ctx) as client:
        data = run_js(client, JS_SEARCH)
    r.add(
        "visible engines are exactly Google and Bing",
        sorted(data["visible"]) == ["Bing", "Google"],
        data["visible"],
    )
    r.add(
        "no other engine is installed",
        sorted(data["all"]) == ["Bing", "Google"],
        data["all"],
    )
    r.add(
        "Google is the default (normal and private)",
        data["default"] == "Google" and data["defaultPrivate"] == "Google",
        f"default={data['default']} private={data['defaultPrivate']}",
    )
    clean = {
        "Google": "https://www.google.com/search?q=m6e+test",
        "Bing": "https://www.bing.com/search?q=m6e+test",
    }
    r.add(
        "search URLs carry no partner codes (D4)",
        all(data["submissions"].get(n) == u for n, u in clean.items()),
        data["submissions"],
    )
    r.add(
        "adding engines is blocked (D5)",
        data["bundledOnly"]
        and data["bundledOnlyLocked"]
        and not data["policyAllowsInstall"]
        and data["userEngine"] != "ADDED"
        and data["openSearch"] != "ADDED"
        and sorted(data["afterAttempts"]) == ["Bing", "Google"],
        f"user={data['userEngine']} opensearch={data['openSearch']}"
        f" locked={data['bundledOnlyLocked']} policyAllowsInstall={data['policyAllowsInstall']}",
    )
    r.add(
        "Settings has no Add button",
        not data["addButtons"],
        data["addButtons"] or "none",
    )
    shown = list(dict.fromkeys(data["uiEngines"]))
    r.add(
        "Settings offers only Google and Bing as default engines",
        sorted(shown) == ["Bing", "Google"],
        shown,
    )


@check("removed", "6", "Removed features are absent")
def check_removed(ctx, r):
    with browser(ctx) as client:
        data = run_js(
            client,
            JS_REMOVED,
            ELEMENTS=REMOVED_ELEMENTS,
            PREFS=REMOVED_PREFS,
            CONSTANTS=REMOVED_CONSTANTS,
            ABOUT=REMOVED_ABOUT,
            GROUPS=REMOVED_SETTINGS_GROUPS,
        )
    for feature, elements in data["elements"].items():
        present = [i for i, state in elements.items() if state == "PRESENT"]
        absent = sum(state == "absent" for state in elements.values())
        r.add(
            f"UI: {feature}",
            not present,
            present
            or f"{len(elements)} elements gone ({len(elements) - absent} hidden,"
            f" {absent} not in the window at all)",
        )
    for feature, prefs in data["prefs"].items():
        bad = [
            f"{n}={p['value']}{'' if p['locked'] else ' (not locked)'}"
            for n, p in prefs.items()
            if p["value"] != p["want"] or not p["locked"]
        ]
        r.add(f"prefs: {feature}", not bad, bad or f"{len(prefs)} prefs off and locked")
    for feature, c in data["constants"].items():
        r.add(
            f"build: {feature}",
            not c["value"],
            f"AppConstants.{c['name']}={c['value']}",
        )
    r.add(
        "Nimbus cannot enrol",
        not data["nimbus"]["enrollmentModule"] and not data["nimbus"]["enabled"],
        f"enrolment code present={data['nimbus']['enrollmentModule']},"
        f" ExperimentAPI.enabled={data['nimbus']['enabled']}",
    )
    r.add(
        "Pocket is off",
        data["pocketPref"] in (None, False),
        f"extensions.pocket.enabled={data['pocketPref']}",
    )
    r.add(
        "sidebar has no Synced Tabs, Firefox View or Pocket tool",
        not data["sidebarSyncTools"],
        data["sidebarSyncTools"] or "none",
    )
    about = data["about"]
    r.add(
        "about: pages of removed features are unregistered",
        not about["registered"],
        about["registered"] or f"{len(about['absent'])} pages unregistered",
    )
    panes = data["panes"]
    shown = {
        pane: sorted(set(groups) & set(REMOVED_SETTINGS_GROUPS))
        for pane, groups in panes["groups"].items()
    }
    shown = {pane: groups for pane, groups in shown.items() if groups}
    r.add(
        "Settings shows no group of a removed feature",
        not shown and panes["groups"],
        shown or f"{len(panes['groups'])} panes checked",
    )
    named = [c for c in panes["categories"] if REMOVED_SETTINGS_TEXT.search(c)]
    r.add(
        "Settings has no category for a removed feature",
        not named,
        named or panes["categories"],
    )
    r.facts["registered about: pages"] = about["allRegistered"]


@check("bookmarks", "7a", "Bookmarks toolbar setting")
def check_bookmarks(ctx, r):
    with browser(ctx) as client:
        data = run_js(client, JS_BOOKMARKS)
    r.add(
        "Appearance has a Show bookmarks toolbar switch",
        data["groupVisible"] and data["toggleFound"],
        data.get("label"),
    )
    if not data["toggleFound"]:
        return
    r.add(
        "the toolbar is hidden by default",
        data["defaultValue"] == "never"
        and data["collapsedAtStart"]
        and not data["pressedAtStart"],
        f"pref={data['defaultValue']} collapsed={data['collapsedAtStart']}",
    )
    on, off = data["afterOn"], data["afterOff"]
    r.add(
        "switching it on shows the toolbar",
        on["pref"] == "always" and not on["collapsed"] and on["pressed"],
        on,
    )
    r.add(
        "switching it off hides the toolbar",
        off["pref"] == "never" and off["collapsed"] and not off["pressed"],
        off,
    )
    r.add(
        "importing from other browsers is available",
        bool(data["migrators"]),
        data["migrators"],
    )


@check("reader", "7b", "Reader View rules")
def check_reader(ctx, r):
    with browser(ctx) as client:
        data = run_js(client, JS_READER, URL=ctx.base_url + "/article.html")
    r.add("the Reader View button appears on the article", data["buttonVisible"])
    if not data["buttonVisible"]:
        return
    r.add("clicking it opens Reader View", data["openedReader"], data["url"][:80])
    s = data["styles"]
    if s.get("missing"):
        r.add("reader content rendered", False, "no .moz-reader-content")
        return
    r.add(
        "text is 25% larger (25 px instead of 20 px)",
        s["paragraphFontSize"] == "25px",
        s["paragraphFontSize"],
    )
    r.add(
        "text is #000000 on #FFFFFF with no background image",
        s["bodyColor"] == "rgb(0, 0, 0)"
        and s["bodyBackground"] == "rgb(255, 255, 255)"
        and s["bodyBackgroundImage"] == "none",
        f"{s['bodyColor']} on {s['bodyBackground']}, image {s['bodyBackgroundImage']}",
    )
    coloured = s["colouredParagraph"] or {}
    r.add(
        "page colours are removed",
        coloured.get("color") == "rgb(0, 0, 0)"
        and not coloured.get("inline")
        and not s["colorsMenuVisible"],
        coloured,
    )
    r.add(
        "navigation, sidebars, iframes and forms are stripped",
        not any(s["counts"].values()),
        s["counts"],
    )
    r.add(
        "ads are stripped",
        not s["adText"] and not s["adLabel"],
        f"ad text left: {s['adText']} label: {s['adLabel']}",
    )
    r.add(
        "only the essential article image is kept",
        s["images"] == ["photo.png"],
        s["images"],
    )


@check("shield", "7c", "Privacy Shield switch")
def check_shield(ctx, r):
    with browser(ctx) as client:
        data = run_js(client, JS_SHIELD)
    ui = data["ui"]
    r.add(
        "Privacy and security opens with the Privacy Shield group",
        ui["groupVisible"] and ui["firstGroupOnPane"] == "privacyShield",
        f"first group: {ui['firstGroupOnPane']}",
    )
    r.add(
        "the switch and the Maximum protection option exist",
        ui["toggleFound"] and ui["maximumFound"],
        ui["label"],
    )
    if not (ui["toggleFound"] and ui["maximumFound"]):
        return
    start, off, on = data["start"], data["off"], data["on"]
    r.add(
        "on by default with strict protection",
        start["enabled"]
        and ui["pressed"]
        and start["category"] == "strict"
        and start["gpc"]
        and not start["rfp"],
        start,
    )
    r.add(
        "standard mode: no first-party fingerprinting protection, third-party "
        "fingerprinters still blocked",
        not start["fpp"]
        and not start["fppPrivate"]
        and not start["baselineFpp"]
        and start["fpTracking"],
        {
            k: start[k]
            for k in ("rfp", "fpp", "fppPrivate", "baselineFpp", "fpTracking")
        },
    )
    r.add(
        "switching it off lifts the hardening and disables Maximum",
        not off["enabled"]
        and not off["pressed"]
        and not off["gpc"]
        and off["maximumDisabled"],
        {k: off[k] for k in ("enabled", "gpc", "trim", "maximumDisabled")},
    )
    r.add(
        "switching it on restores the hardening",
        on["enabled"] and on["pressed"] and on["gpc"] and not on["maximumDisabled"],
        {k: on[k] for k in ("enabled", "gpc", "trim", "maximumDisabled")},
    )
    r.add(
        "Maximum protection turns full fingerprinting resistance on and off",
        data["maximumOn"]["rfp"]
        and data["maximumOn"]["maximum"]
        and not data["maximumOff"]["rfp"]
        and not data["maximumOff"]["maximum"]
        and data["maximumOn"]["fpTracking"]
        and data["maximumOff"]["fpTracking"]
        and not data["maximumOff"]["fpp"],
        f"on: rfp={data['maximumOn']['rfp']} off: rfp={data['maximumOff']['rfp']}"
        f" fpp={data['maximumOff']['fpp']}",
    )


@check("tracker", "7d", "Tracker Blocker button and count")
def check_tracker(ctx, r):
    shim = os.path.join(ctx.out, "tracker-shim")
    shutil.rmtree(shim, ignore_errors=True)
    os.makedirs(os.path.join(shim, "obj-m6e", "dist"))
    os.makedirs(os.path.join(shim, "artifacts", "shots-P3.7"))
    os.symlink(ctx.app, os.path.join(shim, "obj-m6e", "dist", "M6E.app"))
    with ctx.lock.held():
        code, output = run_tool(
            [
                os.path.join(ROOT, "mach"),
                "python",
                os.path.join(HERE, "trackers", "tracker_count_check.py"),
            ],
            timeout=600,
            cwd=shim,
        )
    with open(os.path.join(ctx.out, "tracker.log"), "w") as fh:
        fh.write(output)
    states = {}
    for line in output.splitlines():
        m = re.match(
            r"^(shield on|shield on, site turned off|shield on, site turned back on"
            r"|shield off|shield off, site turned on): (\{.*\})$",
            line,
        )
        if m:
            states[m.group(1)] = json.loads(m.group(2))
    lists = re.search(r"^db tables: (.*)$", output, re.M)
    r.add("tool exited cleanly", code == 0, f"exit code {code}")
    r.add(
        "tracking protection lists loaded",
        bool(lists) and "analytics-track-digest256" in lists.group(1),
        lists.group(1)[:120] if lists else "no table list in the output",
    )
    on = states.get("shield on")
    if not on:
        r.add("button state read with the Shield on", False, output[-400:])
        return
    count = on["short"] if str(on["short"]).isdigit() else None
    r.add(
        "enabled: the icon shows the number of blocked items",
        on["blocked"] > 0
        and count is not None
        and int(count) > 0
        and on["protected"]
        and "has-blocked-trackers" in on["classes"],
        f"classes={on['classes']} count shown={on['short']!r} blocked={on['blocked']}",
    )
    off = states.get("shield on, site turned off")
    r.add(
        "disabled for the site: inactive icon and no number",
        bool(off)
        and off["blocked"] == 0
        and not off["protected"]
        and off["hasException"]
        and off["short"] in ("", None, "0")
        and "inactive" in off["classes"],
        off,
    )
    back = states.get("shield on, site turned back on")
    r.add(
        "turned back on for the site: protection resumes",
        bool(back) and back["protected"] and not back["hasException"],
        back,
    )
    shield_off = states.get("shield off")
    r.add(
        "Shield off: no blocking and no number",
        bool(shield_off)
        and shield_off["blocked"] == 0
        and not shield_off["protected"]
        and "inactive" in shield_off["classes"],
        shield_off,
    )
    override = states.get("shield off, site turned on")
    r.add(
        "Shield off, site turned on: the site override blocks trackers",
        bool(override) and override["protected"] and override["blocked"] > 0,
        override,
    )


@check("push", "7e", "Web Push")
def check_push(ctx, r):
    with ctx.lock.held():
        code, output = run_tool(
            [
                sys.executable,
                os.path.join(HERE, "push", "push_check.py"),
                "--binary",
                ctx.binary,
            ],
            timeout=600,
        )
    with open(os.path.join(ctx.out, "push.log"), "w") as fh:
        fh.write(output)
    for case in ("page open", "page closed", "after restart"):
        m = re.search(rf"^{case}: push (\d+), (\d+) notification", output, re.M)
        r.add(
            f"push delivered and shown: {case}",
            bool(m) and m.group(1) == "201" and int(m.group(2)) >= 1,
            m.group(0) if m else "no result line",
        )
    m = re.search(r"^runs with no windows: (\w+)", output, re.M)
    r.add(
        "app keeps running with no windows",
        bool(m) and m.group(1) == "True",
        m.group(0) if m else "no result line",
    )
    r.add(
        "push_check.py exits cleanly",
        code == 0,
        (output.strip().splitlines() or [""])[-1],
    )


@check("telemetry", "7f", "Telemetry button")
def check_telemetry(ctx, r):
    stamp = int(time.time())
    pages = [
        [
            f"local {n}",
            f"{ctx.base_url}/telemetry.html?t={stamp}&n={n}",
            "http://127.0.0.1",
        ]
        for n in (1, 2, 3)
    ] + [["https", f"https://example.com/?t={stamp}", "https://example.com"]]
    with browser(ctx) as client:
        data = run_js(client, JS_TELEMETRY, timeout=240, PAGES=pages)["pages"]
    local = data["local 1"]
    r.add(
        "the Telemetry button is shown with its tooltip",
        local["buttonVisible"] and local["tooltip"] == "Telemetry",
        local["tooltip"],
    )
    r.add(
        "the panel opens on click", local["panelState"] == "open", local["panelState"]
    )
    rows = {row["metric"]: row for row in local["rows"]}
    r.add(
        "the panel lists the seven figures",
        [row["metric"] for row in local["rows"]]
        == ["ttfb", "fcp", "tti", "size", "scriptParse", "scriptExecution", "tls"],
        [f"{row['label']}: {row['value']}" for row in local["rows"]],
    )
    for metric in ("fcp", "tti", "scriptParse", "scriptExecution"):
        value = rows.get(metric, {}).get("value", "")
        r.add(f"{metric} is measured", bool(re.fullmatch(r"[\d,.]+ ms", value)), value)
    # A loopback response can arrive before the document's time origin, which
    # leaves responseStart at zero or below; the panel then says Not available.
    consistent = []
    for name in (n for n in data if n.startswith("local")):
        shown = {row["metric"]: row["value"] for row in data[name]["rows"]}.get(
            "ttfb", ""
        )
        start = (data[name]["navigation"] or {}).get("responseStart")
        measured = re.fullmatch(r"([\d,.]+) ms", shown)
        if start is None:
            ok = False
        elif measured:
            ok = (
                start > 0
                and abs(float(measured.group(1).replace(",", "")) - start) <= 1
            )
        else:
            ok = shown == "Not available" and start <= 0
        consistent.append((ok, f"{name}: {shown} (responseStart {start})"))
    r.add(
        "ttfb on local pages is consistent with the page's own responseStart",
        all(ok for ok, _ in consistent),
        "; ".join(detail for _, detail in consistent),
    )
    r.info(
        "ttfb on local pages",
        "Not available with a responseStart at or below 0 is the known Navigation"
        " Timing quirk of loopback servers, not a failure; the real test of the"
        " figure is the https page below",
    )
    size = rows.get("size", {}).get("value", "")
    requests = re.search(r"\((\d+) requests", size)
    r.add(
        "page size counts the page's requests",
        bool(requests) and int(requests.group(1)) >= 3,
        size,
    )
    execution = re.match(r"[\d,.]+", rows.get("scriptExecution", {}).get("value", ""))
    r.add(
        "script execution time includes the 30 ms inline loop",
        bool(execution) and float(execution.group(0).replace(",", "")) >= 25,
        rows.get("scriptExecution", {}).get("value"),
    )
    r.add(
        "an http page is reported as not encrypted",
        "Not encrypted" in rows.get("tls", {}).get("value", ""),
        rows.get("tls", {}).get("value"),
    )
    for name, entry in data.items():
        r.facts[f"{name}: rows"] = [
            f"{row['label']}: {row['value']}" for row in entry["rows"]
        ]
        r.facts[f"{name}: navigation timing"] = entry["navigation"]
    secure = {row["metric"]: row["value"] for row in data["https"]["rows"]}
    r.add(
        "ttfb is measured on the https page",
        bool(re.fullmatch(r"[\d,.]+ ms", secure.get("ttfb", ""))),
        secure.get("ttfb"),
    )
    r.add(
        "an https page reports its handshake time",
        bool(re.fullmatch(r"[\d,.]+ ms", secure.get("tls", "")))
        or "Reused connection" in secure.get("tls", ""),
        secure.get("tls"),
    )


def parse_resolver_logs(log_dir):
    native, trr = {}, {}
    for name in os.listdir(log_dir):
        with open(os.path.join(log_dir, name), errors="replace") as fh:
            for line in fh:
                for pattern, store in (
                    (r"NativeLookup host:(\S+)", native),
                    (r"TrrLookup host:(\S+)", trr),
                ):
                    m = re.search(pattern, line)
                    if m:
                        host = m.group(1).lower().rstrip(".")
                        store[host] = store.get(host, 0) + 1
    return native, trr


JS_TRR_CONFIRMED = """
const dns = Cc["@mozilla.org/network/dns-service;1"].getService(Ci.nsIDNSService);
for (let i = 0; i < 60 && dns.currentTrrConfirmationState != Ci.nsIDNSService.CONFIRM_OK; i++) {
  await sleep(500);
}
return dns.currentTrrConfirmationState;
"""


def visit(client, url):
    client.set_context(client.CONTEXT_CONTENT)
    error = None
    try:
        client.navigate(url)
    except Exception as e:
        error = str(e)[:200]
    time.sleep(2)
    client.set_context(client.CONTEXT_CHROME)
    return {"error": error, **run_js(client, JS_NAV_STATE)}


def d25_attempt(ctx, number):
    """Visit the blocked domains once, with the resolver logged to a fresh directory."""
    log_dir = os.path.join(ctx.out, f"d25-logs-{number}")
    shutil.rmtree(log_dir, ignore_errors=True)
    os.makedirs(log_dir)
    env = {
        "MOZ_LOG": "timestamp,nsHostResolver:5,TRR:5",
        "MOZ_LOG_FILE": os.path.join(log_dir, "resolver.log"),
    }
    states = {}
    with browser(ctx, env=env) as client:
        confirmation = run_js(client, JS_TRR_CONFIRMED, timeout=60)
        visit(client, "https://example.com/")
        for url in (
            *(f"https://{d}/" for d in BLOCKED_DOMAINS),
            "https://example.com/",
        ):
            states[url] = visit(client, url)
        trr_mode = run_js(
            client, "return Services.prefs.getIntPref('network.trr.mode');"
        )
        provider = run_js(
            client,
            "return Services.prefs.getStringPref('network.trr.default_provider_uri');",
        )
    native, trr = parse_resolver_logs(log_dir)
    return {
        "states": states,
        "native": native,
        "trr": trr,
        "mode": trr_mode,
        "provider": provider,
        "confirmation": confirmation,
        "log_dir": str(log_dir),
    }


@check("d25", "8", "D25: malware and phishing blocked by DNS")
def check_d25(ctx, r):
    attempt = d25_attempt(ctx, 1)
    leaked = [d for d in BLOCKED_DOMAINS if attempt["native"].get(d, 0)]
    if leaked:
        r.info(
            "first attempt looked a blocked domain up natively, so the check ran again",
            f"{leaked} with TRR confirmation state {attempt['confirmation']}"
            f" (2 is confirmed); resolver log in {attempt['log_dir']}",
        )
        attempt = d25_attempt(ctx, 2)
    states, native, trr = attempt["states"], attempt["native"], attempt["trr"]
    r.add(
        f"the default resolver is https://{DOH_HOST}/dns-query in mode 2",
        attempt["provider"] == f"https://{DOH_HOST}/dns-query" and attempt["mode"] == 2,
        f"{attempt['provider']} mode {attempt['mode']}; TRR confirmation state"
        f" {attempt['confirmation']} before the first visit (2 is confirmed)",
    )
    for domain in BLOCKED_DOMAINS:
        state = states[f"https://{domain}/"]
        document = state.get("documentURI") or ""
        r.add(
            f"{domain} shows the server-not-found page",
            "about:neterror" in document and "e=dnsNotFound" in document,
            f"{document[:100]}" + (f" ({state['error']})" if state["error"] else ""),
        )
        r.add(
            f"{domain}: no native DNS lookup, resolved over DoH",
            native.get(domain, 0) == 0 and trr.get(domain, 0) > 0,
            f"native lookups={native.get(domain, 0)} DoH lookups={trr.get(domain, 0)};"
            f" resolver log in {attempt['log_dir']}",
        )
    normal = states["https://example.com/"]
    r.add(
        "a normal site still loads",
        not normal["error"]
        and normal["uri"].startswith("https://example.com")
        and "about:neterror" not in (normal.get("documentURI") or ""),
        f"{normal['uri']} title={normal.get('title')!r}",
    )


@check("hardening", "9", "Hardening locks")
def check_hardening(ctx, r):
    with browser(ctx) as client:
        data = run_js(client, JS_HARDENING, URL=ctx.base_url + "/plain.html")
    r.add(
        "content sandbox level 3, locked, and in effect",
        data["sandboxLevel"] == 3
        and data["sandboxLocked"]
        and data["sandboxEffective"] == 3,
        f"pref {data['sandboxLevel']} locked={data['sandboxLocked']}"
        f" effective {data['sandboxEffective']}",
    )
    r.add(
        "Fission on, locked, and web pages run in isolated processes",
        data["fissionPref"]
        and data["fissionLocked"]
        and data["fissionActive"]
        and "webIsolated" in data["processTypes"],
        f"pref={data['fissionPref']} locked={data['fissionLocked']}"
        f" active={data['fissionActive']} processes={data['processTypes']}",
    )
    idle = ctx.once("idle", lambda: run_idle(ctx))
    r.add(
        "nothing listens on a TCP port",
        idle["alive"]
        and idle["pids"] > 1
        and idle["open_files"] > 0
        and not idle["tcp_listeners"],
        f"{idle['pids']} processes and {idle['open_files']} open files seen by lsof"
        f" after {idle['seconds']} s"
        + (f"; LISTENING: {idle['tcp_listeners']}" if idle["tcp_listeners"] else ""),
    )
    r.facts["UDP sockets"] = len(idle["udp_sockets"])


@check("performance", "10", "Performance against Phase 0")
def check_performance(ctx, r):
    load_before = os.getloadavg()[0]
    with ctx.lock.held():
        client = Marionette(bin=ctx.binary, startup_timeout=120)
        try:
            client.start_session()
            startup = measure.measure_startup(client, ctx.startup_runs)
            memory = measure.measure_memory(client)
        finally:
            with contextlib.suppress(Exception):
                client.quit(in_app=True)
            with contextlib.suppress(Exception):
                client.cleanup()
    load_after = os.getloadavg()[0]
    paint = startup["median_ms"]["firstPaint"]
    blank = memory["blank"]["allProcessesMemory"] / 1e6
    five = memory["five_sites"]["allProcessesMemory"] / 1e6
    busy = max(load_before, load_after) > os.cpu_count()
    note = (
        f" (machine load average {load_before:.1f} before, {load_after:.1f} after"
        + (
            "; THE MACHINE WAS BUSY, so rerun this check when it is quiet"
            if busy
            else ""
        )
        + ")"
    )
    r.add(
        f"first paint, median of {ctx.startup_runs} warm runs, no slower than Phase 0",
        paint <= PHASE0["firstPaint"],
        f"{paint:.0f} ms against Phase 0 {PHASE0['firstPaint']} ms and Phase 4"
        f" {PHASE4['firstPaint']} ms; runs {[round(s['firstPaint']) for s in startup['runs']]}"
        + note,
    )
    r.add(
        "blank-tab memory no higher than Phase 0",
        blank <= PHASE0["blank_mb"],
        f"{blank:.0f} MB ({memory['blank']['processCount']} processes) against Phase 0"
        f" {PHASE0['blank_mb']} MB and Phase 4 {PHASE4['blank_mb']} MB",
    )
    r.info(
        "memory with five sites, informational (run-to-run noise can exceed 100 MB)",
        f"{five:.0f} MB, {five - PHASE0['five_sites_mb']:+.0f} MB against Phase 0"
        f" {PHASE0['five_sites_mb']} MB and {five - PHASE4['five_sites_mb']:+.0f} MB"
        f" against Phase 4 {PHASE4['five_sites_mb']} MB; navigation errors:"
        f" {memory.get('navigation_errors') or 'none'}",
    )
    r.facts["startup medians (ms)"] = {
        k: round(v) for k, v in startup["median_ms"].items()
    }


# ---------------------------------------------------------------------------
# BitTorrent (spec Phase 8, 8.8). Only synthetic fixtures served on 127.0.0.1
# are used (spec 8.9, D38): no real torrent, magnet link, info hash or tracker
# appears here. Each title says whether the check is automated.
# ---------------------------------------------------------------------------

HELPER_NAME = "m6e-torrent"
MANUAL_STEPS = [
    "Open a fixture .torrent and a magnet: link from Finder or the Dock (double click, `open -a`)"
    " and see the Add dialog (the automated checks use the dialog's own entry points).",
    "One real-network download (P8.7): a torrent the developer picks from"
    " https://fedoraproject.org/torrents/45/ or"
    " https://cdimage.debian.org/debian-cd/current/arm64/bt-dvd/ and no other (spec 8.9, D38),"
    " added from the panel, with the progress, completion, quarantine attribute and quit prompt"
    " seen by hand.",
    "Bound-interface kill switch with a real VPN interface (utun or wg): switch it off and on"
    " and watch the panel (the automated check uses an interface that does not exist).",
]

torrent_tests_module = None


def torrent_tests():
    """The helper conformance harness: its Helper class and fixture tools."""
    global torrent_tests_module
    if torrent_tests_module is None:
        torrent_tests_module = load_module(
            "m6e_torrent_tests",
            os.path.join(HERE, "torrent", "tests", "run_tests.py"),
        )
    return torrent_tests_module


def fixtures():
    torrent_tests()
    import bencode
    import make_torrent
    import swarm

    return bencode, make_torrent, swarm


def helper_binary(ctx):
    return os.path.join(ctx.app, "Contents", "MacOS", HELPER_NAME)


def app_processes(ctx):
    """pid -> executable path of the processes running from this app bundle."""
    prefixes = (os.path.realpath(ctx.app) + os.sep, os.path.abspath(ctx.app) + os.sep)
    table = subprocess.run(
        ["ps", "-axo", "pid=,comm="], capture_output=True, text=True, check=False
    ).stdout
    found = {}
    for line in table.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) == 2 and parts[1].startswith(prefixes):
            found[int(parts[0])] = parts[1]
    return found


def helper_pids(ctx):
    return sorted(
        pid
        for pid, path in app_processes(ctx).items()
        if os.path.basename(path) == HELPER_NAME
    )


def scratch_dir(ctx, name):
    path = os.path.join(ctx.out, "bt-scratch", name)
    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path)
    return path


def make_tap_seed():
    """A loopback seed that keeps everything the peer sends."""
    _, _, swarm = fixtures()

    class Tap:
        def __init__(self, conn):
            self.conn = conn
            self.received = b""

        def recv(self, n):
            data = self.conn.recv(n)
            self.received += data
            return data

        def sendall(self, data):
            if data[:20] == b"\x13BitTorrent protocol" and len(data) == 68:
                data = data[:27] + bytes([data[27] | 0x01]) + data[28:]
            return self.conn.sendall(data)

        def __getattr__(self, name):
            return getattr(self.conn, name)

    class TapSeed(swarm.Seed):
        def __init__(self, *args, **kwargs):
            self.taps = []
            super().__init__(*args, **kwargs)

        def _serve(self, conn):
            tap = Tap(conn)
            self.taps.append(tap)
            super()._serve(tap)

    return TapSeed


def peer_capabilities(tap):
    """DHT port messages (the seed claims DHT support) and the extension names offered."""
    bencode, _, _ = fixtures()
    data = tap.received
    caps = {"dht": False, "extensions": set()}
    if len(data) < 68:
        return caps
    pos = 68
    while pos + 4 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        body = data[pos + 4 : pos + 4 + length]
        pos += 4 + length
        if len(body) == 3 and body[0] == 9:
            caps["dht"] = True
        if len(body) >= 2 and body[0] == 20 and body[1] == 0:
            try:
                handshake = bencode.decode(body[2:])
            except Exception:
                continue
            caps["extensions"] |= {
                k.decode("latin-1") for k in handshake.get(b"m", {}).keys()
            }
    return caps


@contextlib.contextmanager
def loopback_swarm(makers, tap=False, **seed_args):
    """A tracker and a seed on 127.0.0.1 for fixtures built by `makers(announce)`."""
    _, _, swarm = fixtures()
    tracker = swarm.Tracker()
    made = [maker(tracker.url) for maker in makers]
    seed = (make_tap_seed() if tap else swarm.Seed)(made, **seed_args)
    tracker.peers.append(("127.0.0.1", seed.port))
    try:
        yield tracker, seed, made
    finally:
        tracker.close()
        seed.close()


@contextlib.contextmanager
def packaged_helper(ctx, name, bind="lo0", config=None):
    """The packaged helper on a scratch profile, with the sockets bound to `bind`."""
    t = torrent_tests()
    base = scratch_dir(ctx, name)
    dirs = {key: os.path.join(base, key) for key in ("save", "in", "area")}
    for path in dirs.values():
        os.makedirs(path)
    helper = t.Helper(
        [helper_binary(ctx)],
        os.path.join(base, "profile"),
        {"M6E_TORRENT_IDLE_SECONDS": "300"},
        os.path.join(base, "stderr.log"),
    )
    try:
        helper.wait_ready()
        settings = dict(config or {})
        if bind:
            settings["bindInterface"] = bind
        if settings:
            helper.req("setConfig", settings)
        yield helper, dirs
    finally:
        helper.stop()


def write_torrent(fixture, directory, name=None):
    path = os.path.join(directory, name or fixture.name + ".torrent")
    fixture.write(path)
    return path


def tree(path):
    return torrent_tests().tree(path)


def sockets_of(pid):
    try:
        return torrent_tests().sockets(pid)
    except torrent_tests().Skip:
        return []


# -- 1. idle ----------------------------------------------------------------


@check("bt_idle", "BT1", "BitTorrent 1: idle starts nothing [automated]")
def check_bt_idle(ctx, r):
    idle = ctx.once("idle", lambda: run_idle(ctx))
    requested = set(idle["http_requests"]) | set(idle["dns_only"])
    unexpected = sorted(requested - ALLOWED_HOSTS)
    r.add(
        "no m6e-torrent process in the idle app",
        idle["alive"] and idle["pids"] > 1 and not idle["helper_processes"],
        f"{idle['pids']} processes after {idle['seconds']} s;"
        f" helper processes in the tree: {idle['helper_processes'] or 'none'}",
    )
    r.add(
        "no m6e-torrent process from this app anywhere",
        not idle["helper_processes_any"],
        idle["helper_processes_any"] or "none",
    )
    r.add(
        "no TCP listener and no UDP socket on a fixed port",
        idle["alive"]
        and not idle["tcp_listeners"]
        and not [
            s
            for s in idle["udp_sockets"]
            if (udp_port(s) or 65535) < 49152
        ],
        f"TCP listeners: {idle['tcp_listeners'] or 'none'}; UDP sockets:"
        f" {len(idle['udp_sockets'])}",
    )
    r.add(
        "the allowed-host check is unchanged (BitTorrent adds no host)",
        bool(idle["http_requests"]) and not unexpected,
        unexpected or f"{len(idle['http_requests'])} hosts, all allowed",
    )
    r.info("not run here", "the first torrent starts the helper (checks BT2 and BT4)")


def udp_port(line):
    match = re.search(r":(\d+)(?:\s|$|->)", line.split("UDP", 1)[-1])
    return int(match.group(1)) if match else None


# -- the shared browser session (checks 2, 4 and 7) --------------------------

JS_BT_BEGIN = """
const g = win.gM6ETorrent;
if (!g) {
  return { reached: false, reason: "gM6ETorrent is missing" };
}
const prev = g.lastAddDialog;
win.__m6eAddState = null;
const started = __KIND__ == "magnet"
  ? g.addMagnet(__ARG__, __SOURCE__)
  : g.addTorrentFile(__ARG__, __SOURCE__);
started.then(
  value => { win.__m6eAddState = { added: value }; },
  e => { win.__m6eAddState = { error: String(e) }; }
);
for (let i = 0; i < 160; i++) {
  await sleep(250);
  if (g.lastAddDialog !== prev || win.__m6eAddState) {
    break;
  }
}
if (g.lastAddDialog === prev) {
  return { reached: false, reason: "no Add dialog opened", state: win.__m6eAddState };
}
try {
  const opened = g.lastAddDialog;
  await opened.dialog._dialogReady;
  await opened.dialog._frame.contentWindow.document.mozSubdialogReady;
} catch (e) {
  return { reached: false, reason: String(e) };
}
return { reached: true };
"""

JS_BT_STATE = """
const doc = win.gM6ETorrent.lastAddDialog.dialog._frame.contentWindow.document;
const el = id => doc.getElementById(id);
const shown = id => !!el(id) && !el(id).hidden;
return {
  ready: doc.documentElement.hasAttribute("m6e-ready"),
  banner: shown("m6e-torrent-shady"),
  fetching: shown("m6e-torrent-add-fetching"),
  error: shown("m6e-torrent-add-error") ? el("m6e-torrent-add-error").textContent : null,
  reasons: [...doc.querySelectorAll("#m6e-torrent-shady-reasons li")].map(li => ({
    id: li.getAttribute("data-l10n-id"), text: li.textContent,
  })),
  addAnywayDisabled: el("m6e-torrent-shady-add-anyway")?.disabled ?? null,
  addDisabled: el("m6e-torrent-add-confirm")?.disabled ?? null,
  name: el("m6e-torrent-add-name")?.textContent,
  savePath: el("m6e-torrent-add-save-path")?.value,
};
"""

JS_BT_CLICK = """
const doc = win.gM6ETorrent.lastAddDialog.dialog._frame.contentWindow.document;
const button = doc.getElementById(__ID__);
if (!button || button.disabled) {
  return { clicked: false, found: !!button, disabled: button?.disabled ?? null };
}
button.click();
return { clicked: true };
"""

JS_BT_SETTLE = """
for (let i = 0; i < 120 && !win.__m6eAddState; i++) {
  await sleep(250);
}
return win.__m6eAddState;
"""

JS_BT_REMOVE = """
const g = win.gM6ETorrent;
const tid = g.lastAddDialog.params.tid;
const before = (await g.service.list()).torrents.map(t => t.tid);
await g.service.remove(tid, true);
return { tid, wasListed: before.includes(tid) };
"""

JS_BT_RISK = """
const g = win.gM6ETorrent;
const added = await g.service.addFile(__PATH__, __SOURCE__);
const risk = g.risk.assess(added.metadata, __SOURCE__, []);
await g.service.cancelAdd(added.tid);
return { tid: added.tid, files: added.metadata.files.map(f => f.path), risk };
"""

JS_BT_MAGNET_SERVICE = """
const g = win.gM6ETorrent;
const waitMetadata = new Promise(resolve => {
  const observer = {
    observe(subject, topic, data) {
      Services.obs.removeObserver(observer, "m6e-torrent-metadata");
      resolve(JSON.parse(data));
    },
  };
  Services.obs.addObserver(observer, "m6e-torrent-metadata");
});
const added = await g.service.addMagnet(__URI__, __SOURCE__);
const meta = await Promise.race([waitMetadata, sleep(40000).then(() => null)]);
win.__m6eMagnet = { tid: added.tid, metadata: meta?.metadata ?? null };
return { tid: added.tid, got: !!meta, files: meta?.metadata?.files?.length ?? 0 };
"""

JS_BT_CONFIRM_SERVICE = """
const g = win.gM6ETorrent;
const { tid, metadata } = win.__m6eMagnet;
await g.service.confirmAdd(tid, {
  files: metadata.files.filter(f => !f.pad).map(f => ({ index: f.index, priority: 2 })),
  savePath: __SAVE__,
  sequential: false,
  start: true,
});
return { confirmed: true };
"""

JS_BT_CANCEL_SERVICE = """
await win.gM6ETorrent.service.cancelAdd(win.__m6eMagnet.tid);
return true;
"""

JS_BT_ADD_ACTIVE = """
const g = win.gM6ETorrent;
const added = await g.service.addFile(__PATH__, __SOURCE__);
await g.service.confirmAdd(added.tid, {
  files: added.metadata.files.filter(f => !f.pad).map(f => ({ index: f.index, priority: 2 })),
  savePath: __SAVE__,
  sequential: false,
  start: true,
});
await sleep(3000);
const listed = await g.service.list();
return { tid: added.tid, states: listed.torrents.map(t => t.state) };
"""

JS_BT_SERVICE_STATE = """
const g = win.gM6ETorrent;
const listed = await g.service.list();
return { states: listed.torrents.map(t => ({ tid: t.tid, state: t.state, progress: t.progress })) };
"""

JS_BT_PREFS = """
return {
  enabled: Services.prefs.getBoolPref("m6e.torrent.enabled", null),
  bind: Services.prefs.getStringPref("m6e.torrent.bindInterface", null),
  downloadDir: Services.prefs.getStringPref("m6e.torrent.downloadDir", null),
  hasGlobal: !!win.gM6ETorrent,
  hasButton: !!doc.getElementById("m6e-torrent-button"),
};
"""


def wait_for(predicate, timeout, interval=0.25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return predicate()


def dialog_open(client, kind, arg, source):
    return run_js(client, JS_BT_BEGIN, KIND=kind, ARG=arg, SOURCE=source)


def dialog_state(client):
    return run_js(client, JS_BT_STATE)


def dialog_click(client, button_id):
    return run_js(client, JS_BT_CLICK, ID=button_id)


def dialog_wait_ready(client, timeout=60):
    return wait_for(
        lambda: (lambda s: s if s["ready"] or s["error"] else None)(dialog_state(client)),
        timeout,
    )


def session_part(name, out, fn):
    """Run one part of the browser session; an exception becomes the part's error."""
    try:
        out[name] = fn()
    except Exception:
        out[name] = {"error": traceback.format_exc()}


def run_bt_session(ctx):
    """One browser: Add dialog, Shady banner, magnet metadata, then quit with an active torrent."""
    bencode, make_torrent, swarm = fixtures()
    t = torrent_tests()
    work = scratch_dir(ctx, "session")
    save = os.path.join(work, "downloads")
    files = os.path.join(work, "in")
    os.makedirs(save)
    os.makedirs(files)
    rules = json.load(
        open(os.path.join(ROOT, "browser", "components", "torrent", "m6e-torrent-rules.json"))
    )
    trusted = "https://" + rules["trustedSites"][0]["domain"]
    prefs = {
        "m6e.torrent.bindInterface": "lo0",
        "m6e.torrent.downloadDir": save,
        "m6e.torrent.askWhereToSave": False,
        "m6e.torrent.startNow": True,
        "m6e.torrent.confirmQuit": False,
        "m6e.torrent.resumeAtStartup": False,
    }
    out = {"trustedOrigin": trusted, "save": save}

    benign = make_torrent.multi_file(name="m6e-acceptance-benign")
    shady = make_torrent.Fixture(
        "m6e-acceptance-shady",
        [(("Movie.mkv.exe",), make_torrent.deterministic_bytes("shady", 3000))],
    )
    benign_path = write_torrent(benign, files)
    shady_path = write_torrent(shady, files)
    finder = {"kind": "finder", "origin": None}
    page = {"kind": "page", "origin": trusted}

    with browser(ctx, prefs=prefs) as client:
        out["prefs"] = run_js(client, JS_BT_PREFS)
        out["idleHelpers"] = helper_pids(ctx)

        def banner_case(path, source):
            opened = dialog_open(client, "file", path, source)
            if not opened.get("reached"):
                return {"reached": False, "reason": opened}
            state = dialog_wait_ready(client, 30)
            result = {"reached": True, "state": state}
            if state and state["banner"]:
                result["addAnyway"] = dialog_click(client, "m6e-torrent-shady-add-anyway")
                result["settled"] = run_js(client, JS_BT_SETTLE)
                result["removed"] = run_js(client, JS_BT_REMOVE)
            else:
                dialog_click(client, "m6e-torrent-add-cancel")
                result["settled"] = run_js(client, JS_BT_SETTLE)
            return result

        session_part("benignFinder", out, lambda: banner_case(benign_path, finder))
        session_part("benignTrusted", out, lambda: banner_case(benign_path, page))
        session_part("shadyFinder", out, lambda: banner_case(shady_path, finder))
        session_part("shadyTrusted", out, lambda: banner_case(shady_path, page))
        if not out["shadyFinder"].get("reached"):
            session_part(
                "shadyRisk",
                out,
                lambda: run_js(client, JS_BT_RISK, PATH=shady_path, SOURCE=finder),
            )

        def magnet_part():
            fixture = make_torrent.multi_file(name="m6e-acceptance-magnet")
            result = {}
            with loopback_swarm([lambda announce: fixture]) as (tracker, seed, _):
                uri = fixture.magnet(peers=(seed.address,))
                payload = [
                    os.path.join(save, fixture.name, *rel) for rel, _ in fixture.files
                ]
                opened = dialog_open(client, "magnet", uri, finder)
                result["viaDialog"] = bool(opened.get("reached"))
                if not result["viaDialog"]:
                    result["dialogReason"] = opened
                    meta = run_js(client, JS_BT_MAGNET_SERVICE, URI=uri, SOURCE=finder)
                    result["metadataArrived"] = meta["got"]
                else:
                    state = dialog_wait_ready(client, 60)
                    result["metadataArrived"] = bool(state and state["ready"])
                    result["dialogState"] = state
                time.sleep(4)
                result["metadataRequests"] = seed.metadata_requests
                result["blockRequestsBeforeAdd"] = seed.request_count()
                result["bytesServedBeforeAdd"] = seed.bytes_served
                result["filesBeforeAdd"] = sorted(tree(save))
                if result["viaDialog"]:
                    dialog_click(client, "m6e-torrent-add-cancel")
                    result["cancelled"] = run_js(client, JS_BT_SETTLE)
                else:
                    run_js(client, JS_BT_CANCEL_SERVICE)
                time.sleep(3)
                result["blockRequestsAfterCancel"] = seed.request_count()

                opened = dialog_open(client, "magnet", uri, finder)
                if opened.get("reached"):
                    dialog_wait_ready(client, 60)
                    result["addClick"] = dialog_click(client, "m6e-torrent-add-confirm")
                    result["added"] = run_js(client, JS_BT_SETTLE)
                else:
                    run_js(client, JS_BT_MAGNET_SERVICE, URI=uri, SOURCE=finder)
                    run_js(client, JS_BT_CONFIRM_SERVICE, SAVE=save)

                def complete():
                    return all(
                        os.path.isfile(p) and os.path.getsize(p) == len(data)
                        for p, (_, data) in zip(payload, fixture.files)
                    )

                result["downloaded"] = bool(wait_for(complete, 90))
                result["blockRequestsAfterAdd"] = seed.request_count()
                result["sha256Match"] = result["downloaded"] and all(
                    hashlib.sha256(open(p, "rb").read()).digest()
                    == hashlib.sha256(data).digest()
                    for p, (_, data) in zip(payload, fixture.files)
                )
                result["service"] = run_js(client, JS_BT_SERVICE_STATE)
            return result

        session_part("magnet", out, magnet_part)

        def quit_part():
            with loopback_swarm(
                [
                    lambda announce: make_torrent.single_file(
                        announce=announce, name="m6e-acceptance-active.bin"
                    )
                ],
                serve_blocks=False,
            ) as (tracker, seed, made):
                fixture = made[0]
                path = write_torrent(fixture, files)
                active = run_js(client, JS_BT_ADD_ACTIVE, PATH=path, SOURCE=finder, SAVE=save)
                pids = wait_for(lambda: helper_pids(ctx), 15)
                return {
                    "active": active,
                    "helperPids": pids,
                    "announces": len(tracker.announces_for(fixture.tid)),
                }

        session_part("quit", out, quit_part)
        running = out["quit"].get("helperPids") or []

    out["helperGoneAfterQuit"] = bool(
        wait_for(lambda: not [p for p in running if p in helper_pids(ctx)], 20)
    ) and bool(running)
    out["helperPidsAfterQuit"] = helper_pids(ctx)
    return out


def bt_session(ctx):
    return ctx.once("bt_session", lambda: run_bt_session(ctx))


def part(session, name):
    value = session[name]
    if isinstance(value, dict) and "error" in value and len(value) == 1:
        raise RuntimeError(f"{name} failed in the browser session:\n{value['error']}")
    return value


# -- 2. Shady Torrent banner -------------------------------------------------


@check("bt_shady", "BT2", "BitTorrent 2: Shady Torrent banner [automated]")
def check_bt_shady(ctx, r):
    s = bt_session(ctx)
    r.add(
        "the BitTorrent code is in the packaged app",
        s["prefs"]["hasGlobal"] and s["prefs"]["enabled"] is True,
        s["prefs"],
    )
    r.add(
        "a fresh browser has no helper before the first torrent",
        not s["idleHelpers"],
        s["idleHelpers"] or "none",
    )
    for name, label in (
        ("benignFinder", "an ordinary fixture torrent opened as from Finder"),
        ("benignTrusted", "the same file from a trusted https origin"),
    ):
        case = part(s, name)
        state = case.get("state") or {}
        r.add(
            f"no banner: {label}",
            case.get("reached") and state.get("ready") and not state.get("banner"),
            {k: state.get(k) for k in ("ready", "banner", "error")}
            if case.get("reached")
            else case.get("reason"),
        )
    r.add(
        "no banner: the crafted torrent from a trusted https origin",
        (lambda c: c.get("reached") and (c.get("state") or {}).get("ready") and not (c.get("state") or {}).get("banner"))(part(s, "shadyTrusted")),
        "trusted origins skip the check (spec 8.3)",
    )
    shady = part(s, "shadyFinder")
    if shady.get("reached"):
        state = shady["state"] or {}
        r.add(
            "Movie.mkv.exe fixture: the Shady Torrent banner is shown, with a reason",
            state.get("banner") and bool(state.get("reasons"))
            and "Movie.mkv.exe" in json.dumps(state["reasons"]),
            [x["text"] for x in state.get("reasons", [])],
        )
        r.add(
            "Add anyway works: the dialog closes and the torrent is added (then removed)",
            (shady.get("addAnyway") or {}).get("clicked")
            and (shady.get("settled") or {}).get("added") is True
            and (shady.get("removed") or {}).get("wasListed") is True,
            {k: shady.get(k) for k in ("addAnyway", "settled", "removed")},
        )
        r.info("how the banner was checked", "through the Add dialog in the packaged app")
    else:
        risk = part(s, "shadyRisk")
        r.add(
            "Movie.mkv.exe fixture: the Shady Torrent check returns shady, with a reason",
            risk["risk"]["verdict"] == "shady" and bool(risk["risk"]["reasons"]),
            risk["risk"],
        )
        r.info(
            "NOTE: the banner itself was not checked",
            f"the Add dialog was not reachable ({shady.get('reason')}); the verdict came from"
            " the service and M6ETorrentRisk in the packaged app, and the banner has the"
            " browser tests (tests/browser/ui)",
        )
    r.info("MANUAL", MANUAL_STEPS[0])


# -- 3. hostile paths --------------------------------------------------------

HOSTILE_KINDS = [
    "traversal",
    "traversal-mid",
    "absolute",
    "empty-component",
    "dot",
    "backslash",
    "colon",
    "nul",
    "name-dotdot",
    "name-slash",
    "single-name-traversal",
]


@check("bt_paths", "BT3", "BitTorrent 3: hostile paths never write outside [automated]")
def check_bt_paths(ctx, r):
    _, make_torrent, _ = fixtures()
    t = torrent_tests()
    with packaged_helper(ctx, "paths") as (h, dirs):
        save = dirs["save"]
        sibling = os.path.join(dirs["area"], "sibling")
        os.makedirs(sibling)
        with open(os.path.join(sibling, "marker"), "w") as fh:
            fh.write("untouched")
        before = {
            "save": tree(save),
            "area": tree(dirs["area"]),
        }
        accepted, codes = [], {}
        for kind in HOSTILE_KINDS:
            path = os.path.join(dirs["in"], f"hostile-{kind}.torrent")
            with open(path, "wb") as fh:
                fh.write(make_torrent.hostile(kind))
            reply = h.req("add", {"file": path})
            if reply.get("ok"):
                accepted.append(kind)
                tid = reply["result"]["tid"]
                h.req(
                    "confirm",
                    dict(
                        t.confirm_args(
                            reply["result"].get("metadata") or {"files": []}, save
                        ),
                        tid=tid,
                    ),
                )
            else:
                codes[kind] = reply["error"]["code"]
        time.sleep(2)
        r.add(
            "every hostile torrent is refused by the helper",
            not accepted,
            codes if not accepted else f"ACCEPTED: {accepted}",
        )
        r.add(
            "the refusals are unsafe-path or invalid-torrent",
            set(codes.values()) <= {"unsafe-path", "invalid-torrent"},
            sorted(set(codes.values())),
        )

        with loopback_swarm([lambda announce: make_torrent.multi_file(announce)]) as (
            tracker,
            seed,
            made,
        ):
            fixture = made[0]
            os.symlink(sibling, os.path.join(save, fixture.name))
            added = h.call("add", {"file": write_torrent(fixture, dirs["in"])})
            reply = h.req(
                "confirm",
                dict(t.confirm_args(added["metadata"], save), tid=added["tid"]),
            )
            time.sleep(8)
            r.add(
                "a symbolic link in the save folder is not followed",
                sorted(os.listdir(sibling)) == ["marker"]
                and open(os.path.join(sibling, "marker")).read() == "untouched",
                f"confirm answered {reply.get('ok')} {reply.get('error', {}).get('code')};"
                f" the folder it points to holds {sorted(os.listdir(sibling))};"
                f" the seed was asked for {seed.request_count()} blocks",
            )
            os.unlink(os.path.join(save, fixture.name))
        outside = {
            p for p in tree(dirs["area"]) - before["area"] if not p.startswith("save")
        }
        r.add(
            "nothing was created next to or above the save folder",
            not outside and tree(save) == before["save"],
            sorted(outside) or "the save folder is as it was",
        )
        r.info("scope", "the packaged helper; the service applies the same rules first (xpcshell tests)")


# -- 4. magnet --------------------------------------------------------------


@check("bt_magnet", "BT4", "BitTorrent 4: a magnet link downloads nothing before Add [automated]")
def check_bt_magnet(ctx, r):
    _, make_torrent, _ = fixtures()
    t = torrent_tests()
    with packaged_helper(ctx, "magnet") as (h, dirs):
        fixture = make_torrent.multi_file(name="m6e-acceptance-magnet-helper")
        with loopback_swarm([lambda announce: fixture]) as (tracker, seed, _):
            uri = fixture.magnet(peers=(seed.address,))
            since = h.mark()
            added = h.call("add", {"magnet": uri})
            h.wait_event("metadata", timeout=40, since=since)
            time.sleep(4)
            r.add(
                "helper: the metadata arrived and no block was requested",
                seed.metadata_requests > 0
                and seed.request_count() == 0
                and seed.bytes_served == 0,
                f"metadata requests {seed.metadata_requests}, block requests"
                f" {seed.request_count()}, bytes {seed.bytes_served}",
            )
            r.add(
                "helper: no file was created in the save folder",
                not tree(dirs["save"]),
                sorted(tree(dirs["save"])) or "empty",
            )
            h.call("cancel", {"tid": added["tid"]})
            time.sleep(2)
            r.add(
                "helper: after Cancel still nothing was requested",
                seed.request_count() == 0 and not tree(dirs["save"]),
                f"block requests {seed.request_count()}",
            )
    s = bt_session(ctx)
    m = part(s, "magnet")
    how = "Add dialog" if m.get("viaDialog") else "service"
    r.add(
        f"browser ({how}): the metadata arrived through the loopback peer",
        m.get("metadataArrived") and m.get("metadataRequests", 0) > 0,
        {k: m.get(k) for k in ("metadataArrived", "metadataRequests")},
    )
    r.add(
        f"browser ({how}): no block requested and no file written before Add",
        m.get("blockRequestsBeforeAdd") == 0
        and m.get("bytesServedBeforeAdd") == 0
        and not m.get("filesBeforeAdd"),
        {
            k: m.get(k)
            for k in ("blockRequestsBeforeAdd", "bytesServedBeforeAdd", "filesBeforeAdd")
        },
    )
    r.add(
        f"browser ({how}): Cancel keeps it that way",
        m.get("blockRequestsAfterCancel") == 0,
        m.get("blockRequestsAfterCancel"),
    )
    r.add(
        f"browser ({how}): Add then downloads every file, and the contents match",
        m.get("downloaded") and m.get("sha256Match") and m.get("blockRequestsAfterAdd", 0) > 0,
        {k: m.get(k) for k in ("downloaded", "sha256Match", "blockRequestsAfterAdd", "addClick")},
    )
    if not m.get("viaDialog"):
        r.info(
            "NOTE: the Add dialog was not reachable for the magnet link",
            f"{m.get('dialogReason')}; the same steps ran through M6ETorrentService",
        )


# -- 5. private torrent -----------------------------------------------------


@check("bt_private", "BT5", "BitTorrent 5: a private torrent uses no DHT, PEX or LSD [automated]")
def check_bt_private(ctx, r):
    _, make_torrent, _ = fixtures()
    t = torrent_tests()
    config = {"dht": True, "pex": True, "lpd": True}
    results = {}
    for label, private in (("public", False), ("private", True)):
        with packaged_helper(ctx, f"private-{label}", config=config) as (h, dirs):
            with loopback_swarm(
                [
                    lambda announce, p=private, n=label: make_torrent.multi_file(
                        announce, private=p, name=f"m6e-acceptance-{n}"
                    )
                ],
                tap=True,
            ) as (tracker, seed, made):
                fixture = made[0]
                since = h.mark()
                added = t.add_file(h, write_torrent(fixture, dirs["in"]))
                h.call(
                    "confirm",
                    dict(t.confirm_args(added["metadata"], dirs["save"]), tid=added["tid"]),
                )
                t.wait_done(h, added["tid"], since, timeout=90)
                time.sleep(1)
                taps = [peer_capabilities(tap) for tap in seed.taps if tap.received]
                udp = [
                    line
                    for line in sockets_of(h.pid)
                    if "UDP" in line and ":6771" in line
                ]
                results[label] = {
                    "dht": any(c["dht"] for c in taps),
                    "extensions": sorted(set().union(*[c["extensions"] for c in taps])),
                    "lsdSocket": udp,
                    "connections": len(taps),
                    "isPrivate": added["metadata"]["isPrivate"],
                }
    public, private = results["public"], results["private"]
    r.add(
        "control: with DHT, PEX and LSD switched on, a public torrent sends a DHT port"
        " message, offers ut_pex and opens the LSD socket (so the check can see them)",
        public["connections"] > 0
        and public["dht"]
        and "ut_pex" in public["extensions"]
        and bool(public["lsdSocket"]),
        public,
    )
    r.add(
        "the private torrent downloaded from the loopback seed",
        private["connections"] > 0 and private["isPrivate"],
        private,
    )
    r.add(
        "private: no DHT port message is sent to the peer",
        not private["dht"],
        private["dht"],
    )
    r.add(
        "private: ut_pex is not offered",
        "ut_pex" not in private["extensions"],
        private["extensions"],
    )
    r.add(
        "private: no local-discovery socket (UDP 6771)",
        not private["lsdSocket"],
        private["lsdSocket"] or "none",
    )


# -- 6. bound interface missing ---------------------------------------------


@check("bt_interface", "BT6", "BitTorrent 6: a missing bound interface opens no connection [automated]")
def check_bt_interface(ctx, r):
    _, make_torrent, _ = fixtures()
    t = torrent_tests()
    outcome = {}
    for label, interface in (("missing", "m6e-no-such0"), ("control", "lo0")):
        with packaged_helper(ctx, f"iface-{label}", bind=None) as (h, dirs):
            with loopback_swarm([lambda announce: make_torrent.multi_file(announce)]) as (
                tracker,
                seed,
                made,
            ):
                fixture = made[0]
                config = h.req("setConfig", {"bindInterface": interface})
                added = t.add_file(h, write_torrent(fixture, dirs["in"]))
                confirm = h.req(
                    "confirm",
                    dict(t.confirm_args(added["metadata"], dirs["save"]), tid=added["tid"]),
                )
                time.sleep(10 if label == "missing" else 6)
                outcome[label] = {
                    "setConfig": config.get("ok"),
                    "confirm": confirm.get("ok"),
                    "announces": len(tracker.announces_for(fixture.tid)),
                    "handshakes": len(seed.handshakes),
                    "blocks": seed.request_count(),
                    "sockets": sockets_of(h.pid),
                    "outside": sorted(tree(dirs["save"])),
                }
    r.add(
        "control: bound to lo0, the helper reaches the tracker and the seed",
        outcome["control"]["announces"] > 0 and outcome["control"]["handshakes"] > 0,
        outcome["control"],
    )
    missing = outcome["missing"]
    r.add(
        "missing interface: no tracker announce, no peer connection, no block requested",
        missing["announces"] == 0 and missing["handshakes"] == 0 and missing["blocks"] == 0,
        missing,
    )
    r.add(
        "missing interface: the helper holds no TCP or UDP socket",
        not missing["sockets"],
        missing["sockets"] or "none",
    )
    r.info(
        "MANUAL",
        MANUAL_STEPS[2],
    )


# -- 7. quit ----------------------------------------------------------------


@check("bt_quit", "BT7", "BitTorrent 7: quitting leaves no helper process [automated]")
def check_bt_quit(ctx, r):
    s = bt_session(ctx)
    q = part(s, "quit")
    r.add(
        "an active torrent had started the helper before the quit (not vacuous)",
        bool(q.get("helperPids"))
        and bool({"downloading", "queued", "checking"} & set((q.get("active") or {}).get("states", []))),
        {k: q.get(k) for k in ("helperPids", "active", "announces")},
    )
    r.add(
        "no m6e-torrent process from this app remains after the quit",
        s["helperGoneAfterQuit"] and not s["helperPidsAfterQuit"],
        f"left: {s['helperPidsAfterQuit'] or 'none'}",
    )


# -- 8. signature -----------------------------------------------------------

check_helper_module = None


def load_check_helper():
    global check_helper_module
    if check_helper_module is None:
        check_helper_module = load_module(
            "m6e_check_helper",
            os.path.join(HERE, "package", "check-helper.py"),
        )
    return check_helper_module


@check("bt_signed", "BT8", "BitTorrent 8: helper signed with the hardened runtime [automated]")
def check_bt_signed(ctx, r):
    items = load_check_helper().check_helper(ctx.app)
    for name, ok, detail in items:
        r.add(name, ok, detail)
    proc = subprocess.run(
        ["codesign", "--verify", "--deep", "--strict", ctx.app],
        capture_output=True,
        text=True,
        check=False,
    )
    r.add(
        "the whole app verifies with the helper in it",
        proc.returncode == 0,
        (proc.stderr.strip().splitlines() or [""])[-1],
    )
    r.info(
        "also run by release.sh",
        "tools/m6e/package/check-helper.py and check-signed.js (the helper launched by the signed app)",
    )


# -- 9. Launch Services -----------------------------------------------------

LSREGISTER = (
    "/System/Library/Frameworks/CoreServices.framework/Frameworks/"
    "LaunchServices.framework/Support/lsregister"
)


def launch_services_apps(url=None, path=None):
    """Paths of the apps Launch Services offers for a URL or a file (None if unusable)."""
    import ctypes

    cf = ctypes.cdll.LoadLibrary(
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
    )
    cs = ctypes.cdll.LoadLibrary(
        "/System/Library/Frameworks/CoreServices.framework/CoreServices"
    )
    vp = ctypes.c_void_p
    cf.CFStringCreateWithCString.restype = vp
    cf.CFStringCreateWithCString.argtypes = [vp, ctypes.c_char_p, ctypes.c_uint32]
    cf.CFURLCreateWithString.restype = vp
    cf.CFURLCreateWithString.argtypes = [vp, vp, vp]
    cf.CFURLCreateFromFileSystemRepresentation.restype = vp
    cf.CFURLCreateFromFileSystemRepresentation.argtypes = [
        vp,
        ctypes.c_char_p,
        ctypes.c_long,
        ctypes.c_ubyte,
    ]
    cf.CFArrayGetCount.restype = ctypes.c_long
    cf.CFArrayGetCount.argtypes = [vp]
    cf.CFArrayGetValueAtIndex.restype = vp
    cf.CFArrayGetValueAtIndex.argtypes = [vp, ctypes.c_long]
    cf.CFURLCopyFileSystemPath.restype = vp
    cf.CFURLCopyFileSystemPath.argtypes = [vp, ctypes.c_long]
    cf.CFStringGetCString.restype = ctypes.c_ubyte
    cf.CFStringGetCString.argtypes = [vp, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
    cs.LSCopyApplicationURLsForURL.restype = vp
    cs.LSCopyApplicationURLsForURL.argtypes = [vp, ctypes.c_uint32]
    utf8 = 0x08000100
    if url:
        text = cf.CFStringCreateWithCString(None, url.encode(), utf8)
        target = cf.CFURLCreateWithString(None, text, None)
    else:
        encoded = path.encode()
        target = cf.CFURLCreateFromFileSystemRepresentation(None, encoded, len(encoded), 0)
    apps = cs.LSCopyApplicationURLsForURL(target, 0xFFFFFFFF)
    if not apps:
        return []
    found = []
    for i in range(cf.CFArrayGetCount(apps)):
        item = cf.CFArrayGetValueAtIndex(apps, i)
        text = cf.CFURLCopyFileSystemPath(item, 0)
        buffer = ctypes.create_string_buffer(4096)
        if cf.CFStringGetCString(text, buffer, 4096, utf8):
            found.append(os.path.realpath(buffer.value.decode()))
    return found


@check("bt_launchservices", "BT9", "BitTorrent 9: Launch Services lists M6E for .torrent and magnet: [automated]")
def check_bt_launchservices(ctx, r):
    info = plist_strings(os.path.join(ctx.app, "Contents", "Info.plist"))
    types = info.get("CFBundleDocumentTypes", [])
    extensions = {e for t in types for e in t.get("CFBundleTypeExtensions", [])}
    mimes = {m for t in types for m in t.get("CFBundleTypeMIMETypes", [])}
    schemes = {
        s for t in info.get("CFBundleURLTypes", []) for s in t.get("CFBundleURLSchemes", [])
    }
    r.add(
        "Info.plist declares .torrent and the magnet scheme",
        "torrent" in extensions and "application/x-bittorrent" in mimes and "magnet" in schemes,
        f"extensions {sorted(extensions)[:8]}, schemes {sorted(schemes)}",
    )
    app = os.path.realpath(ctx.app)
    registered = subprocess.run(
        [LSREGISTER, "-f", ctx.app], capture_output=True, text=True, check=False
    )
    r.info("lsregister -f", registered.returncode)
    scratch = scratch_dir(ctx, "launchservices")
    sample = os.path.join(scratch, "sample.torrent")
    _, make_torrent, _ = fixtures()
    make_torrent.multi_file(name="m6e-acceptance-ls").write(sample)
    magnet = "magnet:?xt=urn:btih:" + "0" * 40

    def offered(**kwargs):
        try:
            return app in launch_services_apps(**kwargs)
        except Exception:
            return None

    torrent_ok = wait_for(lambda: offered(path=sample), 20)
    magnet_ok = wait_for(lambda: offered(url=magnet), 20)
    r.add(
        "Launch Services lists M6E as an application that can open a .torrent file",
        torrent_ok,
        f"{app}: {torrent_ok}",
    )
    r.add(
        "Launch Services lists M6E as an application that can open a magnet: link",
        magnet_ok,
        f"{app}: {magnet_ok}",
    )
    r.info(
        "scope",
        "the apps offered, not the default; M6E becomes the default only when the user chooses (D33)",
    )
    if not app.startswith("/Applications/"):
        subprocess.run([LSREGISTER, "-u", ctx.app], capture_output=True, check=False)
        r.info("lsregister -u", "the copy under test was unregistered again")


def run_check(ctx, entry):
    result = Result()
    started = time.time()
    error = None
    try:
        entry["fn"](ctx, result)
    except Exception:
        error = traceback.format_exc()
    checked = [i for i in result.items if not i.get("info")]
    failed = [i["name"] for i in checked if not i["ok"]]
    passed = bool(checked) and not failed and error is None
    if error:
        summary = "exception: " + error.strip().splitlines()[-1]
    elif not checked:
        summary = "no item was checked"
    elif failed:
        summary = f"{len(failed)} of {len(checked)} failed: " + "; ".join(failed[:3])
    else:
        summary = f"{len(checked)} items passed"
    return {
        "key": entry["key"],
        "label": entry["label"],
        "title": entry["title"],
        "status": "PASS" if passed else "FAIL",
        "summary": summary,
        "items": result.items,
        "facts": result.facts,
        "error": error,
        "seconds": round(time.time() - started, 1),
    }


def render_table(results):
    width = max(len(c["title"]) for c in results)
    lines = [f"{'#':<4}{'Check':<{width + 2}}{'Result':<8}Summary"]
    for c in results:
        lines.append(
            f"{c['label']:<4}{c['title']:<{width + 2}}{c['status']:<8}{c['summary'][:110]}"
        )
    return "\n".join(lines)


def render_report(meta, results):
    passed = sum(c["status"] == "PASS" for c in results)
    lines = [
        "# M6E acceptance report",
        "",
        f"- App: `{meta['app']}`",
        f"- Date: {meta['date']}",
        f"- Machine: {meta['machine']}",
        f"- Result: {passed} of {len(results)} checks passed",
        "",
        "| # | Check | Result | Summary |",
        "|---|---|---|---|",
    ]
    lines += [
        f"| {c['label']} | {c['title']} | {c['status']} | {c['summary'].replace('|', '/')} |"
        for c in results
    ]
    if any(c["key"].startswith("bt_") for c in results):
        lines += ["", "Manual BitTorrent steps this script does not run:", ""]
        lines += [f"- {step}" for step in MANUAL_STEPS]
    for c in results:
        lines += [
            "",
            f"## {c['label']}. {c['title']}: {c['status']} ({c['seconds']} s)",
            "",
        ]
        for item in c["items"]:
            detail = item["detail"].replace("\n", " ")
            status = "INFO" if item.get("info") else "PASS" if item["ok"] else "FAIL"
            lines.append(
                f"- {status}: {item['name']}" + (f" ({detail})" if detail else "")
            )
        for name, value in c["facts"].items():
            lines.append(f"- {name}: `{json.dumps(value)[:600]}`")
        if c["error"]:
            lines += ["", "```", c["error"].rstrip(), "```"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", help="path to M6E.app")
    parser.add_argument("--out", help="directory for results.json and report.md")
    parser.add_argument("--idle-seconds", type=int, default=300)
    parser.add_argument("--startup-runs", type=int, default=5)
    parser.add_argument("--only", help="comma-separated check keys")
    parser.add_argument("--list", action="store_true", help="list the checks")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--lock-dir", help="mkdir/rmdir mutex around every browser run")
    args = parser.parse_args()

    if args.list:
        for c in CHECKS:
            print(f"{c['key']:<12}{c['label']:<5}{c['title']}")
        return 0
    if not args.app or not args.out:
        parser.error("--app and --out are required")
    selected = [c for c in CHECKS if not args.only or c["key"] in args.only.split(",")]
    unknown = (
        set(args.only.split(",")) - {c["key"] for c in CHECKS} if args.only else set()
    )
    if unknown:
        parser.error(f"unknown checks: {sorted(unknown)}")

    ctx = Context(args)
    if not os.access(ctx.binary, os.X_OK):
        parser.error(f"no executable at {ctx.binary}")
    if os.path.realpath(ctx.app).startswith("/private/tmp/"):
        print(
            "warning: the app is under /tmp. Copies there showed intermittent"
            " page-load hangs in the tests, in every build; test a copy in your home"
            " folder or /Users/Shared instead.",
            file=sys.stderr,
        )
    os.makedirs(ctx.out, exist_ok=True)
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))

    meta = {
        "app": ctx.app,
        "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": f"{os.uname().machine}, {os.cpu_count()} cores, load average"
        f" {os.getloadavg()[0]:.1f} at start",
    }
    results = []
    try:
        for entry in selected:
            print(
                f"[{entry['label']}] {entry['title']} ...", file=sys.stderr, flush=True
            )
            results.append(run_check(ctx, entry))
            print(
                f"[{entry['label']}] {results[-1]['status']}: {results[-1]['summary']}",
                file=sys.stderr,
                flush=True,
            )
    finally:
        ctx.close()
        with open(os.path.join(ctx.out, "results.json"), "w") as fh:
            json.dump({**meta, "checks": results}, fh, indent=2)
        with open(os.path.join(ctx.out, "report.md"), "w") as fh:
            fh.write(render_report(meta, results))
    print()
    print(render_table(results))
    if any(c["key"].startswith("bt_") for c in results):
        print("\nManual BitTorrent steps this script does not run:")
        for step in MANUAL_STEPS:
            print(f"  - {step}")
    failed = [c for c in results if c["status"] != "PASS"]
    print(f"\n{len(results) - len(failed)} of {len(results)} checks passed.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
