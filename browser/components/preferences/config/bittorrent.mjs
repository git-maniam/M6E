/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the BitTorrent section of Settings (Phase 8, P8.5, D33 to D37). The
// settings are the m6e.torrent.* prefs of browser/app/profile/m6e.js: the
// service (M6ETorrentService.sys.mjs) watches them and pushes the changes to
// the helper, so this file only writes prefs. The one exception is the proxy
// password, which goes to the login manager through the service.

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  M6ETorrentHandler:
    "moz-src:///browser/components/torrent/M6ETorrentHandler.sys.mjs",
  M6ETorrentService:
    "moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs",
});

const PREFIX = "m6e.torrent.";
const TOPIC_NETWORK = "m6e-torrent-network";

Preferences.addAll(
  [
    ["enabled", "bool"],
    ["resumeAtStartup", "bool"],
    ["confirmQuit", "bool"],
    ["encryption", "string"],
    ["dht", "bool"],
    ["pex", "bool"],
    ["lpd", "bool"],
    ["portMapping", "bool"],
    ["listenPort", "int"],
    ["downKBps", "int"],
    ["upKBps", "int"],
    ["turtle.enabled", "bool"],
    ["turtle.downKBps", "int"],
    ["turtle.upKBps", "int"],
    ["maxActiveDownloads", "int"],
    ["maxActiveTorrents", "int"],
    ["seedRatioPercent", "int"],
    ["seedHours", "int"],
    ["proxy.type", "string"],
    ["proxy.host", "string"],
    ["proxy.port", "int"],
    ["proxy.username", "string"],
    ["proxy.peers", "bool"],
    ["bindInterface", "string"],
    ["ipFilterPath", "string"],
    ["randomizePeerId", "bool"],
    ["sequentialDefault", "bool"],
    ["downloadDir", "string"],
    ["askWhereToSave", "bool"],
    ["startNow", "bool"],
  ].map(([name, type]) => ({ id: PREFIX + name, type }))
);

function refresh(settingId) {
  Preferences.getSetting(settingId)?.emit("change");
}

function addBoolSetting(id, name, extra = {}) {
  Preferences.addSetting({ id, pref: PREFIX + name, ...extra });
}

function addIntSetting(id, name, min, max, extra = {}) {
  const pref = PREFIX + name;
  Preferences.addSetting({
    id,
    pref,
    ...extra,
    get: value => String(value),
    set(value, _deps, setting) {
      const text = String(value).trim();
      const n = Number(text);
      if (text == "" || !Number.isInteger(n) || n < min || n > max) {
        setting.emit("change");
        return Services.prefs.getIntPref(pref);
      }
      return n;
    },
  });
}

function addTextSetting(id, name, isValid, extra = {}) {
  const pref = PREFIX + name;
  Preferences.addSetting({
    id,
    pref,
    ...extra,
    set(value, _deps, setting) {
      const text = String(value ?? "").trim();
      if (!isValid(text)) {
        setting.emit("change");
        return Services.prefs.getStringPref(pref);
      }
      return text;
    },
  });
}

addBoolSetting("btEnabled", "enabled");
addBoolSetting("btResumeAtStartup", "resumeAtStartup");
addBoolSetting("btConfirmQuit", "confirmQuit");
addBoolSetting("btSequentialDefault", "sequentialDefault");
addBoolSetting("btAskWhere", "askWhereToSave");
addBoolSetting("btStartNow", "startNow");
addBoolSetting("btTurtle", "turtle.enabled");
addBoolSetting("btDht", "dht");
addBoolSetting("btPex", "pex");
addBoolSetting("btLpd", "lpd");
addBoolSetting("btPortMapping", "portMapping");
addBoolSetting("btRandomizePeerId", "randomizePeerId");

const needsProxy = {
  deps: ["btProxyType"],
  disabled: ({ btProxyType }) => !btProxyType.value,
};
addBoolSetting("btProxyPeers", "proxy.peers", needsProxy);

addIntSetting("btDownLimit", "downKBps", 0, 100_000_000);
addIntSetting("btUpLimit", "upKBps", 0, 100_000_000);
addIntSetting("btTurtleDownLimit", "turtle.downKBps", 0, 100_000_000);
addIntSetting("btTurtleUpLimit", "turtle.upKBps", 0, 100_000_000);
addIntSetting("btMaxActiveDownloads", "maxActiveDownloads", 0, 1000);
addIntSetting("btMaxActiveTorrents", "maxActiveTorrents", 0, 1000);
addIntSetting("btListenPort", "listenPort", 0, 65535);
addIntSetting("btProxyPort", "proxy.port", 1, 65535, needsProxy);

addTextSetting(
  "btProxyHost",
  "proxy.host",
  text => text.length <= 253 && !/[\s\0]/.test(text),
  needsProxy
);
addTextSetting(
  "btProxyUsername",
  "proxy.username",
  text => text.length <= 255 && !text.includes("\0"),
  needsProxy
);
addTextSetting(
  "btInterfaceName",
  "bindInterface",
  text => /^[A-Za-z0-9._:-]{0,32}$/.test(text),
  {
    deps: ["btBindEnabled"],
    disabled: ({ btBindEnabled }) => !btBindEnabled.value,
  }
);

Preferences.addSetting({
  id: "btPrivacyNote",
});
Preferences.addSetting({
  id: "btPrivacyVisibility",
});
Preferences.addSetting({
  id: "btProxyFailClosed",
});

Preferences.addSetting({
  id: "btEncryption",
  pref: PREFIX + "encryption",
  set(value, _deps, setting) {
    if (!["disabled", "enabled", "forced"].includes(value)) {
      setting.emit("change");
      return Services.prefs.getStringPref(PREFIX + "encryption");
    }
    return value;
  },
});

/* Default app */

let defaultAppStatus = null;

function readDefaultAppStatus() {
  try {
    if (!lazy.M6ETorrentHandler.canMakeDefault()) {
      return "unavailable";
    }
    return lazy.M6ETorrentHandler.isDefault() ? "default" : "not-default";
  } catch (e) {
    return "unavailable";
  }
}

Preferences.addSetting({
  id: "btDefaultAppState",
  get() {
    defaultAppStatus ??= readDefaultAppStatus();
    return defaultAppStatus;
  },
});

Preferences.addSetting({
  id: "btDefaultApp",
  deps: ["btDefaultAppState"],
  visible: ({ btDefaultAppState }) =>
    ["not-default", "failed"].includes(btDefaultAppState.value),
  async onUserClick() {
    try {
      await lazy.M6ETorrentHandler.makeDefault();
      defaultAppStatus = lazy.M6ETorrentHandler.isDefault()
        ? "default"
        : "failed";
    } catch (e) {
      console.error(e);
      defaultAppStatus = "failed";
    }
    refresh("btDefaultAppState");
  },
});

Preferences.addSetting({
  id: "btDefaultAppDone",
  deps: ["btDefaultAppState"],
  visible: ({ btDefaultAppState }) => btDefaultAppState.value == "default",
});

Preferences.addSetting({
  id: "btDefaultAppFailed",
  deps: ["btDefaultAppState"],
  visible: ({ btDefaultAppState }) => btDefaultAppState.value == "failed",
});

/* Default download folder */

let browserDownloadDir = "";

Preferences.addSetting({
  id: "btDownloadFolder",
  pref: PREFIX + "downloadDir",
  set(folder) {
    return folder?.path ?? Services.prefs.getStringPref(PREFIX + "downloadDir");
  },
  getControlConfig(config, _deps, setting) {
    if (setting.value || !browserDownloadDir) {
      return config;
    }
    return {
      ...config,
      controlAttrs: {
        ...config.controlAttrs,
        ".displayValue": browserDownloadDir,
      },
    };
  },
  setup(emitChange) {
    lazy.Downloads.getPreferredDownloadsDirectory().then(
      dir => {
        browserDownloadDir = dir ?? "";
        emitChange();
      },
      console.error
    );
  },
});

Preferences.addSetting({
  id: "btDownloadFolderReset",
  deps: ["btDownloadFolder"],
  visible: ({ btDownloadFolder }) => !!btDownloadFolder.value,
  onUserClick() {
    Services.prefs.clearUserPref(PREFIX + "downloadDir");
  },
});

/* Seeding goal: stop at a ratio, after hours, or never */

const lastSeed = { percent: 100, hours: 24 };

function seedValues() {
  return {
    percent: Services.prefs.getIntPref(PREFIX + "seedRatioPercent", 100),
    hours: Services.prefs.getIntPref(PREFIX + "seedHours", 0),
  };
}

Preferences.addSetting({
  id: "btSeedMode",
  deps: ["btSeedRatio", "btSeedHours"],
  get() {
    const { percent, hours } = seedValues();
    if (percent > 0) {
      return "ratio";
    }
    return hours > 0 ? "hours" : "never";
  },
  set(mode) {
    const { percent, hours } = seedValues();
    lastSeed.percent = percent > 0 ? percent : lastSeed.percent;
    lastSeed.hours = hours > 0 ? hours : lastSeed.hours;
    const goal = {
      ratio: { percent: lastSeed.percent, hours: 0 },
      hours: { percent: 0, hours: lastSeed.hours },
      never: { percent: 0, hours: 0 },
    }[mode];
    if (goal) {
      Services.prefs.setIntPref(PREFIX + "seedRatioPercent", goal.percent);
      Services.prefs.setIntPref(PREFIX + "seedHours", goal.hours);
    }
  },
});

Preferences.addSetting({
  id: "btSeedRatio",
  pref: PREFIX + "seedRatioPercent",
  disabled: () => seedValues().percent <= 0,
  get(percent) {
    if (percent <= 0) {
      return "";
    }
    const ratio = percent / 100;
    return Number.isInteger(ratio) ? ratio.toFixed(1) : String(ratio);
  },
  set(value, _deps, setting) {
    const ratio = Number(String(value).trim());
    const percent = Math.round(ratio * 100);
    if (!Number.isFinite(ratio) || percent < 1 || percent > 1_000_000) {
      setting.emit("change");
      return Services.prefs.getIntPref(PREFIX + "seedRatioPercent");
    }
    lastSeed.percent = percent;
    return percent;
  },
});

Preferences.addSetting({
  id: "btSeedHours",
  pref: PREFIX + "seedHours",
  disabled: () => seedValues().hours <= 0,
  get: hours => (hours > 0 ? String(hours) : ""),
  set(value, _deps, setting) {
    const text = String(value).trim();
    const hours = Number(text);
    if (text == "" || !Number.isInteger(hours) || hours < 1 || hours > 1e6) {
      setting.emit("change");
      return Services.prefs.getIntPref(PREFIX + "seedHours");
    }
    lastSeed.hours = hours;
    return hours;
  },
});

/* Proxy */

let passwordSaved = false;

async function readPasswordSaved() {
  try {
    passwordSaved = !!(await lazy.M6ETorrentService.getConfig())[
      "proxy.hasPassword"
    ];
  } catch (e) {
    console.error(e);
    passwordSaved = false;
  }
  refresh("btProxyPassword");
  refresh("btProxyPasswordClear");
}

async function storePassword(password) {
  try {
    await lazy.M6ETorrentService.setConfig({ "proxy.password": password });
  } catch (e) {
    console.error(e);
  }
  await readPasswordSaved();
}

Preferences.addSetting({
  id: "btProxyType",
  pref: PREFIX + "proxy.type",
  get: type => type == "socks5",
  set: on => (on ? "socks5" : "none"),
  onUserChange() {
    readPasswordSaved();
  },
});

Preferences.addSetting({
  id: "btProxyPassword",
  deps: ["btProxyType"],
  disabled: ({ btProxyType }) => !btProxyType.value,
  get: () => "",
  set(password) {
    storePassword(password);
    return "";
  },
  getControlConfig(config) {
    return passwordSaved
      ? { ...config, l10nId: "bittorrent-proxy-password-saved" }
      : config;
  },
  setup() {
    readPasswordSaved();
  },
});

Preferences.addSetting({
  id: "btProxyPasswordClear",
  deps: ["btProxyType"],
  visible: ({ btProxyType }) => btProxyType.value && passwordSaved,
  onUserClick() {
    storePassword("");
  },
});

/* Network interface and kill switch */

let bindWanted = null;
let interfaceDown = null;

Preferences.addSetting({
  id: "btBindEnabled",
  get() {
    return (
      bindWanted ?? !!Services.prefs.getStringPref(PREFIX + "bindInterface", "")
    );
  },
  set(on, _deps, setting) {
    bindWanted = !!on;
    if (!on) {
      Services.prefs.clearUserPref(PREFIX + "bindInterface");
    }
    setting.emit("change");
  },
});

Preferences.addSetting({
  id: "btInterfaceEmpty",
  deps: ["btBindEnabled", "btInterfaceName"],
  visible: ({ btBindEnabled, btInterfaceName }) =>
    btBindEnabled.value && !btInterfaceName.value,
});

Preferences.addSetting({
  id: "btInterfaceDown",
  visible: () => interfaceDown !== null,
  getControlConfig(config) {
    return { ...config, l10nArgs: { interface: interfaceDown ?? "" } };
  },
  setup(emitChange) {
    const observer = {
      observe(_subject, _topic, data) {
        try {
          interfaceDown = JSON.parse(data).interfaceDown ?? null;
        } catch (e) {
          interfaceDown = null;
        }
        emitChange();
      },
    };
    interfaceDown =
      lazy.M6ETorrentService.getNetworkState().interfaceDown ?? null;
    Services.obs.addObserver(observer, TOPIC_NETWORK);
    return () => Services.obs.removeObserver(observer, TOPIC_NETWORK);
  },
});

/* IP filter */

async function chooseIpFilter() {
  const title = await document.l10n.formatValue(
    "bittorrent-ipfilter-picker-title"
  );
  const picker = Cc["@mozilla.org/filepicker;1"].createInstance(
    Ci.nsIFilePicker
  );
  picker.init(window.browsingContext, title, Ci.nsIFilePicker.modeOpen);
  picker.appendFilters(Ci.nsIFilePicker.filterAll);
  const result = await new Promise(resolve => picker.open(resolve));
  if (result == Ci.nsIFilePicker.returnOK) {
    Services.prefs.setStringPref(PREFIX + "ipFilterPath", picker.file.path);
  }
}

Preferences.addSetting({
  id: "btIpFilterPath",
  pref: PREFIX + "ipFilterPath",
});

Preferences.addSetting({
  id: "btIpFilterChoose",
  onUserClick() {
    chooseIpFilter().catch(console.error);
  },
});

Preferences.addSetting({
  id: "btIpFilterClear",
  deps: ["btIpFilterPath"],
  visible: ({ btIpFilterPath }) => !!btIpFilterPath.value,
  onUserClick() {
    Services.prefs.clearUserPref(PREFIX + "ipFilterPath");
  },
});

SettingGroupManager.registerGroups({
  bittorrentPrivacy: {
    l10nId: "bittorrent-privacy-group",
    headingLevel: 2,
    items: [
      {
        id: "btPrivacyNote",
        control: "moz-message-bar",
        l10nId: "bittorrent-privacy-note",
        controlAttrs: { type: "info", role: "status" },
      },
      {
        id: "btPrivacyVisibility",
        control: "moz-message-bar",
        l10nId: "bittorrent-privacy-visibility",
        controlAttrs: { type: "warning", role: "status" },
      },
    ],
  },
  bittorrentGeneral: {
    l10nId: "bittorrent-general-group",
    headingLevel: 2,
    items: [
      {
        id: "btEnabled",
        l10nId: "bittorrent-enabled",
        control: "moz-toggle",
      },
      {
        id: "btResumeAtStartup",
        l10nId: "bittorrent-resume-at-startup",
      },
      {
        id: "btConfirmQuit",
        l10nId: "bittorrent-confirm-quit",
      },
      {
        id: "btDefaultAppDone",
        control: "moz-message-bar",
        l10nId: "bittorrent-default-app-done",
        controlAttrs: { type: "success", role: "status" },
      },
      {
        id: "btDefaultAppFailed",
        control: "moz-message-bar",
        l10nId: "bittorrent-default-app-failed",
        controlAttrs: { type: "warning", role: "status" },
      },
      {
        id: "btDefaultApp",
        control: "moz-button",
        l10nId: "bittorrent-default-app",
      },
    ],
  },
  bittorrentDownloads: {
    l10nId: "bittorrent-downloads-group",
    headingLevel: 2,
    items: [
      {
        id: "btDownloadFolder",
        l10nId: "bittorrent-download-folder",
        control: "moz-input-folder",
      },
      {
        id: "btDownloadFolderReset",
        control: "moz-button",
        l10nId: "bittorrent-download-folder-reset",
      },
      {
        id: "btAskWhere",
        l10nId: "bittorrent-ask-where",
      },
      {
        id: "btSequentialDefault",
        l10nId: "bittorrent-sequential-default",
      },
      {
        id: "btStartNow",
        l10nId: "bittorrent-start-now",
      },
    ],
  },
  bittorrentSeeding: {
    l10nId: "bittorrent-seeding-group",
    headingLevel: 2,
    items: [
      {
        id: "btSeedMode",
        control: "moz-radio-group",
        l10nId: "bittorrent-seed-mode",
        options: [
          {
            id: "btSeedModeRatio",
            value: "ratio",
            l10nId: "bittorrent-seed-ratio-option",
            items: [
              {
                id: "btSeedRatio",
                control: "moz-input-text",
                l10nId: "bittorrent-seed-ratio",
              },
            ],
          },
          {
            id: "btSeedModeHours",
            value: "hours",
            l10nId: "bittorrent-seed-hours-option",
            items: [
              {
                id: "btSeedHours",
                control: "moz-input-number",
                l10nId: "bittorrent-seed-hours",
              },
            ],
          },
          {
            id: "btSeedModeNever",
            value: "never",
            l10nId: "bittorrent-seed-never-option",
          },
        ],
      },
    ],
  },
  bittorrentSpeed: {
    l10nId: "bittorrent-speed-group",
    headingLevel: 2,
    items: [
      {
        id: "btDownLimit",
        control: "moz-input-number",
        l10nId: "bittorrent-down-limit",
      },
      {
        id: "btUpLimit",
        control: "moz-input-number",
        l10nId: "bittorrent-up-limit",
      },
      {
        id: "btTurtle",
        control: "moz-toggle",
        l10nId: "bittorrent-turtle",
        items: [
          {
            id: "btTurtleDownLimit",
            control: "moz-input-number",
            l10nId: "bittorrent-turtle-down-limit",
          },
          {
            id: "btTurtleUpLimit",
            control: "moz-input-number",
            l10nId: "bittorrent-turtle-up-limit",
          },
        ],
      },
      {
        id: "btMaxActiveDownloads",
        control: "moz-input-number",
        l10nId: "bittorrent-max-active-downloads",
      },
      {
        id: "btMaxActiveTorrents",
        control: "moz-input-number",
        l10nId: "bittorrent-max-active-torrents",
      },
    ],
  },
  bittorrentConnections: {
    l10nId: "bittorrent-connections-group",
    headingLevel: 2,
    items: [
      {
        id: "btEncryption",
        control: "moz-select",
        l10nId: "bittorrent-encryption",
        options: [
          {
            value: "disabled",
            l10nId: "bittorrent-encryption-disabled",
          },
          {
            value: "enabled",
            l10nId: "bittorrent-encryption-enabled",
          },
          {
            value: "forced",
            l10nId: "bittorrent-encryption-forced",
          },
        ],
      },
      {
        id: "btDht",
        l10nId: "bittorrent-dht",
      },
      {
        id: "btPex",
        l10nId: "bittorrent-pex",
      },
      {
        id: "btLpd",
        l10nId: "bittorrent-lpd",
      },
      {
        id: "btPortMapping",
        l10nId: "bittorrent-port-mapping",
      },
      {
        id: "btListenPort",
        control: "moz-input-number",
        l10nId: "bittorrent-listen-port",
      },
      {
        id: "btRandomizePeerId",
        l10nId: "bittorrent-randomize-peer-id",
      },
    ],
  },
  bittorrentProxy: {
    l10nId: "bittorrent-proxy-group",
    headingLevel: 2,
    items: [
      {
        id: "btProxyType",
        control: "moz-toggle",
        l10nId: "bittorrent-proxy-enabled",
        items: [
          {
            id: "btProxyHost",
            control: "moz-input-text",
            l10nId: "bittorrent-proxy-host",
          },
          {
            id: "btProxyPort",
            control: "moz-input-number",
            l10nId: "bittorrent-proxy-port",
          },
          {
            id: "btProxyUsername",
            control: "moz-input-text",
            l10nId: "bittorrent-proxy-username",
          },
          {
            id: "btProxyPassword",
            control: "moz-input-password",
            l10nId: "bittorrent-proxy-password",
          },
          {
            id: "btProxyPasswordClear",
            control: "moz-button",
            l10nId: "bittorrent-proxy-password-clear",
          },
          {
            id: "btProxyPeers",
            control: "moz-radio-group",
            l10nId: "bittorrent-proxy-scope",
            options: [
              {
                value: true,
                l10nId: "bittorrent-proxy-scope-all",
              },
              {
                value: false,
                l10nId: "bittorrent-proxy-scope-trackers",
              },
            ],
          },
        ],
      },
      {
        id: "btProxyFailClosed",
        control: "moz-message-bar",
        l10nId: "bittorrent-proxy-failclosed",
        controlAttrs: { type: "info", role: "status" },
      },
    ],
  },
  bittorrentInterface: {
    l10nId: "bittorrent-interface-group",
    headingLevel: 2,
    items: [
      {
        id: "btInterfaceDown",
        control: "moz-message-bar",
        l10nId: "bittorrent-interface-down",
        controlAttrs: { type: "warning", role: "alert" },
      },
      {
        id: "btBindEnabled",
        control: "moz-toggle",
        l10nId: "bittorrent-interface-enabled",
        items: [
          {
            id: "btInterfaceName",
            control: "moz-input-text",
            l10nId: "bittorrent-interface-name",
          },
        ],
      },
      {
        id: "btInterfaceEmpty",
        control: "moz-message-bar",
        l10nId: "bittorrent-interface-empty",
        controlAttrs: { type: "warning", role: "status" },
      },
    ],
  },
  bittorrentIpFilter: {
    l10nId: "bittorrent-ipfilter-group",
    headingLevel: 2,
    items: [
      {
        id: "btIpFilterPath",
        control: "moz-input-text",
        l10nId: "bittorrent-ipfilter-path",
        controlAttrs: { readonly: "" },
      },
      {
        id: "btIpFilterChoose",
        control: "moz-button",
        l10nId: "bittorrent-ipfilter-choose",
      },
      {
        id: "btIpFilterClear",
        control: "moz-button",
        l10nId: "bittorrent-ipfilter-clear",
      },
    ],
  },
});
