# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check the Tracker Blocker count in the address bar on a real page.

    ./mach python tools/m6e/trackers/tracker_count_check.py [URL]

Marionette turns off tracking protection, Remote Settings and Safe Browsing
list updates, so this restores them, forces a tracker list update, loads the
page, and prints the trust icon's state and count with protection on and
then off for the site. Screenshots of the address bar go to
artifacts/shots-P3.7.
"""

import base64, json, sys, time
from marionette_driver.marionette import Marionette
url = sys.argv[1] if len(sys.argv) > 1 else "https://www.bbc.com/news"
prefs = {
    "remote.prefs.recommended": False,
    "privacy.trackingprotection.enabled": True,
    "privacy.trackingprotection.pbmode.enabled": True,
    "browser.safebrowsing.update.enabled": True,
    "services.settings.server": "https://firefox.settings.services.mozilla.com/v1",
}
c = Marionette(bin="obj-m6e/dist/M6E.app/Contents/MacOS/m6e", startup_timeout=120, prefs=prefs)
c.start_session()
time.sleep(5)
c.set_context("chrome")
print("force:", c.execute_script("""
const lm = Cc["@mozilla.org/url-classifier/listmanager;1"].getService(Ci.nsIUrlListManager);
const tables = [Services.prefs.getCharPref("urlclassifier.trackingTable"), Services.prefs.getCharPref("urlclassifier.features.socialtracking.blacklistTables", ""), Services.prefs.getCharPref("urlclassifier.features.fingerprinting.blacklistTables", ""), Services.prefs.getCharPref("urlclassifier.features.cryptomining.blacklistTables", "")].join(",");
return lm.forceUpdates(tables);
"""))
for i in range(24):
    time.sleep(5)
    tables = c.execute_async_script("""
const done = arguments[0];
Cc["@mozilla.org/url-classifier/dbservice;1"].getService(Ci.nsIUrlClassifierDBService).getTables(t => done(t));
""")
    if "analytics-track-digest256" in tables:
        break
print("db tables:", ";".join(l.split(";")[0] for l in tables.splitlines()))
c.set_context("content")
c.set_context("content")
c.navigate(url)
time.sleep(8)
c.set_context("chrome")
STATE = """
const icon = document.getElementById("trust-icon-container");
const short = document.getElementById("trust-icon-tracker-count-shortform");
const long = document.getElementById("trust-icon-tracker-count-longform");
const vis = e => !!e && e.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true });
return { classes: [...icon.classList], short: short?.textContent, shortVisible: vis(short), longVisible: vis(long), iconVisible: vis(icon),
  log: Object.keys(JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog())).length,
  hasException: ContentBlockingAllowList.includes(gBrowser.selectedBrowser) };
"""
def state(label):
    time.sleep(8)
    r = c.execute_script(STATE + "")
    r["blocked"] = c.execute_script("""
const log = JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog());
return Object.values(log).filter(a => a.some(([s, b]) => b && (s & 0x1000 || s & 0x40 || s & 0x800 || s & 0x10000 || s & 0x20000000))).length;
""")
    r["protected"] = c.execute_script("const { M6EPrivacyShield } = ChromeUtils.importESModule('moz-src:///browser/components/protections/M6EPrivacyShield.sys.mjs'); return M6EPrivacyShield.isSiteProtected(gBrowser.selectedBrowser);")
    print(label + ":", json.dumps({k: r[k] for k in ("classes", "short", "longVisible", "blocked", "protected", "hasException")}))
    return r

TOGGLE = """
const icon = document.getElementById("trust-icon-container");
icon.click();
await new Promise(r => setTimeout(r, 1500));
document.getElementById("trustpanel-toggle").click();
"""
def toggle():
    c.execute_async_script("const done = arguments[0]; (async () => {" + TOGGLE + "})().then(done, e => done(String(e)));")

state("shield on")
open("artifacts/shots-P3.7/on.png", "wb").write(base64.b64decode(c.screenshot(element=c.find_element("id", "urlbar"))))
toggle()
state("shield on, site turned off")
open("artifacts/shots-P3.7/off.png", "wb").write(base64.b64decode(c.screenshot(element=c.find_element("id", "urlbar"))))
print("permissions:", c.execute_script("""
return [...Services.perms.all].filter(p => /trackingprotection|https-only|m6e-protect/.test(p.type)).map(p => p.type + ' ' + p.principal.origin + ' expire=' + p.expireType);
"""))
toggle()
state("shield on, site turned back on")
c.execute_script("""
const { M6EPrivacyShield } = ChromeUtils.importESModule('moz-src:///browser/components/protections/M6EPrivacyShield.sys.mjs');
Services.prefs.setBoolPref(M6EPrivacyShield.PREF_ENABLED, false);
M6EPrivacyShield.apply({ enabled: false, maximum: false });
gBrowser.reload();
""")
state("shield off")
toggle()
state("shield off, site turned on")
print("permissions:", c.execute_script("""
return [...Services.perms.all].filter(p => /trackingprotection|https-only|m6e-protect/.test(p.type)).map(p => p.type + ' ' + p.principal.origin + ' expire=' + p.expireType);
"""))
c.quit()
