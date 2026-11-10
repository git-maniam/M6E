/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: opening torrents and magnet links (P8.4, D33). Routes magnet: links,
// .torrent downloads and files opened from Finder to the Add dialog of
// gM6ETorrent. Nothing here adds or starts a torrent by itself.

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

const PREF_ENABLED = "m6e.torrent.enabled";
const TORRENT_MIME = "application/x-bittorrent";
const TEMP_DIR_NAME = "m6e-torrent-open";
const QUEUE_LIFETIME_MS = 60000;
const PAGE_COOLDOWN_MS = 2000;

const SOURCE_UNKNOWN = Object.freeze({ kind: "unknown", origin: null });
const SOURCE_FINDER = Object.freeze({ kind: "finder", origin: null });

function isEnabled() {
  return Services.prefs.getBoolPref(PREF_ENABLED, true);
}

function httpsOriginOfPrincipal(principal) {
  try {
    if (principal?.isContentPrincipal && principal.schemeIs("https")) {
      return principal.originNoSuffix;
    }
  } catch (e) {
    console.error(e);
  }
  return null;
}

function httpsOriginOfURI(uri) {
  return uri?.schemeIs("https") ? `https://${uri.hostPort}` : null;
}

function siteOf(host) {
  try {
    return Services.eTLD.getBaseDomainFromHost(host);
  } catch (e) {
    return host;
  }
}

function isTorrentName(name) {
  return /\.torrent$/i.test(name || "");
}

function isTorrentLauncher(launcher) {
  let type = "";
  try {
    type = launcher.MIMEInfo.MIMEType.toLowerCase();
  } catch (e) {}
  if (type == TORRENT_MIME) {
    return true;
  }
  if (isTorrentName(launcher.suggestedFileName)) {
    return true;
  }
  try {
    const url = launcher.source.QueryInterface(Ci.nsIURL);
    return isTorrentName(url.fileName);
  } catch (e) {
    return false;
  }
}

function isReady(win) {
  return (
    !!win &&
    !win.closed &&
    typeof win.gM6ETorrent?.addMagnet == "function" &&
    typeof win.gM6ETorrent?.addTorrentFile == "function"
  );
}

export const M6ETorrentHandler = {
  _initialized: false,
  _queue: [],
  _pending: new Map(),
  _listView: null,
  _busy: new WeakSet(),
  _lastPageOpen: new WeakMap(),
  _declined: new WeakSet(),
  _sweep: null,

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;
    Services.obs.addObserver(this, "browser-delayed-startup-finished");
    this._sweep = this._sweepTempDir().catch(console.error);
  },

  observe(subject, topic) {
    if (topic == "browser-delayed-startup-finished") {
      this._flush();
    }
  },

  /**
   * Called by nsContentDispatchChooser for a navigation to a magnet: URL.
   *
   * A page needs transient user activation; without it the navigation is
   * dropped silently, because falling through would show the external
   * application prompt in place of the Add dialog. A link or file passed from
   * another application counts as the user's own action.
   *
   * @param {nsIURI} uri
   * @param {nsIPrincipal} principal The principal that triggered the load.
   * @param {BrowsingContext} browsingContext
   * @param {boolean} triggeredExternally The load came from another app.
   * @param {boolean} hasUserActivation The triggering document had transient
   *   user activation when the navigation started.
   * @returns {boolean} true when the navigation has been dealt with.
   */
  handleMagnet(
    uri,
    principal,
    browsingContext,
    triggeredExternally,
    hasUserActivation
  ) {
    if (!isEnabled() || !uri.schemeIs("magnet")) {
      return false;
    }
    this.init();
    const win = browsingContext?.topChromeWindow;
    if (triggeredExternally) {
      this._open(win, w => w.gM6ETorrent.addMagnet(uri.spec, SOURCE_UNKNOWN), {
        focus: true,
      });
      return true;
    }
    const privileged =
      !!principal &&
      (principal.isSystemPrincipal ||
        principal.isAddonOrExpandedAddonPrincipal);
    if (!privileged && !hasUserActivation) {
      console.warn(
        "M6E: ignored a magnet: navigation without user activation."
      );
      return true;
    }
    if (!privileged && this._pageOpenBlocked(win)) {
      return true;
    }
    const source = {
      kind: principal?.isContentPrincipal ? "magnet-page" : "unknown",
      origin: httpsOriginOfPrincipal(principal),
    };
    this._open(win, w => w.gM6ETorrent.addMagnet(uri.spec, source), {
      pageInitiated: !privileged,
    });
    return true;
  },

  /**
   * Called by nsUnknownContentTypeDialog before it asks what to do with a
   * download. For a .torrent the browser's own download is saved to a
   * temporary file, so there is no second request, and the Add dialog opens
   * when it has finished.
   *
   * The launcher does not tell us whether the download followed a user
   * action, so a page-initiated .torrent download is accepted only in the
   * selected tab of the active window, and only once per cooldown.
   *
   * @param {nsIHelperAppLauncher} launcher
   * @param {nsIInterfaceRequestor} context
   * @returns {boolean} true when this module has taken over the download.
   */
  handleDownload(launcher, context) {
    if (
      !isEnabled() ||
      this._declined.has(launcher) ||
      !isTorrentLauncher(launcher)
    ) {
      return false;
    }
    this.init();
    const uri = launcher.source;

    if (uri.schemeIs("file")) {
      let file;
      try {
        file = uri.QueryInterface(Ci.nsIFileURL).file;
      } catch (e) {
        return false;
      }
      launcher.cancel(Cr.NS_BINDING_ABORTED);
      this._open(
        this._windowOf(launcher, context),
        w => w.gM6ETorrent.addTorrentFile(file.path, SOURCE_FINDER),
        { focus: true }
      );
      return true;
    }

    const browsingContext = this._browsingContextOf(launcher, context);
    const win = browsingContext?.topChromeWindow;
    if (!this._isForeground(browsingContext, win)) {
      this._declined.add(launcher);
      return false;
    }
    if (this._pageOpenBlocked(win)) {
      launcher.cancel(Cr.NS_BINDING_ABORTED);
      return true;
    }
    const source = {
      kind: "page",
      origin: this._downloadOrigin(uri, browsingContext),
    };
    this._saveToTemp(launcher, win, source).catch(e => {
      console.error(e);
      try {
        launcher.cancel(Cr.NS_BINDING_ABORTED);
      } catch (ex) {}
    });
    return true;
  },

  /**
   * Called by the command line handler for each URL it was given, which is how
   * Finder, `open -a` and other applications reach a running or starting M6E.
   *
   * @param {nsIURI} uri
   * @param {boolean} mayOpenWindow Open a browser window when there is none.
   * @returns {boolean} true when the URL is a magnet link or a .torrent file
   *   and has been dealt with.
   */
  handleCommandLineURI(uri, mayOpenWindow) {
    if (!isEnabled()) {
      return false;
    }
    let run;
    if (uri.schemeIs("magnet")) {
      run = w => w.gM6ETorrent.addMagnet(uri.spec, SOURCE_UNKNOWN);
    } else if (uri.schemeIs("file")) {
      let file;
      try {
        file = uri.QueryInterface(Ci.nsIFileURL).file;
      } catch (e) {
        return false;
      }
      if (!isTorrentName(file.leafName)) {
        return false;
      }
      const path = file.path;
      run = w => w.gM6ETorrent.addTorrentFile(path, SOURCE_FINDER);
    } else {
      return false;
    }
    this.init();
    this._open(null, run, { focus: true, mayOpenWindow });
    return true;
  },

  get _shellService() {
    return Cc["@mozilla.org/browser/shell-service;1"].getService(
      Ci.nsIMacShellService
    );
  },

  canMakeDefault() {
    return this._shellService.canSetAsDefaultHandler;
  },

  /**
   * @returns {{torrent: boolean, magnet: boolean}} Whether M6E is the default
   *   app for .torrent files and for magnet links.
   */
  getDefaultState() {
    const shell = this._shellService;
    return {
      torrent: shell.isDefaultHandlerFor(".torrent"),
      magnet: shell.isDefaultHandlerFor("magnet"),
    };
  },

  isDefault() {
    const { torrent, magnet } = this.getDefaultState();
    return torrent && magnet;
  },

  /**
   * Asks Launch Services to make M6E the default app for .torrent files and
   * magnet links. macOS may ask the user to confirm each change.
   *
   * @returns {Promise<{torrent: boolean, magnet: boolean}>} Which of the two
   *   changes were applied.
   */
  async makeDefault() {
    const shell = this._shellService;
    if (!shell.canSetAsDefaultHandler) {
      return { torrent: false, magnet: false };
    }
    const torrent = await shell.setAsDefaultHandlerFor(".torrent");
    const magnet = await shell.setAsDefaultHandlerFor("magnet");
    return { torrent, magnet };
  },

  _browsingContextOf(launcher, context) {
    try {
      const bc = BrowsingContext.get(launcher.browsingContextId);
      if (bc) {
        return bc;
      }
    } catch (e) {}
    try {
      return context.getInterface(Ci.nsIDocShell).browsingContext;
    } catch (e) {
      return null;
    }
  },

  _windowOf(launcher, context) {
    return this._browsingContextOf(launcher, context)?.topChromeWindow;
  },

  _isForeground(browsingContext, win) {
    if (!browsingContext || !win || win.closed) {
      return false;
    }
    const browser = browsingContext.top.embedderElement;
    return (
      !!browser &&
      win.gBrowser?.selectedBrowser === browser &&
      Services.focus.activeWindow === win
    );
  },

  _pageOpenBlocked(win) {
    if (!win) {
      return false;
    }
    if (this._busy.has(win)) {
      return true;
    }
    const last = this._lastPageOpen.get(win);
    if (last && Date.now() - last < PAGE_COOLDOWN_MS) {
      return true;
    }
    this._lastPageOpen.set(win, Date.now());
    return false;
  },

  // The page the link was on counts only when it is https and on the same
  // site as the file, so a URL typed or pasted while a trusted page is open
  // is not credited to that page.
  _downloadOrigin(uri, browsingContext) {
    const fileOrigin = httpsOriginOfURI(uri);
    const pagePrincipal =
      browsingContext?.currentWindowGlobal?.documentPrincipal;
    const pageOrigin = httpsOriginOfPrincipal(pagePrincipal);
    if (fileOrigin && pageOrigin) {
      try {
        const pageHost = pagePrincipal.host;
        if (siteOf(pageHost) == siteOf(uri.host)) {
          return pageOrigin;
        }
      } catch (e) {}
    }
    return fileOrigin;
  },

  async _tempDir() {
    const dir = PathUtils.join(
      Services.dirsvc.get("TmpD", Ci.nsIFile).path,
      TEMP_DIR_NAME
    );
    await IOUtils.makeDirectory(dir, { permissions: 0o700 });
    return dir;
  },

  async _sweepTempDir() {
    const dir = PathUtils.join(
      Services.dirsvc.get("TmpD", Ci.nsIFile).path,
      TEMP_DIR_NAME
    );
    if (await IOUtils.exists(dir)) {
      await IOUtils.remove(dir, { recursive: true, ignoreAbsent: true });
    }
  },

  async _saveToTemp(launcher, win, source) {
    await this._sweep;
    const dir = await this._tempDir();
    const name = Services.uuid.generateUUID().toString().slice(1, -1);
    const path = PathUtils.join(dir, `${name}.torrent`);
    const file = new lazy.FileUtils.File(path);

    await this._watchDownloads();
    this._pending.set(path, { win, source });

    try {
      launcher.MIMEInfo.preferredAction = Ci.nsIHandlerInfo.saveToDisk;
    } catch (e) {}
    Cc["@mozilla.org/uriloader/external-helper-app-service;1"]
      .getService(Ci.nsPIExternalAppLauncher)
      .deleteTemporaryFileOnExit(file);
    launcher.saveDestinationAvailable(file, true);
  },

  async _watchDownloads() {
    if (this._listView) {
      return;
    }
    const list = await lazy.Downloads.getList(lazy.Downloads.ALL);
    if (this._listView) {
      return;
    }
    this._listView = {
      onDownloadChanged: download => this._onDownloadChanged(list, download),
    };
    await list.addView(this._listView);
  },

  _onDownloadChanged(list, download) {
    const path = download.target?.path;
    const entry = this._pending.get(path);
    if (!entry) {
      return;
    }
    if (download.succeeded) {
      this._finishDownload(list, download, path, entry);
    } else if (download.error || download.canceled) {
      this._pending.delete(path);
      this._unwatchIfIdle(list);
    }
  },

  _finishDownload(list, download, path, entry) {
    this._pending.delete(path);
    this._unwatchIfIdle(list);
    list.remove(download).catch(console.error);
    this._open(
      entry.win,
      w => w.gM6ETorrent.addTorrentFile(path, entry.source),
      {
        pageInitiated: true,
      }
    );
  },

  _unwatchIfIdle(list) {
    if (!this._pending.size && this._listView) {
      const view = this._listView;
      this._listView = null;
      list.removeView(view).catch(console.error);
    }
  },

  // Runs `run` with a browser window whose gM6ETorrent is ready, preferring
  // `preferred`, else the most recent window. Waits for a window that is still
  // starting.
  _open(
    preferred,
    run,
    { focus = false, pageInitiated = false, mayOpenWindow = false } = {}
  ) {
    this._queue.push({
      preferred,
      run,
      focus,
      pageInitiated,
      expires: Date.now() + QUEUE_LIFETIME_MS,
    });
    this._flush();
    if (this._queue.length && mayOpenWindow && this._noWindows()) {
      lazy.BrowserWindowTracker.openWindow({});
    }
  },

  _noWindows() {
    return (
      !lazy.BrowserWindowTracker.getTopWindow({ allowPopups: true }) &&
      !lazy.BrowserWindowTracker.getPendingWindow()
    );
  },

  _pickWindow(preferred) {
    if (isReady(preferred)) {
      return preferred;
    }
    for (const options of [{ private: false }, {}]) {
      const win = lazy.BrowserWindowTracker.getTopWindow(options);
      if (isReady(win)) {
        return win;
      }
    }
    return null;
  },

  _flush() {
    const now = Date.now();
    const waiting = [];
    for (const item of this._queue) {
      if (item.expires < now) {
        console.error("M6E: no browser window took the torrent request.");
        continue;
      }
      const win = this._pickWindow(item.preferred);
      if (!win) {
        waiting.push(item);
        continue;
      }
      try {
        if (item.focus) {
          win.focus();
        }
        const result = item.run(win);
        if (item.pageInitiated && typeof result?.then == "function") {
          this._busy.add(win);
          const done = () => this._busy.delete(win);
          result.then(done, done);
        }
      } catch (e) {
        console.error(e);
      }
    }
    this._queue = waiting;
  },
};
