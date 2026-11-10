/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: the BitTorrent section of Settings (P8.5). Every control writes its
// m6e.torrent.* pref, the service reads it back, and the proxy password goes
// to the login manager. Nothing here starts the helper or touches the
// network, Launch Services or a real torrent: the default-app calls are
// stubbed and every value is synthetic (spec 8.9, D38).

const { M6ETorrentService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs"
);
const { M6ETorrentHandler } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentHandler.sys.mjs"
);
const { PREF_DEFAULTS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentProtocol.sys.mjs"
);
const { MockFilePicker } = SpecialPowers;

const PREFIX = "m6e.torrent.";
const LOGIN_ORIGIN = "chrome://m6e-torrent";
const LOGIN_REALM = "M6E BitTorrent proxy";
const SYNTHETIC_SECRET = "synthetic-secret-not-real";

const CONTROL_IDS = [
  "btPrivacyNote",
  "btPrivacyVisibility",
  "btEnabled",
  "btResumeAtStartup",
  "btConfirmQuit",
  "btDefaultApp",
  "btDownloadFolder",
  "btAskWhere",
  "btSequentialDefault",
  "btStartNow",
  "btSeedMode",
  "btSeedRatio",
  "btSeedHours",
  "btDownLimit",
  "btUpLimit",
  "btTurtle",
  "btTurtleDownLimit",
  "btTurtleUpLimit",
  "btMaxActiveDownloads",
  "btMaxActiveTorrents",
  "btEncryption",
  "btDht",
  "btPex",
  "btLpd",
  "btPortMapping",
  "btListenPort",
  "btRandomizePeerId",
  "btProxyType",
  "btProxyHost",
  "btProxyPort",
  "btProxyUsername",
  "btProxyPassword",
  "btProxyPeers",
  "btProxyFailClosed",
  "btBindEnabled",
  "btInterfaceName",
  "btIpFilterPath",
  "btIpFilterChoose",
];

function readPref(name) {
  switch (typeof PREF_DEFAULTS[name]) {
    case "boolean":
      return Services.prefs.getBoolPref(PREFIX + name);
    case "number":
      return Services.prefs.getIntPref(PREFIX + name);
    default:
      return Services.prefs.getStringPref(PREFIX + name);
  }
}

function writePref(name, value) {
  switch (typeof value) {
    case "boolean":
      Services.prefs.setBoolPref(PREFIX + name, value);
      break;
    case "number":
      Services.prefs.setIntPref(PREFIX + name, value);
      break;
    default:
      Services.prefs.setStringPref(PREFIX + name, value);
  }
}

function waitForPref(name, expected) {
  return TestUtils.waitForCondition(
    () => readPref(name) === expected,
    `${name} becomes ${expected}`
  );
}

async function openSection() {
  await openPreferencesViaOpenPreferencesAPI("bittorrent", {
    leaveOpen: true,
  });
  let doc = gBrowser.selectedBrowser.contentDocument;
  await TestUtils.waitForCondition(
    () => doc.getElementById("btPrivacyNote"),
    "Wait for the BitTorrent section"
  );
  return doc;
}

async function typeInto(el, value) {
  el.value = value;
  el.dispatchEvent(new Event("input", { bubbles: true }));
  el.dispatchEvent(new Event("change", { bubbles: true }));
}

function controlHidden(doc, id) {
  return doc.getElementById(`setting-control-${id}`).hidden;
}

async function savedLogins() {
  return Services.logins.searchLoginsAsync({
    origin: LOGIN_ORIGIN,
    httpRealm: LOGIN_REALM,
  });
}

add_setup(async function () {
  let snapshot = {};
  for (let name of Object.keys(PREF_DEFAULTS)) {
    snapshot[name] = readPref(name);
  }
  let original = {
    canMakeDefault: M6ETorrentHandler.canMakeDefault,
    isDefault: M6ETorrentHandler.isDefault,
    makeDefault: M6ETorrentHandler.makeDefault,
  };
  registerCleanupFunction(async () => {
    for (let [name, value] of Object.entries(snapshot)) {
      writePref(name, value);
    }
    Object.assign(M6ETorrentHandler, original);
    MockFilePicker.cleanup();
    await M6ETorrentService.setConfig({ "proxy.password": "" });
  });
  MockFilePicker.init();
});

add_task(async function test_defaults_are_private() {
  for (let name of Object.keys(PREF_DEFAULTS)) {
    Services.prefs.clearUserPref(PREFIX + name);
    is(
      readPref(name),
      PREF_DEFAULTS[name],
      `${name} has its shipped default in the test profile`
    );
  }
  ok(!readPref("portMapping"), "Port mapping is off");
  ok(!readPref("lpd"), "Local peer discovery is off");
  ok(readPref("dht") && readPref("pex"), "DHT and peer exchange are on");
  is(readPref("seedRatioPercent"), 100, "The seeding goal is ratio 1.0");
  is(readPref("proxy.type"), "none", "No proxy");
  is(readPref("bindInterface"), "", "No interface binding");
  ok(
    !Services.prefs
      .getChildList(PREFIX)
      .some(name => /password/i.test(name)),
    "No password pref exists"
  );
});

add_task(async function test_section_renders() {
  let doc = await openSection();
  ok(doc.getElementById("category-bittorrent"), "The sidebar has the section");
  for (let id of CONTROL_IDS) {
    ok(doc.getElementById(id), `${id} is in the section`);
  }
  ok(
    !controlHidden(doc, "btPrivacyNote"),
    "The privacy note is shown at the top"
  );
  ok(
    !controlHidden(doc, "btProxyFailClosed"),
    "The fail-closed explanation is shown"
  );
  ok(
    controlHidden(doc, "btInterfaceDown"),
    "No interface warning while the interface is up"
  );
  gBrowser.removeCurrentTab();
});

add_task(async function test_toggles_and_checkboxes() {
  let doc = await openSection();
  for (let [id, name] of [
    ["btLpd", "lpd"],
    ["btPortMapping", "portMapping"],
    ["btDht", "dht"],
    ["btPex", "pex"],
    ["btRandomizePeerId", "randomizePeerId"],
    ["btResumeAtStartup", "resumeAtStartup"],
    ["btConfirmQuit", "confirmQuit"],
    ["btSequentialDefault", "sequentialDefault"],
    ["btAskWhere", "askWhereToSave"],
    ["btStartNow", "startNow"],
    ["btTurtle", "turtle.enabled"],
    ["btEnabled", "enabled"],
  ]) {
    let before = readPref(name);
    doc.getElementById(id).click();
    await waitForPref(name, !before);
    doc.getElementById(id).click();
    await waitForPref(name, before);
  }
  gBrowser.removeCurrentTab();
});

add_task(async function test_numbers_and_limits() {
  let doc = await openSection();
  for (let [id, name, value] of [
    ["btDownLimit", "downKBps", 500],
    ["btUpLimit", "upKBps", 40],
    ["btTurtleDownLimit", "turtle.downKBps", 25],
    ["btTurtleUpLimit", "turtle.upKBps", 5],
    ["btMaxActiveDownloads", "maxActiveDownloads", 2],
    ["btMaxActiveTorrents", "maxActiveTorrents", 8],
    ["btListenPort", "listenPort", 50000],
  ]) {
    await typeInto(doc.getElementById(id), String(value));
    await waitForPref(name, value);
  }

  let down = doc.getElementById("btDownLimit");
  await typeInto(down, "-5");
  await TestUtils.waitForCondition(
    () => down.value == "500",
    "A negative limit is refused and the field shows the stored value"
  );
  await typeInto(down, "fast");
  await TestUtils.waitForCondition(
    () => down.value == "500",
    "A word is refused"
  );
  is(readPref("downKBps"), 500, "The stored limit did not change");

  let port = doc.getElementById("btListenPort");
  await typeInto(port, "70000");
  await TestUtils.waitForCondition(
    () => port.value == "50000",
    "A port above 65535 is refused"
  );

  let config = await M6ETorrentService.getConfig();
  is(config.downKBps, 500, "The service reads the download limit");
  is(config["turtle.upKBps"], 5, "The service reads the Turtle limit");
  is(config.maxActiveTorrents, 8, "The service reads the queue limit");
  is(config.listenPort, 50000, "The service reads the listening port");
  gBrowser.removeCurrentTab();
});

add_task(async function test_encryption() {
  let doc = await openSection();
  let select = doc.getElementById("btEncryption");
  is(select.value, "enabled", "Encryption starts enabled");
  for (let mode of ["forced", "disabled", "enabled"]) {
    select.value = mode;
    select.dispatchEvent(new Event("change", { bubbles: true }));
    await waitForPref("encryption", mode);
  }
  is((await M6ETorrentService.getConfig()).encryption, "enabled");
  gBrowser.removeCurrentTab();
});

add_task(async function test_seeding_goal() {
  let doc = await openSection();
  let mode = doc.getElementById("btSeedMode");
  let ratio = doc.getElementById("btSeedRatio");
  let hours = doc.getElementById("btSeedHours");
  is(mode.value, "ratio", "The default goal is a ratio");
  is(ratio.value, "1.0", "The default ratio is 1.0");

  await typeInto(ratio, "2.5");
  await waitForPref("seedRatioPercent", 250);
  await typeInto(ratio, "0");
  await TestUtils.waitForCondition(
    () => ratio.value == "2.5",
    "A ratio of zero is refused"
  );

  doc.getElementById("btSeedModeHours").click();
  await waitForPref("seedRatioPercent", 0);
  await waitForPref("seedHours", 24);
  await TestUtils.waitForCondition(() => mode.value == "hours");
  await typeInto(hours, "12");
  await waitForPref("seedHours", 12);

  doc.getElementById("btSeedModeNever").click();
  await waitForPref("seedHours", 0);
  is(readPref("seedRatioPercent"), 0, "Never stop clears both goals");

  doc.getElementById("btSeedModeRatio").click();
  await waitForPref("seedRatioPercent", 250);
  is(readPref("seedHours"), 0, "The ratio goal clears the hours");

  let config = await M6ETorrentService.getConfig();
  is(config.seedRatioPercent, 250, "The service reads the ratio");
  gBrowser.removeCurrentTab();
});

add_task(async function test_proxy_and_password() {
  Services.prefs.clearUserPref(PREFIX + "proxy.type");
  let doc = await openSection();
  let toggle = doc.getElementById("btProxyType");
  let host = doc.getElementById("btProxyHost");
  let password = doc.getElementById("btProxyPassword");
  ok(!toggle.pressed, "The proxy starts off");
  ok(host.disabled && password.disabled, "Its fields are disabled");

  toggle.click();
  await waitForPref("proxy.type", "socks5");
  await TestUtils.waitForCondition(
    () => !host.disabled && !password.disabled,
    "The proxy fields are enabled"
  );

  await typeInto(host, "  proxy.invalid  ");
  await waitForPref("proxy.host", "proxy.invalid");
  await typeInto(host, "bad host");
  await TestUtils.waitForCondition(
    () => host.value == "proxy.invalid",
    "A host with a space is refused"
  );
  await typeInto(doc.getElementById("btProxyPort"), "1081");
  await waitForPref("proxy.port", 1081);
  await typeInto(doc.getElementById("btProxyUsername"), "synthetic-user");
  await waitForPref("proxy.username", "synthetic-user");

  let scope = doc.getElementById("btProxyPeers");
  ok(readPref("proxy.peers"), "Trackers and peers go through the proxy");
  scope.querySelectorAll("moz-radio")[1].click();
  await waitForPref("proxy.peers", false);

  is((await savedLogins()).length, 0, "No password is saved yet");
  await typeInto(password, SYNTHETIC_SECRET);
  await TestUtils.waitForCondition(async () => {
    return (await savedLogins()).length == 1;
  }, "The password reaches the login manager");
  is(
    (await savedLogins())[0].password,
    SYNTHETIC_SECRET,
    "The login manager holds the password"
  );
  for (let name of Services.prefs.getChildList("")) {
    if (Services.prefs.getPrefType(name) == Services.prefs.PREF_STRING) {
      isnot(
        Services.prefs.getStringPref(name, ""),
        SYNTHETIC_SECRET,
        `${name} does not hold the password`
      );
    }
  }
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btProxyPasswordClear"),
    "The remove button appears"
  );
  is(password.value, "", "The password is never shown in the field");
  is(
    (await M6ETorrentService.getConfig())["proxy.hasPassword"],
    true,
    "The service knows a password is saved"
  );

  doc.getElementById("btProxyPasswordClear").click();
  await TestUtils.waitForCondition(async () => {
    return (await savedLogins()).length == 0;
  }, "The saved password is removed");

  toggle.click();
  await waitForPref("proxy.type", "none");
  gBrowser.removeCurrentTab();
});

add_task(async function test_interface_and_kill_switch() {
  Services.prefs.clearUserPref(PREFIX + "bindInterface");
  let doc = await openSection();
  let toggle = doc.getElementById("btBindEnabled");
  let name = doc.getElementById("btInterfaceName");
  ok(!toggle.pressed, "No interface is bound");
  ok(name.disabled, "The name field is disabled");
  ok(
    controlHidden(doc, "btInterfaceEmpty"),
    "No warning while binding is off"
  );

  toggle.click();
  await TestUtils.waitForCondition(() => !name.disabled);
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btInterfaceEmpty"),
    "Binding with no name warns that traffic is not restricted"
  );

  await typeInto(name, "utun9");
  await waitForPref("bindInterface", "utun9");
  await TestUtils.waitForCondition(
    () => controlHidden(doc, "btInterfaceEmpty"),
    "The warning goes once there is a name"
  );
  await typeInto(name, "en0; rm");
  await TestUtils.waitForCondition(
    () => name.value == "utun9",
    "A name with a space or a semicolon is refused"
  );
  is(readPref("bindInterface"), "utun9");

  Services.obs.notifyObservers(
    null,
    "m6e-torrent-network",
    JSON.stringify({ listening: null, interfaceDown: "utun9" })
  );
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btInterfaceDown"),
    "The kill switch state is shown when the interface is down"
  );
  Services.obs.notifyObservers(
    null,
    "m6e-torrent-network",
    JSON.stringify({ listening: null, interfaceDown: null })
  );
  await TestUtils.waitForCondition(
    () => controlHidden(doc, "btInterfaceDown"),
    "The state clears when the interface returns"
  );

  toggle.click();
  await waitForPref("bindInterface", "");
  gBrowser.removeCurrentTab();
});

add_task(async function test_folders_and_ip_filter() {
  let doc = await openSection();
  let dir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "m6e-torrent-settings"
  );
  let file = PathUtils.join(dir, "synthetic-filter.p2p");
  await IOUtils.writeUTF8(file, "synthetic\n");
  registerCleanupFunction(() => IOUtils.remove(dir, { recursive: true }));

  ok(
    controlHidden(doc, "btDownloadFolderReset"),
    "No reset button while the browser folder is used"
  );
  Services.prefs.setStringPref(PREFIX + "downloadDir", dir);
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btDownloadFolderReset"),
    "A chosen folder shows the reset button"
  );
  doc.getElementById("btDownloadFolderReset").click();
  await waitForPref("downloadDir", "");

  ok(
    controlHidden(doc, "btIpFilterClear"),
    "No filter, so nothing to remove"
  );
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  let picked = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  picked.initWithPath(file);
  MockFilePicker.setFiles([picked]);
  doc.getElementById("btIpFilterChoose").click();
  await waitForPref("ipFilterPath", file);
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btIpFilterClear"),
    "The remove button appears"
  );
  doc.getElementById("btIpFilterClear").click();
  await waitForPref("ipFilterPath", "");
  gBrowser.removeCurrentTab();
});

add_task(async function test_default_app_button() {
  let calls = 0;
  let isDefault = false;
  M6ETorrentHandler.canMakeDefault = () => true;
  M6ETorrentHandler.isDefault = () => isDefault;
  M6ETorrentHandler.makeDefault = async () => {
    calls++;
    isDefault = true;
    return { torrent: true, magnet: true };
  };

  let doc = await openSection();
  ok(!controlHidden(doc, "btDefaultApp"), "The button is offered");
  ok(controlHidden(doc, "btDefaultAppDone"), "No confirmation yet");
  is(calls, 0, "Opening Settings never changes the default app");

  doc.getElementById("btDefaultApp").click();
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btDefaultAppDone"),
    "The confirmation appears"
  );
  is(calls, 1, "One click, one request to the handler");
  ok(controlHidden(doc, "btDefaultApp"), "The button goes");
  gBrowser.removeCurrentTab();

  isDefault = false;
  M6ETorrentHandler.makeDefault = async () => {
    calls++;
    return { torrent: false, magnet: false };
  };
  doc = await openSection();
  doc.getElementById("btDefaultApp").click();
  await TestUtils.waitForCondition(
    () => !controlHidden(doc, "btDefaultAppFailed"),
    "A refusal by macOS is reported"
  );
  ok(!controlHidden(doc, "btDefaultApp"), "The button stays for a retry");
  gBrowser.removeCurrentTab();
});

add_task(async function test_hidden_button_keeps_section() {
  writePref("enabled", false);
  let doc = await openSection();
  ok(
    doc.getElementById("category-bittorrent") &&
      !doc.getElementById("category-bittorrent").hidden,
    "The section stays reachable with the BT button hidden"
  );
  let toggle = doc.getElementById("btEnabled");
  ok(!toggle.pressed, "The switch shows the button is hidden");
  toggle.click();
  await waitForPref("enabled", true);
  gBrowser.removeCurrentTab();
});
