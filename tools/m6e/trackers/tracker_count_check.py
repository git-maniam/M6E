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
print("on:", json.dumps(c.execute_script(STATE)))
print("log:", json.dumps(c.execute_script("""
const log = JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog());
return Object.fromEntries(Object.entries(log).slice(0, 8).map(([o, a]) => [o, a.map(x => '0x' + x[0].toString(16) + (x[1] ? ' blocked' : ''))]));
""")))
print("tables:", c.execute_script("return [Services.prefs.getCharPref('urlclassifier.trackingTable'), Services.prefs.getCharPref('browser.safebrowsing.provider.mozilla.updateURL'), Services.prefs.getBoolPref('privacy.trackingprotection.enabled'), Services.prefs.getStringPref('browser.contentblocking.category')]"))
open("artifacts/shots-P3.7/on.png", "wb").write(base64.b64decode(c.screenshot(element=c.find_element("id", "urlbar"))))
c.execute_script("ContentBlockingAllowList.add(gBrowser.selectedBrowser); gBrowser.reload();")
time.sleep(8)
print("off:", json.dumps(c.execute_script(STATE)))
open("artifacts/shots-P3.7/off.png", "wb").write(base64.b64decode(c.screenshot(element=c.find_element("id", "urlbar"))))
c.execute_script("ContentBlockingAllowList.remove(gBrowser.selectedBrowser);")
c.quit()
