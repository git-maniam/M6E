/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the pure logic of the torrent service (P8.1): helper protocol framing
// and correlation, pref to helper configuration, and the safety precheck of a
// .torrent file. No Gecko globals, so it also runs under plain Node.

export const PROTOCOL_VERSION = 1;
export const MAX_LINE_BYTES = 1024 * 1024;
export const MAX_TORRENT_BYTES = 16 * 1024 * 1024;
export const MAX_BENCODE_DEPTH = 64;
export const MAX_MAGNET_LENGTH = 16 * 1024;
export const MAX_DOWNLOAD_FOLDERS = 4;
export const MIN_HELPER_IDLE_SECONDS = 90;
export const HELPER_IDLE_MARGIN_SECONDS = 30;

export const PREF_BRANCH = "m6e.torrent.";

// Keep in sync with the m6e.torrent.* block of browser/app/profile/m6e.js.
export const PREF_DEFAULTS = {
  enabled: true,
  resumeAtStartup: true,
  confirmQuit: true,
  encryption: "enabled",
  dht: true,
  pex: true,
  lpd: false,
  portMapping: false,
  listenPort: 0,
  downKBps: 0,
  upKBps: 0,
  "turtle.enabled": false,
  "turtle.downKBps": 50,
  "turtle.upKBps": 10,
  maxActiveDownloads: 3,
  maxActiveTorrents: 5,
  seedRatioPercent: 100,
  seedHours: 0,
  "proxy.type": "none",
  "proxy.host": "",
  "proxy.port": 1080,
  "proxy.username": "",
  "proxy.peers": true,
  bindInterface: "",
  ipFilterPath: "",
  randomizePeerId: false,
  sequentialDefault: false,
  trustedSites: "",
  sandbox: false,
  sandboxFolders: "",
};

const ACTIVE_STATES = new Set([
  "fetching-metadata",
  "checking",
  "queued",
  "downloading",
  "seeding",
]);

export function isActiveState(state) {
  return ACTIVE_STATES.has(state);
}

export function isBusyState(state) {
  return ACTIVE_STATES.has(state) || state == "pending-confirm";
}

export class TorrentError extends Error {
  constructor(code, message, extra = {}) {
    super(message || code);
    this.name = "TorrentError";
    this.code = code;
    Object.assign(this, extra);
  }
}

export function encodeRequest(id, op, args = {}) {
  return JSON.stringify({ id, op, args }) + "\n";
}

// Splits the helper's standard output into JSON objects. Complete lines that
// are not a JSON object come back as { invalid } and are the caller's to log.
export class LineFramer {
  #buffer = "";
  #maxLine;

  constructor(maxLine = MAX_LINE_BYTES) {
    this.#maxLine = maxLine;
  }

  push(text) {
    this.#buffer += text;
    const out = [];
    let start = 0;
    for (;;) {
      const end = this.#buffer.indexOf("\n", start);
      if (end < 0) {
        break;
      }
      const line = this.#buffer.slice(start, end);
      start = end + 1;
      if (line.length > this.#maxLine) {
        throw new TorrentError("protocol-error", "line too long");
      }
      if (line.trim() == "") {
        continue;
      }
      let message;
      try {
        message = JSON.parse(line);
      } catch (e) {
        out.push({ invalid: line.slice(0, 200) });
        continue;
      }
      if (message && typeof message == "object" && !Array.isArray(message)) {
        out.push({ message });
      } else {
        out.push({ invalid: line.slice(0, 200) });
      }
    }
    this.#buffer = this.#buffer.slice(start);
    if (this.#buffer.length > this.#maxLine) {
      throw new TorrentError("protocol-error", "line too long");
    }
    return out;
  }
}

// Matches replies to requests by id and times requests out.
export class RequestTable {
  #next = 1;
  #pending = new Map();
  #timers;

  constructor(timers = globalThis) {
    this.#timers = timers;
  }

  get size() {
    return this.#pending.size;
  }

  add(op, timeoutMs) {
    const id = this.#next++;
    const entry = { op };
    const promise = new Promise((resolve, reject) => {
      entry.resolve = resolve;
      entry.reject = reject;
    });
    if (timeoutMs > 0) {
      entry.timer = this.#timers.setTimeout(() => {
        this.#pending.delete(id);
        entry.reject(new TorrentError("timeout", `${op} timed out`));
      }, timeoutMs);
    }
    this.#pending.set(id, entry);
    return { id, promise };
  }

  #take(id) {
    const entry = this.#pending.get(id);
    if (!entry) {
      return null;
    }
    this.#pending.delete(id);
    if (entry.timer !== undefined) {
      this.#timers.clearTimeout(entry.timer);
    }
    return entry;
  }

  // Returns false for a reply that matches no request.
  settle(message) {
    const entry = this.#take(message.id);
    if (!entry) {
      return false;
    }
    if (message.ok === true) {
      entry.resolve(message.result ?? {});
    } else {
      const error = message.error ?? {};
      const extra = typeof message.tid == "string" ? { tid: message.tid } : {};
      if (typeof error.tid == "string") {
        extra.tid = error.tid;
      }
      entry.reject(
        new TorrentError(
          typeof error.code == "string" ? error.code : "engine-error",
          typeof error.message == "string" ? error.message : "",
          extra
        )
      );
    }
    return true;
  }

  fail(id, error) {
    const entry = this.#take(id);
    if (entry) {
      entry.reject(error);
    }
  }

  rejectAll(makeError) {
    for (const id of [...this.#pending.keys()]) {
      this.fail(id, makeError());
    }
  }
}

export function validatePrefValue(name, value) {
  const fallback = PREF_DEFAULTS[name];
  if (fallback === undefined) {
    throw new TorrentError("bad-request", `unknown setting ${name}`);
  }
  if (typeof value != typeof fallback) {
    throw new TorrentError("bad-request", `${name} has the wrong type`);
  }
  const integer = (min, max) => {
    if (!Number.isInteger(value) || value < min || value > max) {
      throw new TorrentError("bad-request", `${name} is out of range`);
    }
  };
  switch (name) {
    case "encryption":
      if (!["disabled", "enabled", "forced"].includes(value)) {
        throw new TorrentError("bad-request", "unknown encryption mode");
      }
      break;
    case "proxy.type":
      if (!["none", "socks5"].includes(value)) {
        throw new TorrentError("bad-request", "unknown proxy type");
      }
      break;
    case "listenPort":
    case "proxy.port":
      integer(0, 65535);
      break;
    case "downKBps":
    case "upKBps":
    case "turtle.downKBps":
    case "turtle.upKBps":
      integer(0, 100_000_000);
      break;
    case "maxActiveDownloads":
    case "maxActiveTorrents":
      integer(0, 1000);
      break;
    case "seedRatioPercent":
      integer(0, 1_000_000);
      break;
    case "seedHours":
      integer(0, 1_000_000);
      break;
    case "proxy.host":
      if (value.length > 253 || /[\s\0]/.test(value)) {
        throw new TorrentError("bad-request", "invalid proxy host");
      }
      break;
    case "proxy.username":
      if (value.length > 255 || value.includes("\0")) {
        throw new TorrentError("bad-request", "invalid proxy user name");
      }
      break;
    case "bindInterface":
      if (!/^[A-Za-z0-9._:-]{0,32}$/.test(value)) {
        throw new TorrentError("bad-request", "invalid interface name");
      }
      break;
    case "ipFilterPath":
      if (value != "" && !isAbsolutePosixPath(value)) {
        throw new TorrentError("bad-request", "invalid filter path");
      }
      break;
    case "trustedSites":
      if (value != "" && parseTrustedSites(value) === null) {
        throw new TorrentError("bad-request", "invalid trusted sites");
      }
      break;
    case "sandboxFolders":
      if (value != "" && parseFolderList(value) === null) {
        throw new TorrentError("bad-request", "invalid folder list");
      }
      break;
  }
  return value;
}

export function isAbsolutePosixPath(path) {
  return (
    typeof path == "string" &&
    path.startsWith("/") &&
    !path.includes("\0") &&
    path.length < 4096
  );
}

// Returns an array of strings, or null when the value is not one.
export function parseTrustedSites(text) {
  if (text === "" || text === undefined) {
    return [];
  }
  try {
    const list = JSON.parse(text);
    if (Array.isArray(list) && list.every(s => typeof s == "string")) {
      return list;
    }
  } catch (e) {
    // Fall through.
  }
  return null;
}

// Returns an array of absolute folder paths, or null when the value is not
// one.
export function parseFolderList(text) {
  if (text === "" || text === undefined) {
    return [];
  }
  try {
    const list = JSON.parse(text);
    if (Array.isArray(list) && list.every(isAbsolutePosixPath)) {
      return list;
    }
  } catch (e) {
    // Fall through.
  }
  return null;
}

function trimFolder(folder) {
  return folder.length > 1 ? folder.replace(/\/+$/, "") || "/" : folder;
}

// Unique, absolute folders in order, at most max of them.
export function mergeFolders(folders, max = MAX_DOWNLOAD_FOLDERS) {
  const out = [];
  for (const folder of folders) {
    if (!isAbsolutePosixPath(folder)) {
      continue;
    }
    const clean = trimFolder(folder);
    if (clean != "/" && !out.includes(clean)) {
      out.push(clean);
    }
  }
  return out.slice(0, max);
}

export function pathIsInside(folder, path) {
  const base = trimFolder(folder);
  return path == base || path.startsWith(base + "/");
}

// The most recently used folders first, the default folder always kept.
export function rememberFolder(remembered, folder, max = MAX_DOWNLOAD_FOLDERS - 1) {
  return mergeFolders([folder, ...remembered], max);
}

// How long the helper waits before it exits on its own: longer than the
// service's own idle stop, so that the service always stops it first.
export function helperIdleSeconds(idleStopMs) {
  return Math.max(
    MIN_HELPER_IDLE_SECONDS,
    Math.ceil(idleStopMs / 1000) + HELPER_IDLE_MARGIN_SECONDS
  );
}

// The helper's command line. The sandbox arguments are only passed when the
// m6e.torrent.sandbox pref is on (see the report of P8.1 for the helper side).
export function helperArguments({ profileDir, sandbox, folders, inboxDir }) {
  const args = ["--protocol", String(PROTOCOL_VERSION), "--profile-dir", profileDir];
  if (sandbox) {
    args.push("--sandbox", "--inbox-dir", inboxDir);
    for (const folder of mergeFolders(folders)) {
      args.push("--download-dir", folder);
    }
  }
  return args;
}

const PRIVACY_CONFIG_KEYS = new Set([
  "encryption",
  "dht",
  "pex",
  "lpd",
  "portMapping",
  "listenPort",
  "proxy",
  "bindInterface",
  "ipFilterPath",
]);

function configValueIsActive(key, value) {
  switch (key) {
    case "proxy":
      return !!value && value.type != "none";
    case "seedGoal":
      return value?.ratio != null || value?.hours != null;
    case "bindInterface":
    case "ipFilterPath":
    case "peerIdPrefix":
    case "userAgent":
      return value !== null && value !== undefined && value !== "";
    default:
      return true;
  }
}

// Sorts the keys of a setConfig request that the helper did not apply:
// fatal ones touch privacy or the network and the helper must not run
// without them; the rest are reported only.
export function unsupportedConfig(config, result) {
  const unsupported = new Set(result?.unsupported ?? []);
  if (Array.isArray(result?.applied)) {
    for (const key of Object.keys(config)) {
      if (!result.applied.includes(key)) {
        unsupported.add(key);
      }
    }
  }
  const out = { fatal: [], warn: [] };
  for (const key of unsupported) {
    if (!(key in config) || !configValueIsActive(key, config[key])) {
      continue;
    }
    (PRIVACY_CONFIG_KEYS.has(key) ? out.fatal : out.warn).push(key);
  }
  return out;
}

// Returns "https://host[:port]" or null for anything that is not an https
// origin.
export function httpsOrigin(value) {
  if (typeof value != "string" || value.length > 2048) {
    return null;
  }
  try {
    const url = new URL(value);
    return url.protocol == "https:" && url.hostname ? url.origin : null;
  } catch (e) {
    return null;
  }
}

export function sanitizeSource(source) {
  const kinds = ["page", "magnet-page", "finder", "unknown"];
  const kind = kinds.includes(source?.kind) ? source.kind : "unknown";
  return { kind, origin: httpsOrigin(source?.origin) };
}

// Pref names (without the branch) mapped to the part of the helper's
// configuration they feed.
export function classifyPref(name) {
  switch (name) {
    case "downKBps":
    case "upKBps":
    case "turtle.enabled":
    case "turtle.downKBps":
    case "turtle.upKBps":
    case "maxActiveDownloads":
    case "maxActiveTorrents":
      return { group: "limits" };
    case "encryption":
    case "dht":
    case "pex":
    case "lpd":
    case "portMapping":
    case "listenPort":
    case "bindInterface":
    case "ipFilterPath":
      return { group: "config", key: name };
    case "proxy.type":
    case "proxy.host":
    case "proxy.port":
    case "proxy.username":
    case "proxy.peers":
    case "proxy.password":
      return { group: "config", key: "proxy" };
    case "seedRatioPercent":
    case "seedHours":
      return { group: "config", key: "seedGoal" };
    case "randomizePeerId":
      return { group: "config", key: "peerIdPrefix" };
    default:
      return null;
  }
}

export const ALL_CONFIG_KEYS = [
  "encryption",
  "dht",
  "pex",
  "lpd",
  "portMapping",
  "listenPort",
  "proxy",
  "bindInterface",
  "ipFilterPath",
  "seedGoal",
  "peerIdPrefix",
  "userAgent",
];

export function buildLimits(p) {
  return {
    downKBps: p.downKBps,
    upKBps: p.upKBps,
    turtle: {
      enabled: p["turtle.enabled"],
      downKBps: p["turtle.downKBps"],
      upKBps: p["turtle.upKBps"],
    },
    maxActiveDownloads: p.maxActiveDownloads,
    maxActiveTorrents: p.maxActiveTorrents,
  };
}

// Builds the helper's setConfig arguments for the given keys (all by default)
// from a complete map of prefs. extras: { password, peerIdPrefix }.
export function buildConfig(p, keys = ALL_CONFIG_KEYS, extras = {}) {
  const config = {};
  for (const key of keys) {
    switch (key) {
      case "encryption":
      case "dht":
      case "pex":
      case "lpd":
      case "portMapping":
      case "listenPort":
        config[key] = p[key];
        break;
      case "proxy":
        config.proxy = {
          type: p["proxy.type"],
          host: p["proxy.host"],
          port: p["proxy.port"],
          username: p["proxy.username"],
          password: extras.password ?? "",
          peers: p["proxy.peers"],
        };
        break;
      case "bindInterface":
        config.bindInterface = p.bindInterface || null;
        break;
      case "ipFilterPath":
        config.ipFilterPath = p.ipFilterPath || null;
        break;
      case "seedGoal":
        config.seedGoal = {
          ratio: p.seedRatioPercent > 0 ? p.seedRatioPercent / 100 : null,
          hours: p.seedHours > 0 ? p.seedHours : null,
        };
        break;
      case "peerIdPrefix":
        config.peerIdPrefix = p.randomizePeerId
          ? (extras.peerIdPrefix ?? null)
          : null;
        break;
      case "userAgent":
        config.userAgent = null;
        break;
    }
  }
  return config;
}

// A random 8 character Peer ID prefix, "-" + 2 letters + 4 digits + "-".
export function randomPeerIdPrefix(random = Math.random) {
  const letter = () => String.fromCharCode(65 + Math.floor(random() * 26));
  const digit = () => String.fromCharCode(48 + Math.floor(random() * 10));
  return `-${letter()}${letter()}${digit()}${digit()}${digit()}${digit()}-`;
}

// Maps the user facing setConfig() argument to { prefName: value }. Accepts
// the short pref names and the helper's grouped "proxy" and "seedGoal" forms.
export function partialToPrefs(partial) {
  const out = {};
  if (!partial || typeof partial != "object") {
    throw new TorrentError("bad-request", "settings must be an object");
  }
  for (const [key, value] of Object.entries(partial)) {
    if (key == "proxy" && value && typeof value == "object") {
      for (const sub of ["type", "host", "port", "username", "peers"]) {
        if (sub in value) {
          out[`proxy.${sub}`] = value[sub];
        }
      }
      if ("password" in value) {
        out["proxy.password"] = value.password;
      }
    } else if (key == "seedGoal" && value && typeof value == "object") {
      if ("ratio" in value) {
        out.seedRatioPercent =
          value.ratio === null ? 0 : Math.round(value.ratio * 100);
      }
      if ("hours" in value) {
        out.seedHours = value.hours === null ? 0 : value.hours;
      }
    } else {
      out[key] = value;
    }
  }
  return out;
}

export function limitsToPrefs(limits) {
  const out = {};
  for (const [key, value] of Object.entries(limits ?? {})) {
    if (key == "turtle" && value && typeof value == "object") {
      for (const sub of ["enabled", "downKBps", "upKBps"]) {
        if (sub in value) {
          out[`turtle.${sub}`] = value[sub];
        }
      }
    } else {
      out[key] = value;
    }
  }
  for (const name of Object.keys(out)) {
    if (classifyPref(name)?.group != "limits") {
      throw new TorrentError("bad-request", `${name} is not a limit`);
    }
  }
  return out;
}

export function validateMagnet(uri) {
  if (typeof uri != "string" || uri.length > MAX_MAGNET_LENGTH) {
    throw new TorrentError("bad-request", "invalid magnet link");
  }
  // eslint-disable-next-line no-control-regex
  if (/[\x00-\x20\x7f]/.test(uri) || !/^magnet:\?/i.test(uri)) {
    throw new TorrentError("bad-request", "invalid magnet link");
  }
  const topics = uri
    .slice(uri.indexOf("?") + 1)
    .split("&")
    .filter(part => /^xt=/i.test(part))
    .map(part => part.slice(3));
  const valid = topics.some(
    t =>
      /^urn:btih:([0-9a-f]{40}|[a-z2-7]{32})$/i.test(t) ||
      /^urn:btmh:1220[0-9a-f]{64}$/i.test(t)
  );
  if (!valid) {
    throw new TorrentError("invalid-torrent", "the magnet link has no torrent");
  }
  return uri;
}

function invalid(message) {
  return new TorrentError("invalid-torrent", message);
}

function latin1(bytes) {
  let out = "";
  for (const b of bytes) {
    out += String.fromCharCode(b);
  }
  return out;
}

// A strict bencode reader: no leading zeros, no negative zero, no duplicate
// dictionary keys, no trailing data, and nesting limited to maxDepth. Strings
// are Uint8Arrays, dictionaries are Maps keyed by the Latin-1 decoding of the
// key bytes.
export function parseBencode(bytes, maxDepth = MAX_BENCODE_DEPTH) {
  let pos = 0;

  const readInteger = (terminator, what) => {
    const start = pos;
    if (bytes[pos] == 0x2d) {
      pos++;
    }
    const digits = pos;
    while (pos < bytes.length && bytes[pos] >= 0x30 && bytes[pos] <= 0x39) {
      pos++;
    }
    if (pos == digits || pos - digits > 16) {
      throw invalid(`bad ${what}`);
    }
    if (bytes[digits] == 0x30 && (pos - digits > 1 || digits != start)) {
      throw invalid(`bad ${what}`);
    }
    if (bytes[pos] != terminator) {
      throw invalid(`bad ${what}`);
    }
    const value = Number(latin1(bytes.subarray(start, pos)));
    pos++;
    if (!Number.isSafeInteger(value)) {
      throw invalid(`bad ${what}`);
    }
    return value;
  };

  const readString = () => {
    const length = readInteger(0x3a, "string length");
    if (length < 0 || pos + length > bytes.length) {
      throw invalid("string runs past the end");
    }
    const value = bytes.subarray(pos, pos + length);
    pos += length;
    return value;
  };

  const readValue = depth => {
    if (pos >= bytes.length) {
      throw invalid("unexpected end");
    }
    const c = bytes[pos];
    if (c == 0x69) {
      pos++;
      return readInteger(0x65, "integer");
    }
    if (c == 0x6c || c == 0x64) {
      if (depth >= maxDepth) {
        throw invalid("nested too deeply");
      }
      pos++;
      if (c == 0x6c) {
        const list = [];
        while (bytes[pos] != 0x65) {
          list.push(readValue(depth + 1));
        }
        pos++;
        return list;
      }
      const dict = new Map();
      while (bytes[pos] != 0x65) {
        if (pos >= bytes.length || bytes[pos] < 0x30 || bytes[pos] > 0x39) {
          throw invalid("bad dictionary key");
        }
        const key = latin1(readString());
        if (dict.has(key)) {
          throw invalid("duplicate dictionary key");
        }
        dict.set(key, readValue(depth + 1));
      }
      pos++;
      return dict;
    }
    if (c >= 0x30 && c <= 0x39) {
      return readString();
    }
    throw invalid("unexpected byte");
  };

  const root = readValue(0);
  if (pos != bytes.length) {
    throw invalid("data after the end");
  }
  return root;
}

function unsafe(what) {
  return new TorrentError("unsafe-path", `unsafe path in ${what}`);
}

function checkComponent(value, what) {
  if (!(value instanceof Uint8Array)) {
    throw invalid(`${what} is not a string`);
  }
  if (value.length == 0) {
    throw unsafe(what);
  }
  if (
    value.length <= 2 &&
    value[0] == 0x2e &&
    (value.length == 1 || value[1] == 0x2e)
  ) {
    throw unsafe(what);
  }
  for (const b of value) {
    if (b == 0x00 || b == 0x2f || b == 0x3a || b == 0x5c) {
      throw unsafe(what);
    }
  }
}

function checkPathList(list, what) {
  if (!Array.isArray(list) || !list.length) {
    throw invalid(`${what} is not a path`);
  }
  for (const part of list) {
    checkComponent(part, what);
  }
}

function checkFileTree(tree, what) {
  if (!(tree instanceof Map)) {
    throw invalid(`${what} is not a file tree`);
  }
  for (const [key, value] of tree) {
    if (key == "") {
      if (!(value instanceof Map)) {
        throw invalid(`${what} has a bad file entry`);
      }
      continue;
    }
    checkComponent(Uint8Array.from(key, ch => ch.charCodeAt(0)), what);
    checkFileTree(value, what);
  }
}

// The safety rules of PROTOCOL.md applied to the bytes of a .torrent file.
// Returns nothing; throws a TorrentError (too-large, invalid-torrent or
// unsafe-path).
export function precheckTorrent(bytes) {
  if (!(bytes instanceof Uint8Array)) {
    throw invalid("not a byte array");
  }
  if (bytes.length > MAX_TORRENT_BYTES) {
    throw new TorrentError("too-large", "the torrent file is larger than 16 MiB");
  }
  const root = parseBencode(bytes);
  const info = root instanceof Map ? root.get("info") : null;
  if (!(info instanceof Map)) {
    throw invalid("the torrent has no info dictionary");
  }
  checkComponent(info.get("name"), "the torrent name");
  if (info.has("name.utf-8")) {
    checkComponent(info.get("name.utf-8"), "the torrent name");
  }
  let hasContent = false;
  if (info.has("files")) {
    const files = info.get("files");
    if (!Array.isArray(files) || !files.length) {
      throw invalid("the file list is empty");
    }
    for (const file of files) {
      if (!(file instanceof Map) || !Number.isSafeInteger(file.get("length"))) {
        throw invalid("a file entry is malformed");
      }
      checkPathList(file.get("path"), "a file path");
      if (file.has("path.utf-8")) {
        checkPathList(file.get("path.utf-8"), "a file path");
      }
    }
    hasContent = true;
  }
  if (info.has("file tree")) {
    checkFileTree(info.get("file tree"), "the file tree");
    hasContent = true;
  }
  if (Number.isSafeInteger(info.get("length"))) {
    hasContent = true;
  }
  if (!hasContent) {
    throw invalid("the torrent has no files");
  }
}

// Candidate locations of a torrent's file below its save folder: Metadata
// paths may or may not start with the torrent's name.
export function candidateFilePaths(savePath, metadata, file) {
  const parts = file.path.split("/");
  const base = savePath.replace(/\/+$/, "");
  const out = [`${base}/${parts.join("/")}`];
  if (metadata.files.length > 1 && metadata.name) {
    out.push(`${base}/${metadata.name}/${parts.join("/")}`);
  }
  return out;
}
