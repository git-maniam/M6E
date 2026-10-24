# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Run the Phase 5 acceptance checks on a fresh profile of an M6E build.

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
    "VPN": {"browser.ipProtection.enabled": False},
    "Translations": {
        "browser.translations.enable": False,
        "browser.ai.control.translations": "blocked",
    },
    "AI": {
        "browser.ml.enable": False,
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
        "browser.urlbar.suggest.quicksuggest.sponsored": False,
    },
    "New Tab widgets and wallpapers": {
        "browser.newtabpage.activity-stream.newtabWallpapers.enabled": False,
        "browser.newtabpage.activity-stream.widgets.weather.enabled": False,
        "browser.newtabpage.activity-stream.widgets.stocks.enabled": False,
    },
    "Nimbus experiments": {"nimbus.rollouts.enabled": False},
    "Content relevancy": {"toolkit.contentRelevancy.enabled": False},
}

# Build-time switches (AppConstants) that must be off.
REMOVED_CONSTANTS = {
    "Crash reporter": "MOZ_CRASHREPORTER",
    "Updater": "MOZ_UPDATER",
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
        and not data["maximumOff"]["maximum"],
        f"on: rfp={data['maximumOn']['rfp']} off: rfp={data['maximumOff']['rfp']}",
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
    failed = [c for c in results if c["status"] != "PASS"]
    print(f"\n{len(results) - len(failed)} of {len(results)} checks passed.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
