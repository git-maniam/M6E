/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: the parent-process side of the BitTorrent feature (P8.1). It starts
 * the m6e-torrent helper on demand, speaks the JSON-lines protocol of
 * tools/m6e/torrent/PROTOCOL.md with it, keeps the torrent list, and reports
 * changes through Services.obs. The pure logic is in M6ETorrentProtocol.sys.mjs.
 * Contracts: browser/components/torrent/CONTRACTS.md.
 */

import { setTimeout, clearTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  PROTOCOL_VERSION,
  MAX_TORRENT_BYTES,
  PREF_BRANCH,
  PREF_DEFAULTS,
  ALL_CONFIG_KEYS,
  LineFramer,
  RequestTable,
  TorrentError,
  buildConfig,
  buildLimits,
  candidateFilePaths,
  classifyPref,
  encodeRequest,
  helperArguments,
  helperIdleSeconds,
  httpsOrigin,
  isAbsolutePosixPath,
  isActiveState,
  isBusyState,
  limitsToPrefs,
  mergeFolders,
  parseFolderList,
  parseTrustedSites,
  partialToPrefs,
  pathIsInside,
  precheckTorrent,
  randomPeerIdPrefix,
  rememberFolder,
  sanitizeSource,
  unsupportedConfig,
  validateMagnet,
  validatePrefValue,
} from "moz-src:///browser/components/torrent/M6ETorrentProtocol.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  Subprocess: "resource://gre/modules/Subprocess.sys.mjs",
});
XPCOMUtils.defineLazyServiceGetters(lazy, {
  downloadPlatform: [
    "@mozilla.org/toolkit/download-platform;1",
    Ci.mozIDownloadPlatform,
  ],
});

const HELPER_NAME = "m6e-torrent";
const HELPER_PATH_PREF = PREF_BRANCH + "helperPath";
const SHUTDOWN_BLOCKER_NAME = "M6ETorrentService: stop the torrent helper";
const INBOX_NAME = "inbox";
const INBOX_MAX_AGE_MS = 5 * 60_000;
const MAX_FILTER_BYTES = 64 * 1024 * 1024;
const IDLE_SECONDS_ENV = "M6E_TORRENT_IDLE_SECONDS";
const QUARANTINE_ATTR = "com.apple.quarantine";
const MAX_ORIGIN_METADATA_FILES = 20;
const MAX_LOG_ENTRIES = 200;
const STDERR_TAIL_BYTES = 2048;
const PROXY_LOGIN_ORIGIN = "chrome://m6e-torrent";
const PROXY_LOGIN_REALM = "M6E BitTorrent proxy";

const TOPIC_CHANGED = "m6e-torrent-changed";
const TOPIC_METADATA = "m6e-torrent-metadata";
const TOPIC_ERROR = "m6e-torrent-error";
const TOPIC_COMPLETED = "m6e-torrent-completed";
const TOPIC_NETWORK = "m6e-torrent-network";
const TOPIC_TRACKERS = "m6e-torrent-trackers";

const DEFAULT_TIMINGS = {
  idleStopMs: 60_000,
  requestTimeoutMs: 30_000,
  restoreTimeoutMs: 120_000,
  startTimeoutMs: 15_000,
  shutdownGraceMs: 3_000,
  notifyIntervalMs: 1_000,
  prefDebounceMs: 100,
  stableUptimeMs: 60_000,
  pendingMaxAgeMs: 30 * 60_000,
  restartBackoffMs: [1_000, 2_000, 4_000, 8_000, 16_000],
};

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function deferred() {
  const d = {};
  d.promise = new Promise((resolve, reject) => {
    d.resolve = resolve;
    d.reject = reject;
  });
  return d;
}

class M6ETorrentServiceImpl {
  #timings = { ...DEFAULT_TIMINGS };
  #session = null;
  #startPromise = null;
  #stopPromise = null;
  #torrents = new Map();
  #pending = new Map();
  #sources = new Map();
  #trackers = new Map();
  #metadataWaiters = new Map();
  #reportedUnsupported = new Set();
  #quarantined = new Set();
  #log = [];
  #network = { listening: null, interfaceDown: null };
  #quitting = false;
  #initDone = false;
  #hasStartedThisSession = false;
  #resumeOnStart = false;
  #failures = 0;
  #restartTimer = null;
  #idleTimer = null;
  #notifyTimer = null;
  #lastNotify = 0;
  #prefTimer = null;
  #dirtyPrefs = new Set();
  #prefObserverAdded = false;
  #blockerAdded = false;
  #peerIdPrefix = null;
  #quitObserverAdded = false;
  #prefObserver;
  #shutdownBlocker;
  #quitObserver;

  constructor() {
    this.#prefObserver = {
      observe: (subject, topic, data) => this.#onPrefChanged(data),
    };
    this.#shutdownBlocker = () => this.#stop("quit");
    this.#quitObserver = {
      observe: () => {
        this.#quitting = true;
        this.#stop("quit").catch(console.error);
      },
    };
  }

  /* Preferences */

  #pref(name) {
    const fallback = PREF_DEFAULTS[name];
    const full = PREF_BRANCH + name;
    try {
      switch (typeof fallback) {
        case "boolean":
          return Services.prefs.getBoolPref(full, fallback);
        case "number":
          return Services.prefs.getIntPref(full, fallback);
        default:
          return Services.prefs.getStringPref(full, fallback);
      }
    } catch (e) {
      return fallback;
    }
  }

  #allPrefs() {
    const out = {};
    for (const name of Object.keys(PREF_DEFAULTS)) {
      out[name] = this.#pref(name);
    }
    return out;
  }

  get #enabled() {
    return this.#pref("enabled");
  }

  #setPref(name, value) {
    validatePrefValue(name, value);
    const full = PREF_BRANCH + name;
    switch (typeof value) {
      case "boolean":
        Services.prefs.setBoolPref(full, value);
        break;
      case "number":
        Services.prefs.setIntPref(full, value);
        break;
      default:
        Services.prefs.setStringPref(full, value);
    }
  }

  #onPrefChanged(name) {
    if (!name) {
      return;
    }
    this.#dirtyPrefs.add(name.slice(PREF_BRANCH.length));
    if (!this.#prefTimer) {
      this.#prefTimer = setTimeout(() => {
        this.#prefTimer = null;
        this.#flushPrefs(false).catch(e => this.#reportError(null, e));
      }, this.#timings.prefDebounceMs);
    }
  }

  async #flushPrefs(rethrow = true) {
    if (this.#prefTimer) {
      clearTimeout(this.#prefTimer);
      this.#prefTimer = null;
    }
    const dirty = this.#dirtyPrefs;
    this.#dirtyPrefs = new Set();
    const session = this.#session;
    if (!dirty.size || !session?.initialized || session.stopping) {
      return;
    }
    if (dirty.has("enabled") && !this.#enabled) {
      await this.#stop("disabled");
      return;
    }
    if (dirty.has("sandbox") && !this.#isBusy()) {
      await this.#stop("sandbox");
      return;
    }
    let limits = false;
    const keys = new Set();
    for (const name of dirty) {
      const c = classifyPref(name);
      if (c?.group == "limits") {
        limits = true;
      } else if (c?.group == "config") {
        keys.add(c.key);
      }
    }
    try {
      if (limits) {
        await this.#send(session, "setLimits", buildLimits(this.#allPrefs()));
      }
      if (keys.size) {
        await this.#applyConfig(session, [...keys]);
      }
    } catch (e) {
      this.#reportError(null, e);
      if (e.code == "config-failed") {
        await this.#stop("config-failed");
      }
      if (rethrow) {
        throw e;
      }
    }
  }

  async #proxyPassword() {
    try {
      const logins = await Services.logins.searchLoginsAsync({
        origin: PROXY_LOGIN_ORIGIN,
        httpRealm: PROXY_LOGIN_REALM,
      });
      return logins[0]?.password ?? "";
    } catch (e) {
      return "";
    }
  }

  async #storeProxyPassword(password) {
    if (typeof password != "string" || password.includes("\0")) {
      throw new TorrentError("bad-request", "invalid proxy password");
    }
    const existing = await Services.logins.searchLoginsAsync({
      origin: PROXY_LOGIN_ORIGIN,
      httpRealm: PROXY_LOGIN_REALM,
    });
    for (const login of existing) {
      await Services.logins.removeLoginAsync(login);
    }
    if (password == "") {
      return;
    }
    const login = Cc["@mozilla.org/login-manager/loginInfo;1"].createInstance(
      Ci.nsILoginInfo
    );
    login.init(
      PROXY_LOGIN_ORIGIN,
      null,
      PROXY_LOGIN_REALM,
      this.#pref("proxy.username") || "proxy",
      password,
      "",
      ""
    );
    await Services.logins.addLoginAsync(login);
  }

  /* Helper process */

  #helperPath() {
    if (Cu.isInAutomation) {
      const override = Services.prefs.getStringPref(HELPER_PATH_PREF, "");
      if (override) {
        return override;
      }
    }
    const exe = Services.dirsvc.get("XREExeF", Ci.nsIFile);
    return PathUtils.join(exe.parent.path, HELPER_NAME);
  }

  get #torrentsDir() {
    return PathUtils.join(PathUtils.profileDir, "torrents");
  }

  get #inboxDir() {
    return PathUtils.join(this.#torrentsDir, INBOX_NAME);
  }

  // Leftovers of a crash. Younger files may belong to a request in flight.
  async #cleanInbox() {
    let children;
    try {
      children = await IOUtils.getChildren(this.#inboxDir);
    } catch (e) {
      return;
    }
    for (const path of children) {
      try {
        const { lastModified } = await IOUtils.stat(path);
        if (Date.now() - lastModified > INBOX_MAX_AGE_MS) {
          await IOUtils.remove(path, { recursive: true, ignoreAbsent: true });
        }
      } catch (e) {
        // Already gone.
      }
    }
  }

  // The sandboxed helper can read only its inbox, so a file the user picked
  // is copied there (mode 0600) for the helper and deleted after its reply.
  async #stageInput(bytes, prefix) {
    await IOUtils.makeDirectory(this.#inboxDir, {
      createAncestors: true,
      ignoreExisting: true,
      permissions: 0o700,
    });
    const staged = await IOUtils.createUniqueFile(this.#inboxDir, prefix, 0o600);
    try {
      await IOUtils.write(staged, bytes);
    } catch (e) {
      await IOUtils.remove(staged, { ignoreAbsent: true });
      throw e;
    }
    return staged;
  }

  async #withStaged(bytes, prefix, callback) {
    const staged = await this.#stageInput(bytes, prefix);
    try {
      return await callback(staged);
    } finally {
      await IOUtils.remove(staged, { ignoreAbsent: true }).catch(() => {});
    }
  }

  async #allowedFolders() {
    const folders = [];
    try {
      folders.push(await lazy.Downloads.getPreferredDownloadsDirectory());
    } catch (e) {
      this.#addLog("warn", `no default download folder: ${e.message}`);
    }
    folders.push(...(parseFolderList(this.#pref("sandboxFolders")) ?? []));
    return mergeFolders(folders);
  }

  // With the sandbox on, the helper can write only to the folders it was
  // started with. A folder outside them is remembered and the helper stopped,
  // so that its next start (the caller adds the torrent again) includes it.
  async #ensureFolderAllowed(savePath) {
    if (!this.#pref("sandbox")) {
      return;
    }
    const allowed = await this.#allowedFolders();
    if (allowed.some(folder => pathIsInside(folder, savePath))) {
      return;
    }
    const remembered = parseFolderList(this.#pref("sandboxFolders")) ?? [];
    this.#setPref(
      "sandboxFolders",
      JSON.stringify(rememberFolder(remembered, savePath))
    );
    const session = this.#session;
    if (session && !session.closed) {
      this.#resumeOnStart = [...this.#torrents.values()].some(t =>
        isActiveState(t.state)
      );
      await this.#stop("folder");
    }
  }

  // The helper also exits by itself when idle. It is told to wait longer than
  // the service does, so that the service stops it first; an exit with status
  // 0 while nothing is active is still treated as normal (see #onExit).
  #helperIdleSeconds() {
    const override = Cu.isInAutomation
      ? Services.prefs.getIntPref(PREF_BRANCH + "helperIdleSeconds", 0)
      : 0;
    return override || helperIdleSeconds(this.#timings.idleStopMs);
  }

  async #ensureHelper() {
    if (this.#quitting) {
      throw new TorrentError("shutting-down", "M6E is quitting");
    }
    if (!this.#enabled) {
      throw new TorrentError("disabled", "BitTorrent is turned off");
    }
    if (this.#stopPromise) {
      await this.#stopPromise.catch(() => {});
    }
    if (this.#startPromise) {
      return this.#startPromise;
    }
    if (this.#session?.initialized && !this.#session.stopping) {
      return this.#session;
    }
    this.#startPromise = this.#start().finally(() => {
      this.#startPromise = null;
      this.#evaluateIdle();
    });
    return this.#startPromise;
  }

  async #start() {
    if (this.#restartTimer) {
      clearTimeout(this.#restartTimer);
      this.#restartTimer = null;
    }
    const restoreStart = this.#resumeOnStart
      ? true
      : this.#hasStartedThisSession
        ? false
        : this.#pref("resumeAtStartup");
    this.#resumeOnStart = false;
    this.#hasStartedThisSession = true;
    this.#reportedUnsupported.clear();

    const command = this.#helperPath();
    if (!(await IOUtils.exists(command))) {
      throw new TorrentError("helper-missing", `no helper at ${command}`);
    }
    const sandbox = this.#pref("sandbox");
    const folders = sandbox ? await this.#allowedFolders() : [];
    await this.#cleanInbox();
    let proc;
    try {
      proc = await lazy.Subprocess.call({
        command,
        arguments: helperArguments({
          profileDir: this.#torrentsDir,
          sandbox,
          folders,
          inboxDir: this.#inboxDir,
        }),
        environment: { [IDLE_SECONDS_ENV]: String(this.#helperIdleSeconds()) },
        environmentAppend: true,
        stderr: "pipe",
      });
    } catch (e) {
      throw new TorrentError(
        "helper-start-failed",
        `the helper did not start: ${e.message ?? e}`
      );
    }
    const session = {
      proc,
      requests: new RequestTable({ setTimeout, clearTimeout }),
      framer: new LineFramer(),
      ready: deferred(),
      exited: deferred(),
      initialized: false,
      stopping: false,
      closed: false,
      startedAt: Date.now(),
      stderrTail: "",
      engine: "",
      pid: proc.pid ?? null,
      sandbox,
      folders,
    };
    session.ready.promise.catch(() => {});
    this.#session = session;
    session.readerDone = this.#readLoop(session);
    this.#drainStderr(session);
    proc.wait().then(
      ({ exitCode }) => this.#onExit(session, exitCode),
      e => this.#onExit(session, -1, e)
    );

    try {
      const timeout = setTimeout(
        () =>
          session.ready.reject(
            new TorrentError("helper-start-failed", "the helper did not say ready")
          ),
        this.#timings.startTimeoutMs
      );
      try {
        await session.ready.promise;
      } finally {
        clearTimeout(timeout);
      }
      await this.#applyConfig(session, ALL_CONFIG_KEYS, true);
      const restored = await this.#send(
        session,
        "restore",
        { start: restoreStart },
        this.#timings.restoreTimeoutMs
      );
      this.#replaceTorrents(restored.torrents);
    } catch (e) {
      await this.#killSession(session);
      throw e;
    }
    session.initialized = true;
    this.#addSessionHooks();
    return session;
  }

  async #readLoop(session) {
    try {
      for (;;) {
        const chunk = await session.proc.stdout.readString();
        if (!chunk) {
          break;
        }
        this.#onData(session, chunk);
      }
    } catch (e) {
      if (e?.errorCode !== lazy.Subprocess.ERROR_END_OF_FILE) {
        this.#addLog("warn", `reading the helper failed: ${e?.message ?? e}`);
      }
    }
  }

  async #drainStderr(session) {
    try {
      for (;;) {
        const chunk = await session.proc.stderr.readString();
        if (!chunk) {
          break;
        }
        session.stderrTail = (session.stderrTail + chunk).slice(-STDERR_TAIL_BYTES);
      }
    } catch (e) {
      // The pipe closed with the process.
    }
  }

  #onData(session, chunk) {
    let items;
    try {
      items = session.framer.push(chunk);
    } catch (e) {
      this.#addLog("error", "the helper broke the protocol; stopping it");
      session.proc.kill(0).catch(() => {});
      return;
    }
    for (const item of items) {
      if (item.invalid !== undefined) {
        this.#addLog("warn", `unreadable helper output: ${item.invalid}`);
      } else if (typeof item.message.event == "string") {
        this.#onEvent(session, item.message);
      } else if (!session.requests.settle(item.message)) {
        this.#addLog("warn", "a reply matched no request");
      }
    }
  }

  async #onExit(session, exitCode, error) {
    await session.readerDone;
    session.closed = true;
    const message =
      `the helper exited (${exitCode})` +
      (session.stderrTail ? `: ${session.stderrTail.trim().slice(-300)}` : "");
    session.requests.rejectAll(() => new TorrentError("helper-exited", message));
    session.ready.reject(new TorrentError("helper-start-failed", message));
    session.exited.resolve();
    if (this.#session !== session) {
      return;
    }
    this.#session = null;
    this.#removeSessionHooks();
    this.#clearIdleTimer();
    this.#network = { listening: null, interfaceDown: null };
    if (session.stopping || this.#quitting) {
      return;
    }
    if (error) {
      this.#addLog("error", String(error));
    }
    const hadActive = [...this.#torrents.values()].some(t => isActiveState(t.state));
    if (exitCode === 0 && !hadActive && !error) {
      this.#addLog("info", "the helper exited because it was idle");
      this.#pending.clear();
      return;
    }
    this.#pending.clear();
    this.#emitError(null, "helper-exited", message);
    if (hadActive && session.initialized) {
      this.#scheduleRestart(session);
    }
  }

  #scheduleRestart(session) {
    if (Date.now() - session.startedAt >= this.#timings.stableUptimeMs) {
      this.#failures = 0;
    }
    const delays = this.#timings.restartBackoffMs;
    if (this.#failures >= delays.length) {
      this.#emitError(null, "helper-failed", "the helper keeps stopping");
      return;
    }
    const delay = delays[this.#failures++];
    this.#resumeOnStart = true;
    this.#restartTimer = setTimeout(() => {
      this.#restartTimer = null;
      this.#ensureHelper().catch(e => {
        this.#emitError(null, e.code ?? "helper-start-failed", e.message);
        this.#resumeOnStart = true;
        this.#scheduleRestart({ startedAt: Date.now() });
      });
    }, delay);
  }

  async #killSession(session) {
    session.stopping = true;
    try {
      await session.proc.kill(300);
    } catch (e) {
      // Already gone.
    }
    await session.exited.promise;
  }

  async #stop(reason) {
    if (this.#stopPromise) {
      return this.#stopPromise;
    }
    if (this.#restartTimer) {
      clearTimeout(this.#restartTimer);
      this.#restartTimer = null;
    }
    if (this.#startPromise) {
      await this.#startPromise.catch(() => {});
    }
    const session = this.#session;
    if (!session) {
      return undefined;
    }
    this.#stopPromise = (async () => {
      session.stopping = true;
      this.#clearIdleTimer();
      try {
        await this.#send(session, "shutdown", {}, this.#timings.shutdownGraceMs);
      } catch (e) {
        // The helper may exit before it replies.
      }
      session.proc.stdin.close().catch(() => {});
      await Promise.race([
        session.exited.promise,
        sleep(this.#timings.shutdownGraceMs),
      ]);
      if (!session.closed) {
        await this.#killSession(session);
      }
      this.#addLog("info", `helper stopped (${reason})`);
    })().finally(() => {
      this.#stopPromise = null;
    });
    return this.#stopPromise;
  }

  #send(session, op, args = {}, timeoutMs = this.#timings.requestTimeoutMs) {
    if (session.closed) {
      return Promise.reject(new TorrentError("helper-exited", "the helper is gone"));
    }
    const { id, promise } = session.requests.add(op, timeoutMs);
    session.proc.stdin
      .write(encodeRequest(id, op, args))
      .catch(e =>
        session.requests.fail(
          id,
          new TorrentError("helper-exited", `the helper is gone: ${e?.message ?? e}`)
        )
      );
    return promise;
  }

  async #call(op, args = {}, timeoutMs) {
    const session = await this.#ensureHelper();
    try {
      return await this.#send(session, op, args, timeoutMs);
    } finally {
      this.#evaluateIdle();
    }
  }

  async #applyConfig(session, keys, initial = false) {
    const prefs = this.#allPrefs();
    if (keys.includes("peerIdPrefix") && !this.#peerIdPrefix) {
      this.#peerIdPrefix = randomPeerIdPrefix();
    }
    const password = keys.includes("proxy") && prefs["proxy.type"] != "none"
      ? await this.#proxyPassword()
      : "";
    const config = buildConfig(prefs, keys, {
      password,
      peerIdPrefix: this.#peerIdPrefix,
    });
    let staged = null;
    try {
      if (typeof config.ipFilterPath == "string") {
        const bytes = await IOUtils.read(config.ipFilterPath, {
          maxBytes: MAX_FILTER_BYTES + 1,
        }).catch(() => {
          throw new TorrentError("config-failed", "the IP filter file cannot be read");
        });
        if (bytes.length > MAX_FILTER_BYTES) {
          throw new TorrentError("config-failed", "the IP filter file is too large");
        }
        staged = await this.#stageInput(bytes, PathUtils.filename(config.ipFilterPath));
        config.ipFilterPath = staged;
      }
      if (initial) {
        await this.#send(session, "setLimits", buildLimits(prefs));
      }
      const result = await this.#send(session, "setConfig", config);
      const { fatal, warn } = unsupportedConfig(config, result);
      for (const key of warn) {
        if (!this.#reportedUnsupported.has(key)) {
          this.#reportedUnsupported.add(key);
          this.#emitError(null, "unsupported-setting", `the helper cannot apply ${key}`);
        }
      }
      if (fatal.length) {
        throw new TorrentError(
          "config-failed",
          `the helper cannot apply ${fatal.join(" and ")}`
        );
      }
    } catch (e) {
      if (e.code == "config-failed" || e.code == "helper-exited") {
        throw e;
      }
      throw new TorrentError(
        "config-failed",
        `the helper rejected its settings: ${e.message}`
      );
    } finally {
      if (staged) {
        await IOUtils.remove(staged, { ignoreAbsent: true }).catch(() => {});
      }
    }
  }

  #addSessionHooks() {
    if (!this.#prefObserverAdded) {
      Services.prefs.addObserver(PREF_BRANCH, this.#prefObserver);
      this.#prefObserverAdded = true;
    }
    if (!this.#blockerAdded) {
      lazy.AsyncShutdown.profileBeforeChange.addBlocker(
        SHUTDOWN_BLOCKER_NAME,
        this.#shutdownBlocker
      );
      this.#blockerAdded = true;
    }
    if (!this.#quitObserverAdded) {
      Services.obs.addObserver(this.#quitObserver, "quit-application");
      this.#quitObserverAdded = true;
    }
  }

  #removeSessionHooks() {
    if (this.#prefObserverAdded) {
      Services.prefs.removeObserver(PREF_BRANCH, this.#prefObserver);
      this.#prefObserverAdded = false;
    }
    if (this.#blockerAdded) {
      lazy.AsyncShutdown.profileBeforeChange.removeBlocker(this.#shutdownBlocker);
      this.#blockerAdded = false;
    }
    if (this.#quitObserverAdded) {
      Services.obs.removeObserver(this.#quitObserver, "quit-application");
      this.#quitObserverAdded = false;
    }
  }

  /* Events from the helper */

  #onEvent(session, message) {
    switch (message.event) {
      case "ready":
        if (message.protocol !== PROTOCOL_VERSION) {
          session.ready.reject(
            new TorrentError(
              "protocol-mismatch",
              `the helper speaks protocol ${message.protocol}`
            )
          );
          break;
        }
        session.engine = String(message.engine ?? "");
        session.pid = message.pid ?? session.pid;
        session.ready.resolve();
        break;
      case "progress":
        this.#mergeTorrents(message.torrents);
        this.#evaluateIdle();
        break;
      case "metadata":
        this.#onMetadata(message);
        break;
      case "completed":
        if (typeof message.tid == "string") {
          this.#onCompleted(session, message.tid, message.warning);
        }
        break;
      case "trackers":
        this.#onTrackers(message);
        break;
      case "peers":
        this.#onPeers(message);
        break;
      case "listening":
        this.#network.listening = message.port ?? null;
        this.#notifyNetwork();
        break;
      case "closed":
        this.#network.listening = null;
        this.#notifyNetwork();
        break;
      case "interfaceDown":
        this.#network.interfaceDown = message.interface ?? "";
        this.#notifyNetwork();
        break;
      case "interfaceUp":
        this.#network.interfaceDown = null;
        this.#notifyNetwork();
        break;
      case "error":
        this.#emitError(message.tid, message.code ?? "engine-error", message.message ?? "");
        break;
      case "log":
        this.#addLog(message.level ?? "info", String(message.message ?? ""));
        break;
    }
  }

  #onMetadata({ tid, metadata }) {
    if (typeof tid != "string" || !metadata) {
      return;
    }
    const entry = this.#pending.get(tid);
    if (entry) {
      entry.metadata = metadata;
    }
    this.#metadataWaiters.get(tid)?.resolve(metadata);
    this.#notify(TOPIC_METADATA, {
      tid,
      metadata,
      source: this.#sources.get(tid) ?? sanitizeSource(null),
    });
  }

  #onTrackers({ tid, trackers }) {
    if (typeof tid != "string" || !Array.isArray(trackers)) {
      return;
    }
    this.#trackers.set(tid, trackers);
    this.#notify(TOPIC_TRACKERS, { tid, trackers });
  }

  #onPeers({ tid, seeds, peers }) {
    const status = this.#torrents.get(tid);
    if (!status || !Number.isInteger(seeds) || !Number.isInteger(peers)) {
      return;
    }
    this.#torrents.set(tid, { ...status, seeds, peers });
    this.#scheduleChanged();
  }

  async #onCompleted(session, tid, warning) {
    if (typeof warning == "string" && warning) {
      this.#addLog("warn", `${tid}: ${warning}`);
    }
    try {
      const status = await this.#send(session, "list").then(r => r.torrents);
      this.#replaceTorrents(status);
    } catch (e) {
      this.#addLog("warn", `listing after completion failed: ${e.message}`);
    }
    if (!this.#quarantined.has(tid)) {
      this.#quarantined.add(tid);
      try {
        await this.#quarantineCompleted(session, tid);
      } catch (e) {
        this.#emitError(tid, "quarantine-failed", e?.message ?? String(e));
      }
    }
    this.#notify(
      TOPIC_COMPLETED,
      typeof warning == "string" && warning ? { tid, warning } : { tid }
    );
    this.#evaluateIdle();
  }

  #addLog(level, message) {
    this.#log.push({ time: Date.now(), level, message });
    if (this.#log.length > MAX_LOG_ENTRIES) {
      this.#log.shift();
    }
  }

  #emitError(tid, code, message) {
    this.#addLog("error", `${tid ? tid + ": " : ""}${code}: ${message}`);
    this.#notify(TOPIC_ERROR, tid ? { tid, code, message } : { code, message });
  }

  #reportError(tid, error) {
    this.#emitError(tid, error?.code ?? "engine-error", error?.message ?? String(error));
  }

  #notify(topic, data) {
    Services.obs.notifyObservers(null, topic, JSON.stringify(data));
  }

  #notifyNetwork() {
    this.#notify(TOPIC_NETWORK, this.#network);
  }

  /* The torrent list */

  #replaceTorrents(list) {
    this.#torrents.clear();
    for (const status of list ?? []) {
      if (status?.tid) {
        this.#torrents.set(status.tid, status);
        this.#pending.delete(status.tid);
      }
    }
    this.#scheduleChanged();
  }

  #mergeTorrents(list) {
    for (const status of list ?? []) {
      if (status?.tid) {
        this.#torrents.set(status.tid, status);
      }
    }
    this.#scheduleChanged();
  }

  #scheduleChanged() {
    if (this.#notifyTimer) {
      return;
    }
    const wait = this.#lastNotify + this.#timings.notifyIntervalMs - Date.now();
    const fire = () => {
      this.#notifyTimer = null;
      this.#lastNotify = Date.now();
      this.#notify(TOPIC_CHANGED, { torrents: [...this.#torrents.values()] });
    };
    if (wait <= 0) {
      fire();
    } else {
      this.#notifyTimer = setTimeout(fire, wait);
    }
  }

  /* Idle stop. The helper holds the sleep assertion itself while a torrent is active. */

  #clearIdleTimer() {
    if (this.#idleTimer) {
      clearTimeout(this.#idleTimer);
      this.#idleTimer = null;
    }
  }

  #isBusy() {
    const session = this.#session;
    return (
      !!this.#startPromise ||
      (session?.requests.size ?? 0) > 0 ||
      [...this.#torrents.values()].some(t => isBusyState(t.state)) ||
      [...this.#pending.values()].some(
        p => Date.now() - p.addedAt < this.#timings.pendingMaxAgeMs
      )
    );
  }

  #evaluateIdle() {
    const session = this.#session;
    if (!session?.initialized || session.stopping) {
      return;
    }
    if (this.#isBusy()) {
      this.#clearIdleTimer();
    } else if (!this.#idleTimer) {
      this.#idleTimer = setTimeout(() => {
        this.#idleTimer = null;
        if (!this.#isBusy()) {
          this.#stop("idle").catch(console.error);
        }
      }, this.#timings.idleStopMs);
    }
  }

  /* Quarantine */

  async #quarantineCompleted(session, tid) {
    const detail = await this.#send(session, "detail", { tid });
    const { status, metadata } = detail;
    if (!status?.savePath || !metadata?.files) {
      return;
    }
    const selected = new Set(
      (detail.files ?? []).filter(f => f.priority > 0).map(f => f.index)
    );
    const origin = this.#sources.get(tid)?.origin;
    const originUri = origin ? lazy.NetUtil.newURI(origin) : null;
    let withOrigin = 0;
    for (const file of metadata.files) {
      if (file.pad || (selected.size && !selected.has(file.index))) {
        continue;
      }
      for (const path of candidateFilePaths(status.savePath, metadata, file)) {
        let stat;
        try {
          stat = await IOUtils.stat(path);
        } catch (e) {
          continue;
        }
        if (stat.type != "regular") {
          break;
        }
        const nsFile = new lazy.FileUtils.File(path);
        this.#quarantineFile(nsFile);
        if (originUri && withOrigin < MAX_ORIGIN_METADATA_FILES) {
          withOrigin++;
          await lazy.downloadPlatform
            .downloadDone(originUri, originUri, nsFile, "application/octet-stream", false)
            .catch(() => {});
        }
        break;
      }
    }
  }

  #quarantineFile(nsFile) {
    const mac = nsFile.QueryInterface(Ci.nsILocalFileMac);
    if (mac.hasXAttr(QUARANTINE_ATTR)) {
      return;
    }
    const seconds = Math.floor(Date.now() / 1000).toString(16);
    mac.setXAttr(
      QUARANTINE_ATTR,
      Array.from(new TextEncoder().encode(`0081;${seconds};M6E;`))
    );
  }

  /* Startup */

  /**
   * Called once at startup. Starts the helper only when the profile holds
   * saved torrents to resume; otherwise nothing runs.
   */
  async init() {
    if (this.#initDone || !this.#enabled || this.#quitting) {
      return;
    }
    this.#initDone = true;
    try {
      const saved = await IOUtils.getChildren(this.#torrentsDir).catch(() => []);
      if (!saved.some(path => path.endsWith(".resume"))) {
        return;
      }
      await this.#ensureHelper();
    } catch (e) {
      this.#reportError(null, e);
    }
  }

  /* Public API */

  async #readTorrentFile(path) {
    const stat = await IOUtils.stat(path).catch(() => {
      throw new TorrentError("io-error", "the torrent file cannot be read");
    });
    if (stat.type != "regular") {
      throw new TorrentError("invalid-torrent", "not a file");
    }
    if (stat.size > MAX_TORRENT_BYTES) {
      throw new TorrentError("too-large", "the torrent file is larger than 16 MiB");
    }
    const bytes = await IOUtils.read(path, { maxBytes: MAX_TORRENT_BYTES + 1 });
    precheckTorrent(bytes);
    return bytes;
  }

  async #addTorrentFile(path) {
    const bytes = await this.#readTorrentFile(path);
    await this.#ensureHelper();
    return this.#withStaged(bytes, "torrent.torrent", file =>
      this.#call("add", { file })
    );
  }

  async #addPending(tid, entry) {
    if (entry.input.file) {
      return this.#addTorrentFile(entry.input.file);
    }
    const waiter = deferred();
    waiter.promise.catch(() => {});
    this.#metadataWaiters.set(tid, waiter);
    const timer = setTimeout(
      () =>
        waiter.reject(
          new TorrentError("timeout", "the magnet link's metadata did not arrive")
        ),
      this.#timings.requestTimeoutMs
    );
    try {
      const result = await this.#call("add", { magnet: entry.input.magnet });
      if (!result.metadata) {
        await waiter.promise;
      }
      return result;
    } finally {
      clearTimeout(timer);
      this.#metadataWaiters.delete(tid);
    }
  }

  async addFile(path, source) {
    if (!isAbsolutePosixPath(path)) {
      throw new TorrentError("bad-request", "the torrent path must be absolute");
    }
    if (!this.#enabled) {
      throw new TorrentError("disabled", "BitTorrent is turned off");
    }
    const result = await this.#addTorrentFile(path);
    const clean = sanitizeSource(source);
    this.#sources.set(result.tid, clean);
    this.#pending.set(result.tid, {
      metadata: result.metadata ?? null,
      input: { file: path },
      addedAt: Date.now(),
    });
    this.#evaluateIdle();
    return { tid: result.tid, metadata: result.metadata };
  }

  async addMagnet(uri, source) {
    validateMagnet(uri);
    if (!this.#enabled) {
      throw new TorrentError("disabled", "BitTorrent is turned off");
    }
    const result = await this.#call("add", { magnet: uri });
    this.#sources.set(result.tid, sanitizeSource(source));
    this.#pending.set(result.tid, {
      metadata: result.metadata ?? null,
      input: { magnet: uri },
      addedAt: Date.now(),
    });
    this.#evaluateIdle();
    return { tid: result.tid };
  }

  async confirmAdd(tid, options = {}) {
    this.#checkTid(tid);
    const entry = this.#pending.get(tid);
    let files = options.files;
    if (!Array.isArray(files)) {
      const known = entry?.metadata?.files;
      if (!known) {
        throw new TorrentError("bad-request", "no file selection given");
      }
      files = known.filter(f => !f.pad).map(f => ({ index: f.index, priority: 2 }));
    }
    for (const f of files) {
      if (
        !Number.isInteger(f?.index) ||
        f.index < 0 ||
        !Number.isInteger(f.priority) ||
        f.priority < 0 ||
        f.priority > 3
      ) {
        throw new TorrentError("bad-request", "invalid file selection");
      }
    }
    if (!isAbsolutePosixPath(options.savePath)) {
      throw new TorrentError("bad-request", "the save folder must be absolute");
    }
    const args = {
      tid,
      files: files.map(({ index, priority }) => ({ index, priority })),
      savePath: options.savePath,
      sequential: options.sequential ?? this.#pref("sequentialDefault"),
      start: options.start !== false,
    };
    await this.#ensureFolderAllowed(options.savePath);
    let result;
    try {
      result = await this.#call("confirm", args);
    } catch (e) {
      if (e.code != "not-found" || !entry?.input) {
        throw e;
      }
      await this.#addPending(tid, entry);
      result = await this.#call("confirm", args);
    }
    this.#pending.delete(tid);
    if (result.status) {
      this.#torrents.set(tid, result.status);
      this.#scheduleChanged();
    }
    this.#evaluateIdle();
    return result;
  }

  async cancelAdd(tid) {
    this.#checkTid(tid);
    const known = this.#pending.delete(tid);
    this.#sources.delete(tid);
    if (this.#session?.initialized && !this.#session.stopping) {
      try {
        await this.#send(this.#session, "cancel", { tid });
      } catch (e) {
        if (e.code != "not-found") {
          throw e;
        }
      }
    }
    this.#evaluateIdle();
    return known;
  }

  async pause(tid) {
    this.#checkTid(tid);
    const result = await this.#call("pause", { tid });
    this.#cacheStatus(result.status);
    return result;
  }

  async resume(tid) {
    this.#checkTid(tid);
    const result = await this.#call("resume", { tid });
    this.#cacheStatus(result.status);
    return result;
  }

  async remove(tid, deleteFiles = false) {
    this.#checkTid(tid);
    await this.#call("remove", { tid, deleteFiles: !!deleteFiles });
    this.#torrents.delete(tid);
    this.#pending.delete(tid);
    this.#sources.delete(tid);
    this.#trackers.delete(tid);
    this.#quarantined.delete(tid);
    this.#scheduleChanged();
    this.#evaluateIdle();
  }

  async pauseAll() {
    await this.#call("pauseAll");
    await this.#syncList();
  }

  async resumeAll() {
    await this.#call("resumeAll");
    await this.#syncList();
  }

  async #syncList() {
    const result = await this.#call("list");
    this.#replaceTorrents(result.torrents);
    this.#evaluateIdle();
    return result.torrents;
  }

  #cacheStatus(status) {
    if (status?.tid) {
      this.#torrents.set(status.tid, status);
      this.#scheduleChanged();
    }
  }

  async list() {
    await this.init();
    if (this.#session?.initialized && !this.#session.stopping) {
      return { torrents: await this.#syncList() };
    }
    return { torrents: [...this.#torrents.values()] };
  }

  async detail(tid) {
    this.#checkTid(tid);
    return this.#call("detail", { tid });
  }

  get activeTorrentCount() {
    if (!this.#session?.initialized || this.#session.stopping) {
      return 0;
    }
    return [...this.#torrents.values()].filter(t => isActiveState(t.state)).length;
  }

  async hasActiveTorrents() {
    if (!this.#session?.initialized || this.#session.stopping) {
      return false;
    }
    try {
      await this.#syncList();
    } catch (e) {
      // Use what is known.
    }
    return [...this.#torrents.values()].some(t => isActiveState(t.state));
  }

  /**
   * Writes the limits to the prefs (the helper follows the prefs).
   *
   * @param {object} limits
   *   Any of downKBps, upKBps, turtle: { enabled, downKBps, upKBps },
   *   maxActiveDownloads and maxActiveTorrents.
   */
  async setLimits(limits) {
    for (const [name, value] of Object.entries(limitsToPrefs(limits))) {
      this.#setPref(name, value);
    }
    await this.#flushPrefs();
  }

  /**
   * All prefs of the feature by their name without "m6e.torrent.", for
   * example "turtle.enabled" and "proxy.type", plus proxy.hasPassword.
   */
  async getConfig() {
    const out = this.#allPrefs();
    out["proxy.hasPassword"] =
      out["proxy.type"] != "none" && (await this.#proxyPassword()) != "";
    return out;
  }

  /**
   * Writes prefs by their name without "m6e.torrent.". The grouped forms of
   * the helper's setConfig ("proxy": {...}, "seedGoal": {...}) are also
   * accepted. "proxy.password" goes to the login manager.
   */
  async setConfig(partial) {
    const prefs = partialToPrefs(partial);
    for (const [name, value] of Object.entries(prefs)) {
      if (name != "proxy.password") {
        validatePrefValue(name, value);
      }
    }
    for (const [name, value] of Object.entries(prefs)) {
      if (name == "proxy.password") {
        await this.#storeProxyPassword(value);
        this.#dirtyPrefs.add(name);
      } else {
        this.#setPref(name, value);
      }
    }
    await this.#flushPrefs();
  }

  getTrustedSites() {
    return parseTrustedSites(this.#pref("trustedSites")) ?? [];
  }

  async addTrustedSite(origin) {
    const clean = httpsOrigin(origin);
    if (!clean) {
      throw new TorrentError("bad-request", "only https sites can be trusted");
    }
    const sites = this.getTrustedSites();
    if (!sites.includes(clean)) {
      sites.push(clean);
      this.#setPref("trustedSites", JSON.stringify(sites));
    }
  }

  getNetworkState() {
    return { ...this.#network };
  }

  getLog() {
    return [...this.#log];
  }

  async shutdown() {
    await this.#stop("shutdown");
  }

  #checkTid(tid) {
    if (typeof tid != "string" || !/^[0-9a-f]{40}$/.test(tid)) {
      throw new TorrentError("bad-request", "invalid torrent id");
    }
  }

  /* Testing */

  get testing() {
    if (!Cu.isInAutomation) {
      throw new Error("testing hooks are only available in automation");
    }
    return {
      setTimings: timings => Object.assign(this.#timings, timings),
      reset: async () => {
        await this.#stop("test");
        this.#timings = { ...DEFAULT_TIMINGS };
        this.#torrents.clear();
        this.#pending.clear();
        this.#sources.clear();
        this.#trackers.clear();
        this.#metadataWaiters.clear();
        this.#reportedUnsupported.clear();
        this.#quarantined.clear();
        this.#log = [];
        this.#failures = 0;
        this.#resumeOnStart = false;
        this.#hasStartedThisSession = false;
        this.#initDone = false;
        this.#quitting = false;
        this.#peerIdPrefix = null;
        this.#clearIdleTimer();
        if (this.#restartTimer) {
          clearTimeout(this.#restartTimer);
          this.#restartTimer = null;
        }
        if (this.#notifyTimer) {
          clearTimeout(this.#notifyTimer);
          this.#notifyTimer = null;
        }
        this.#lastNotify = 0;
      },
      isHelperRunning: () => !!this.#session && !this.#session.closed,
      helperPid: () => this.#session?.pid ?? null,
      hasIdleTimer: () => !!this.#idleTimer,
      callHelper: (op, args) => this.#call(op, args),
      trackers: tid => this.#trackers.get(tid) ?? null,
      allowedFolders: () => this.#allowedFolders(),
      quarantineFile: nsFile => this.#quarantineFile(nsFile),
      cachedTorrents: () => [...this.#torrents.values()],
    };
  }
}

export const M6ETorrentService = new M6ETorrentServiceImpl();
