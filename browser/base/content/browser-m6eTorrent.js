/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the BitTorrent button, panel and Add dialog (Phase 8, D33 to D37).

const m6eTorrentModules = {};
ChromeUtils.defineESModuleGetters(m6eTorrentModules, {
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  M6ETorrentRisk: "moz-src:///browser/components/torrent/M6ETorrentRisk.sys.mjs",
  M6ETorrentService:
    "moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs",
});

var gM6ETorrent = {
  ADD_DIALOG_URL: "chrome://browser/content/m6e-torrent-add.html",
  UNKNOWN_SOURCE: Object.freeze({ kind: "unknown", origin: null }),
  PENDING_STATES: ["pending-confirm", "fetching-metadata"],
  RUNNING_STATES: ["downloading", "seeding"],
  KNOWN_STATES: [
    "checking",
    "queued",
    "downloading",
    "seeding",
    "paused",
    "completed",
    "error",
  ],
  PREF_PREFIX: "m6e.torrent.",
  MAGNET_PATTERN: /^magnet:\?(?:.*&)?xt=urn:bt(?:ih|mh):[^&\s]+/i,

  torrents: [],
  _service: null,
  _risk: null,
  _initialized: false,
  _panelReady: false,
  _metadata: new Map(),
  _acknowledged: new Set(),
  _buttonKey: "",
  _rows: new Map(),
  _highlightTid: null,

  /**
   * The torrent service. Tests replace it with a mock by assigning a value
   * here, and restore it by assigning null.
   */
  get service() {
    return this._service ?? m6eTorrentModules.M6ETorrentService;
  },

  set service(value) {
    this._service = value;
  },

  /** The risk assessor. Tests may replace it the same way. */
  get risk() {
    return this._risk ?? m6eTorrentModules.M6ETorrentRisk;
  },

  set risk(value) {
    this._risk = value;
  },

  get button() {
    return document.getElementById("m6e-torrent-button");
  },

  get panel() {
    return document.getElementById("m6e-torrent-panel");
  },

  get panelShown() {
    return this._panelReady && this.panel.state != "closed";
  },

  get enabled() {
    return Services.prefs.getBoolPref(this.PREF_PREFIX + "enabled", true);
  },

  get syncL10n() {
    if (!this._syncL10n) {
      this._syncL10n = new Localization(["browser/m6eTorrent.ftl"], true);
    }
    return this._syncL10n;
  },

  /** Torrents that appear in the list; pending ones belong to the Add dialog. */
  get visibleTorrents() {
    return this.torrents.filter(t => !this.PENDING_STATES.includes(t.state));
  },

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;
    Services.obs.addObserver(this, "m6e-torrent-changed");
    Services.obs.addObserver(this, "m6e-torrent-metadata");
    Services.obs.addObserver(this, "quit-application-requested");
    Services.prefs.addObserver(this.PREF_PREFIX, this);
    window.addEventListener("unload", () => this.uninit(), { once: true });
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    this._initialized = false;
    Services.obs.removeObserver(this, "m6e-torrent-changed");
    Services.obs.removeObserver(this, "m6e-torrent-metadata");
    Services.obs.removeObserver(this, "quit-application-requested");
    Services.prefs.removeObserver(this.PREF_PREFIX, this);
  },

  observe(subject, topic, data) {
    switch (topic) {
      case "m6e-torrent-changed":
        this.setTorrents(JSON.parse(data).torrents);
        break;
      case "m6e-torrent-metadata": {
        let detail = JSON.parse(data);
        this._metadata.set(detail.tid, detail);
        if (this._metadata.size > 20) {
          this._metadata.delete(this._metadata.keys().next().value);
        }
        break;
      }
      case "quit-application-requested":
        this.onQuitRequested(subject);
        break;
      case "nsPref:changed":
        if (this.panelShown) {
          this.syncSettings();
        }
        break;
    }
  },

  takeMetadata(tid) {
    let detail = this._metadata.get(tid);
    this._metadata.delete(tid);
    return detail?.metadata ?? null;
  },

  setTorrents(torrents) {
    this.torrents = Array.isArray(torrents) ? torrents : [];
    this.updateButton();
    if (this.panelShown) {
      this.renderPanel();
    }
  },

  async refresh() {
    try {
      let result = await this.service.list();
      this.setTorrents(Array.isArray(result) ? result : result?.torrents);
    } catch (e) {
      console.error("M6E: could not list torrents", e);
    }
  },

  updateButton() {
    let button = this.button;
    if (!button) {
      return;
    }
    let visible = this.visibleTorrents;
    let hasError = visible.some(t => t.state == "error" || t.error);
    let count = visible.filter(t => this.RUNNING_STATES.includes(t.state))
      .length;
    let state = hasError ? "attention" : count ? "active" : "idle";
    let key = `${state}:${count}`;
    if (key == this._buttonKey) {
      return;
    }
    this._buttonKey = key;
    button.setAttribute("state", state);
    let badge = document.getElementById("m6e-torrent-badge");
    badge.setAttribute("value", state == "attention" ? "!" : String(count));
    switch (state) {
      case "attention":
        document.l10n.setAttributes(button, "m6e-torrent-button-attention");
        break;
      case "active":
        document.l10n.setAttributes(button, "m6e-torrent-button-active", {
          count,
        });
        break;
      default:
        document.l10n.setAttributes(button, "m6e-torrent-button");
    }
  },

  /**
   * Opens or closes the panel for a left click, Space or Enter on the button.
   *
   * @param {Event} event
   */
  handleButtonEvent(event) {
    event.stopPropagation();
    if (
      (event.type == "click" && event.button != 0) ||
      (event.type == "keypress" &&
        event.charCode != KeyEvent.DOM_VK_SPACE &&
        event.keyCode != KeyEvent.DOM_VK_RETURN)
    ) {
      return;
    }
    if (this.panel.state == "open" || this.panel.state == "showing") {
      this.panel.hidePopup();
      return;
    }
    this.openPanel(event);
  },

  openPanel(triggerEvent) {
    this.init();
    this.setupPanel();
    this.syncSettings();
    this.renderPanel();
    this.panel.openPopup(this.button, {
      position: "bottomleft topleft",
      triggerEvent,
    });
    return this.refresh();
  },

  setupPanel() {
    if (this._panelReady) {
      return;
    }
    this._panelReady = true;
    let panel = this.panel;

    let toggle = document.createElement("moz-toggle");
    toggle.id = "m6e-torrent-turtle";
    document.l10n.setAttributes(toggle, "m6e-torrent-turtle");
    toggle.addEventListener("toggle", () => {
      Services.prefs.setBoolPref(
        this.PREF_PREFIX + "turtle.enabled",
        toggle.pressed
      );
    });
    document.getElementById("m6e-torrent-settings").prepend(toggle);

    panel.addEventListener("click", event => this.onPanelClick(event));
    panel.addEventListener("change", event => this.onSettingChange(event));
    panel.addEventListener("keydown", event => {
      if (
        event.key == "Enter" &&
        event.target.id == "m6e-torrent-magnet-input"
      ) {
        event.preventDefault();
        this.submitMagnet();
      }
    });
    panel.addEventListener("popupshown", () => {
      this.button.setAttribute("open", "true");
    });
    panel.addEventListener("popuphidden", () => {
      this.button.removeAttribute("open");
      this._highlightTid = null;
      this.hideMagnetRow();
    });
  },

  renderPanel() {
    this.renderHeader();
    this.renderList();
  },

  renderHeader() {
    let visible = this.visibleTorrents;
    let down = 0;
    let up = 0;
    for (let t of visible) {
      down += t.downKBps || 0;
      up += t.upKBps || 0;
    }
    document.l10n.setAttributes(
      document.getElementById("m6e-torrent-speeds"),
      "m6e-torrent-speeds",
      { down: this.formatSpeed(down), up: this.formatSpeed(up) }
    );

    let pauseResume = document.getElementById("m6e-torrent-pause-resume-all");
    let anyRunning = visible.some(t =>
      ["checking", "queued", "downloading", "seeding"].includes(t.state)
    );
    let anyPaused = visible.some(t => t.state == "paused");
    pauseResume.disabled = !anyRunning && !anyPaused;
    pauseResume.dataset.action = anyRunning || !anyPaused ? "pause" : "resume";
    document.l10n.setAttributes(
      pauseResume,
      anyRunning || !anyPaused
        ? "m6e-torrent-pause-all"
        : "m6e-torrent-resume-all"
    );
  },

  renderList() {
    let visible = this.visibleTorrents;
    let list = document.getElementById("m6e-torrent-list");
    list.hidden = !visible.length;
    document.getElementById("m6e-torrent-empty").hidden = !!visible.length;
    document.getElementById("m6e-torrent-error-banner").hidden = !visible.some(
      t => t.state == "error" || t.error
    );

    let rows = this._rows;
    let seen = new Set();
    visible.forEach((torrent, index) => {
      seen.add(torrent.tid);
      let row = rows.get(torrent.tid);
      if (!row) {
        row = this.createRow(torrent.tid);
        rows.set(torrent.tid, row);
      }
      this.updateRow(row, torrent);
      if (list.children[index] != row) {
        list.insertBefore(row, list.children[index] ?? null);
      }
    });
    for (let [tid, row] of rows) {
      if (!seen.has(tid)) {
        row.remove();
        rows.delete(tid);
      }
    }

    rows.get(this._highlightTid)?.scrollIntoView({ block: "nearest" });
  },

  createRow(tid) {
    let make = (tag, className, parent) => {
      let element = document.createElement(tag);
      if (className) {
        element.className = className;
      }
      parent?.appendChild(element);
      return element;
    };
    let button = (className, action, parent) => {
      let element = make("button", className, parent);
      element.type = "button";
      element.dataset.action = action;
      return element;
    };

    let row = make("li", "m6e-torrent-row");
    row.dataset.tid = tid;

    let main = make("div", "row-main", row);
    button("row-name", "open", main);
    make("span", "row-state", main);

    let progress = make("progress", "row-progress", row);
    progress.max = 1;

    let details = make("div", "row-details", row);
    make("span", "row-down", details);
    make("span", "row-up", details);
    make("span", "row-eta", details);
    make("span", "row-error", details);

    let actions = make("div", "row-actions", row);
    button("row-toggle", "pause", actions);
    button("row-reveal", "reveal", actions);
    button("row-remove", "remove", actions);

    let choices = make("div", "row-remove-choices", row);
    choices.hidden = true;
    make("span", "row-remove-question", choices);
    button("row-remove-keep", "remove-keep", choices);
    button("row-remove-delete", "remove-delete", choices);
    button("row-remove-cancel", "remove-cancel", choices);
    for (let [className, id] of [
      ["row-remove-keep", "m6e-torrent-remove-keep"],
      ["row-remove-delete", "m6e-torrent-remove-delete"],
      ["row-remove-cancel", "m6e-torrent-remove-cancel"],
    ]) {
      document.l10n.setAttributes(choices.querySelector("." + className), id);
    }
    return row;
  },

  updateRow(row, torrent) {
    let name = torrent.name || torrent.tid;
    let state = torrent.state;
    let known = this.KNOWN_STATES.includes(state);
    row.dataset.state = state;
    row.toggleAttribute("highlight", row.dataset.tid == this._highlightTid);

    let nameButton = row.querySelector(".row-name");
    nameButton.textContent = name;
    let stateLabel = row.querySelector(".row-state");
    if (known) {
      document.l10n.setAttributes(stateLabel, `m6e-torrent-state-${state}`);
    } else {
      stateLabel.removeAttribute("data-l10n-id");
      stateLabel.textContent = state;
    }

    let progress = row.querySelector(".row-progress");
    let fraction = Math.min(1, Math.max(0, torrent.progress || 0));
    progress.value = fraction;
    document.l10n.setAttributes(progress, "m6e-torrent-progress", {
      percent: fraction * 100,
    });

    let showDown = state == "downloading" || torrent.downKBps > 0;
    let showUp = state == "seeding" || torrent.upKBps > 0;
    let down = row.querySelector(".row-down");
    let up = row.querySelector(".row-up");
    let eta = row.querySelector(".row-eta");
    let error = row.querySelector(".row-error");
    down.hidden = !showDown;
    up.hidden = !showUp;
    if (showDown) {
      document.l10n.setAttributes(down, "m6e-torrent-row-down", {
        speed: this.formatSpeed(torrent.downKBps || 0),
      });
    }
    if (showUp) {
      document.l10n.setAttributes(up, "m6e-torrent-row-up", {
        speed: this.formatSpeed(torrent.upKBps || 0),
      });
    }
    let hasEta = state == "downloading" && torrent.etaSeconds != null;
    eta.hidden = !hasEta;
    if (hasEta) {
      document.l10n.setAttributes(eta, "m6e-torrent-row-eta", {
        eta: this.formatDuration(torrent.etaSeconds),
      });
    }
    error.hidden = !torrent.error;
    if (torrent.error) {
      document.l10n.setAttributes(error, "m6e-torrent-row-error", {
        message: torrent.error.message || torrent.error.code || "",
      });
    }

    let args = { name };
    document.l10n.setAttributes(nameButton, "m6e-torrent-row-open", args);
    let toggle = row.querySelector(".row-toggle");
    let isRunning = ["checking", "queued", "downloading", "seeding"].includes(
      state
    );
    toggle.dataset.action = isRunning ? "pause" : "resume";
    document.l10n.setAttributes(
      toggle,
      isRunning ? "m6e-torrent-row-pause" : "m6e-torrent-row-resume",
      args
    );
    document.l10n.setAttributes(
      row.querySelector(".row-reveal"),
      "m6e-torrent-row-reveal",
      args
    );
    document.l10n.setAttributes(
      row.querySelector(".row-remove"),
      "m6e-torrent-row-remove",
      args
    );
    document.l10n.setAttributes(
      row.querySelector(".row-remove-question"),
      "m6e-torrent-remove-question",
      args
    );
  },

  syncSettings() {
    let prefs = Services.prefs;
    let prefix = this.PREF_PREFIX;
    let setValue = (id, value) => {
      let element = document.getElementById(id);
      if (document.activeElement != element) {
        element.value = value;
      }
    };

    let toggle = document.getElementById("m6e-torrent-turtle");
    if (toggle) {
      toggle.pressed = prefs.getBoolPref(prefix + "turtle.enabled", false);
    }
    setValue(
      "m6e-torrent-limit-down",
      prefs.getIntPref(prefix + "downKBps", 0)
    );
    setValue("m6e-torrent-limit-up", prefs.getIntPref(prefix + "upKBps", 0));
    let encryption = prefs.getStringPref(prefix + "encryption", "enabled");
    setValue(
      "m6e-torrent-encryption",
      ["disabled", "enabled", "forced"].includes(encryption)
        ? encryption
        : "enabled"
    );

    let ratioPercent = prefs.getIntPref(prefix + "seedRatioPercent", 100);
    let hours = prefs.getIntPref(prefix + "seedHours", 0);
    let goal = ratioPercent > 0 ? "ratio" : hours > 0 ? "hours" : "never";
    setValue("m6e-torrent-seed-goal", goal);
    document.getElementById("m6e-torrent-seed-value").hidden = goal == "never";
    if (goal != "never") {
      setValue(
        "m6e-torrent-seed-input",
        goal == "ratio" ? ratioPercent / 100 : hours
      );
      document.l10n.setAttributes(
        document.getElementById("m6e-torrent-seed-input"),
        goal == "ratio" ? "m6e-torrent-seed-ratio" : "m6e-torrent-seed-hours"
      );
      document.l10n.setAttributes(
        document.getElementById("m6e-torrent-seed-unit"),
        goal == "ratio"
          ? "m6e-torrent-seed-ratio-unit"
          : "m6e-torrent-seed-hours-unit"
      );
    }
  },

  onSettingChange(event) {
    let target = event.target;
    let prefs = Services.prefs;
    let prefix = this.PREF_PREFIX;
    let wholeNumber = value => Math.max(0, Math.floor(Number(value) || 0));
    switch (target.id) {
      case "m6e-torrent-limit-down":
        prefs.setIntPref(prefix + "downKBps", wholeNumber(target.value));
        break;
      case "m6e-torrent-limit-up":
        prefs.setIntPref(prefix + "upKBps", wholeNumber(target.value));
        break;
      case "m6e-torrent-encryption":
        prefs.setStringPref(prefix + "encryption", target.value);
        break;
      case "m6e-torrent-seed-goal":
      case "m6e-torrent-seed-input": {
        let goal = document.getElementById("m6e-torrent-seed-goal").value;
        let amount = Number(
          document.getElementById("m6e-torrent-seed-input").value
        );
        if (goal == "ratio" && amount > 0) {
          prefs.setIntPref(
            prefix + "seedRatioPercent",
            Math.max(1, Math.round(amount * 100))
          );
          prefs.setIntPref(prefix + "seedHours", 0);
        } else if (goal == "ratio") {
          prefs.setIntPref(prefix + "seedRatioPercent", 100);
          prefs.setIntPref(prefix + "seedHours", 0);
        } else if (goal == "hours") {
          prefs.setIntPref(prefix + "seedRatioPercent", 0);
          prefs.setIntPref(
            prefix + "seedHours",
            Math.max(1, Math.round(amount) || 24)
          );
        } else {
          prefs.setIntPref(prefix + "seedRatioPercent", 0);
          prefs.setIntPref(prefix + "seedHours", 0);
        }
        break;
      }
    }
    this.syncSettings();
  },

  onPanelClick(event) {
    let button = event.target.closest("button");
    if (!button) {
      return;
    }
    switch (button.id) {
      case "m6e-torrent-add-file":
        this.pickAndAddFile();
        return;
      case "m6e-torrent-add-magnet":
        this.toggleMagnetRow();
        return;
      case "m6e-torrent-magnet-submit":
        this.submitMagnet();
        return;
      case "m6e-torrent-magnet-cancel":
        this.hideMagnetRow();
        document.getElementById("m6e-torrent-add-magnet").focus();
        return;
      case "m6e-torrent-pause-resume-all":
        this.runAction(() =>
          button.dataset.action == "resume"
            ? this.service.resumeAll()
            : this.service.pauseAll()
        );
        return;
      case "m6e-torrent-open-settings":
        this.panel.hidePopup();
        this.openSettings();
        return;
    }

    let row = button.closest(".m6e-torrent-row");
    if (!row) {
      return;
    }
    let tid = row.dataset.tid;
    let torrent = this.torrents.find(t => t.tid == tid);
    let choices = row.querySelector(".row-remove-choices");
    switch (button.dataset.action) {
      case "open":
        this.panel.hidePopup();
        this.openDetails(tid);
        break;
      case "pause":
        this.runAction(() => this.service.pause(tid));
        break;
      case "resume":
        this.runAction(() => this.service.resume(tid));
        break;
      case "reveal":
        if (torrent) {
          this.reveal(torrent);
        }
        break;
      case "remove":
        choices.hidden = false;
        row.querySelector(".row-remove-keep").focus();
        break;
      case "remove-keep":
        this.runAction(() => this.service.remove(tid, false));
        break;
      case "remove-delete":
        this.runAction(() => this.service.remove(tid, true));
        break;
      case "remove-cancel":
        choices.hidden = true;
        row.querySelector(".row-remove").focus();
        break;
    }
  },

  async runAction(action) {
    try {
      await action();
    } catch (e) {
      console.error("M6E: torrent action failed", e);
    }
    await this.refresh();
  },

  toggleMagnetRow() {
    let row = document.getElementById("m6e-torrent-magnet-row");
    if (row.hidden) {
      row.hidden = false;
      document
        .getElementById("m6e-torrent-add-magnet")
        .setAttribute("aria-expanded", "true");
      document.getElementById("m6e-torrent-magnet-input").focus();
    } else {
      this.hideMagnetRow();
    }
  },

  hideMagnetRow() {
    let row = document.getElementById("m6e-torrent-magnet-row");
    if (!row) {
      return;
    }
    row.hidden = true;
    document.getElementById("m6e-torrent-magnet-input").value = "";
    document.getElementById("m6e-torrent-magnet-error").hidden = true;
    document
      .getElementById("m6e-torrent-add-magnet")
      .setAttribute("aria-expanded", "false");
  },

  async submitMagnet() {
    let input = document.getElementById("m6e-torrent-magnet-input");
    let uri = input.value.trim();
    let error = document.getElementById("m6e-torrent-magnet-error");
    if (!this.MAGNET_PATTERN.test(uri)) {
      error.hidden = false;
      input.setAttribute("aria-invalid", "true");
      input.focus();
      return;
    }
    input.removeAttribute("aria-invalid");
    this.panel.hidePopup();
    await this.addMagnet(uri, this.UNKNOWN_SOURCE);
  },

  async pickAndAddFile() {
    this.panel.hidePopup();
    let path = await this.pickTorrentFile();
    if (path) {
      await this.addTorrentFile(path, this.UNKNOWN_SOURCE);
    }
  },

  async pickTorrentFile() {
    let [title, filter] = await document.l10n.formatValues([
      { id: "m6e-torrent-pick-file-title" },
      { id: "m6e-torrent-pick-file-filter" },
    ]);
    let picker = Cc["@mozilla.org/filepicker;1"].createInstance(
      Ci.nsIFilePicker
    );
    picker.init(window.browsingContext, title, Ci.nsIFilePicker.modeOpen);
    picker.appendFilter(filter, "*.torrent");
    picker.appendFilters(Ci.nsIFilePicker.filterAll);
    let result = await new Promise(resolve => picker.open(resolve));
    return result == Ci.nsIFilePicker.returnOK ? picker.file.path : null;
  },

  async pickFolder(startPath) {
    let title = await document.l10n.formatValue(
      "m6e-torrent-pick-folder-title"
    );
    let picker = Cc["@mozilla.org/filepicker;1"].createInstance(
      Ci.nsIFilePicker
    );
    picker.init(window.browsingContext, title, Ci.nsIFilePicker.modeGetFolder);
    try {
      if (startPath) {
        picker.displayDirectory = new m6eTorrentModules.FileUtils.File(
          startPath
        );
      }
    } catch (e) {
      // The folder may not exist yet; the picker starts where it likes.
    }
    let result = await new Promise(resolve => picker.open(resolve));
    return result == Ci.nsIFilePicker.returnOK ? picker.file.path : null;
  },

  /**
   * Shows the torrent's folder or file in Finder.
   *
   * @param {object} torrent - a Status object
   */
  reveal(torrent) {
    if (!torrent.savePath) {
      return;
    }
    try {
      let { File } = m6eTorrentModules.FileUtils;
      let target = new File(PathUtils.join(torrent.savePath, torrent.name));
      (target.exists() ? target : new File(torrent.savePath)).reveal();
    } catch (e) {
      console.error("M6E: could not show the torrent in Finder", e);
    }
  },

  openSettings() {
    return openPreferences("bittorrent");
  },

  openDetails(tid) {
    return openTrustedLinkIn(
      "about:torrents#" + encodeURIComponent(tid),
      "tab"
    );
  },

  /**
   * Opens the Add dialog for a .torrent file.
   *
   * @param {string} path - absolute path of the file
   * @param {object} source - a Source object from CONTRACTS.md
   */
  async addTorrentFile(path, source = this.UNKNOWN_SOURCE) {
    if (!this.enabled) {
      return false;
    }
    this.init();
    let result;
    try {
      result = await this.service.addFile(path, source);
    } catch (e) {
      return this.handleAddError(e);
    }
    return this.openAddDialog({
      mode: "file",
      tid: result.tid,
      metadata: result.metadata,
      source,
    });
  },

  /**
   * Opens the Add dialog for a magnet link.
   *
   * @param {string} uri - the magnet link
   * @param {object} source - a Source object from CONTRACTS.md
   */
  async addMagnet(uri, source = this.UNKNOWN_SOURCE) {
    if (!this.enabled) {
      return false;
    }
    this.init();
    let result;
    try {
      result = await this.service.addMagnet(uri, source);
    } catch (e) {
      return this.handleAddError(e);
    }
    return this.openAddDialog({
      mode: "magnet",
      tid: result.tid,
      metadata: result.metadata ?? null,
      source,
    });
  },

  async handleAddError(error) {
    let code = error?.code ?? error?.error?.code;
    let tid = error?.tid ?? error?.error?.tid;
    if (code == "duplicate" && tid) {
      await this.showExisting(tid);
      return false;
    }
    await this.showError(error?.message ?? error?.error?.message ?? "");
    return false;
  },

  /**
   * Opens the panel and marks the row of a torrent that is already added.
   *
   * @param {string} tid
   */
  async showExisting(tid) {
    this._highlightTid = tid;
    await this.openPanel();
    this.renderList();
  },

  async showError(message) {
    try {
      let [title, text] = await document.l10n.formatValues([
        { id: "m6e-torrent-error-title" },
        { id: "m6e-torrent-error-add", args: { message } },
      ]);
      await Services.prompt.asyncAlert(
        gBrowser.selectedBrowser.browsingContext,
        Services.prompt.MODAL_TYPE_TAB,
        title,
        text
      );
    } catch (e) {
      console.error("M6E: could not show the torrent error", e);
    }
  },

  /**
   * The Add dialog is a tab-modal SubDialog (gBrowser.getTabDialogBox), not
   * a window-modal gDialogBox: a magnet link can wait on peers for minutes,
   * and that must not freeze the window or the other tabs. It belongs to
   * the tab the link was clicked in, so closing the tab drops the pending
   * torrent. Everything it needs is passed in, so tests can mock it.
   */
  async openAddDialog({ mode, tid, metadata, source }) {
    let defaultSavePath = "";
    try {
      defaultSavePath =
        (await m6eTorrentModules.Downloads.getPreferredDownloadsDirectory()) ??
        "";
    } catch (e) {
      console.error("M6E: no download folder", e);
    }
    let params = {
      mode,
      tid,
      metadata: metadata ?? this.takeMetadata(tid),
      source: source ?? this.UNKNOWN_SOURCE,
      defaultSavePath,
      added: false,
      service: this.service,
      assess: (meta, src, extra) => this.risk.assess(meta, src, extra),
      takeMetadata: id => this.takeMetadata(id),
      trustedSites: () => this.trustedSites,
      addTrustedSite: origin => this.addTrustedSite(origin),
      isAcknowledged: infoHash => this._acknowledged.has(infoHash),
      acknowledge: infoHash => this._acknowledged.add(infoHash),
      showExisting: id => this.showExisting(id),
      getFreeSpace: path => this.getFreeSpace(path),
      pickFolder: path => this.pickFolder(path),
      formatBytes: bytes => this.formatBytes(bytes),
    };
    try {
      let { dialog, closedPromise } = await gBrowser
        .getTabDialogBox(gBrowser.selectedBrowser)
        .open(this.ADD_DIALOG_URL, { features: "resizable=no" }, params);
      closedPromise.then(() => {
        if (!params.added) {
          this._metadata.delete(tid);
          this.cancelPending(tid);
        }
      });
      return { dialog, closedPromise, params };
    } catch (e) {
      console.error("M6E: could not open the Add dialog", e);
      this.cancelPending(tid);
      return false;
    }
  },

  cancelPending(tid) {
    return Promise.resolve()
      .then(() => this.service.cancelAdd(tid))
      .catch(e => console.error("M6E: could not cancel the torrent", e));
  },

  get trustedSites() {
    try {
      let value = JSON.parse(
        Services.prefs.getStringPref(this.PREF_PREFIX + "trustedSites", "") ||
          "[]"
      );
      return Array.isArray(value)
        ? value.filter(site => typeof site == "string")
        : [];
    } catch (e) {
      return [];
    }
  },

  addTrustedSite(origin) {
    let sites = this.trustedSites;
    if (origin && !sites.includes(origin)) {
      sites.push(origin);
      Services.prefs.setStringPref(
        this.PREF_PREFIX + "trustedSites",
        JSON.stringify(sites)
      );
    }
  },

  /** Free bytes on the volume that holds (or would hold) a folder. */
  getFreeSpace(path) {
    try {
      let file = new m6eTorrentModules.FileUtils.File(path);
      while (!file.exists() && file.parent) {
        file = file.parent;
      }
      return file.diskSpaceAvailable;
    } catch (e) {
      return null;
    }
  },

  hasActiveTorrentsSync() {
    let done = false;
    let active = false;
    Promise.resolve()
      .then(() => this.service.hasActiveTorrents())
      .then(
        value => {
          active = !!value;
        },
        e => console.error("M6E: could not ask about active torrents", e)
      )
      .finally(() => {
        done = true;
      });
    let timer = setTimeout(() => {
      done = true;
    }, 2000);
    Services.tm.spinEventLoopUntil(
      "gM6ETorrent:hasActiveTorrentsSync",
      () => done
    );
    clearTimeout(timer);
    return active;
  },

  /**
   * Like the active-downloads prompt (DownloadIntegration): a Quit or Don't
   * quit choice, shown by the most recent browser window only.
   *
   * @param {nsISupportsPRBool} cancel - set to true to stop the quit
   */
  onQuitRequested(cancel) {
    if (
      !this.enabled ||
      !Services.prefs.getBoolPref(this.PREF_PREFIX + "confirmQuit", true)
    ) {
      return;
    }
    if (cancel instanceof Ci.nsISupportsPRBool && cancel.data) {
      return;
    }
    if (
      window.closed ||
      window != Services.wm.getMostRecentWindow("navigator:browser")
    ) {
      return;
    }
    if (!this.hasActiveTorrentsSync()) {
      return;
    }
    cancel.data = this.promptQuit();
  },

  /** @returns {boolean} true when the user chose not to quit */
  promptQuit() {
    let l10n = this.syncL10n;
    let flags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1;
    let choice = Services.prompt.confirmEx(
      window,
      l10n.formatValueSync("m6e-torrent-quit-title"),
      l10n.formatValueSync("m6e-torrent-quit-message"),
      flags,
      l10n.formatValueSync("m6e-torrent-quit-button"),
      l10n.formatValueSync("m6e-torrent-dont-quit-button"),
      null,
      null,
      {}
    );
    return choice == 1;
  },

  formatBytes(bytes) {
    let units = ["byte", "kilobyte", "megabyte", "gigabyte", "terabyte"];
    let unit = 0;
    while (bytes >= 1000 && unit < units.length - 1) {
      bytes /= 1000;
      unit++;
    }
    return new Intl.NumberFormat(undefined, {
      style: "unit",
      unit: units[unit],
      unitDisplay: "short",
      maximumFractionDigits: unit ? 1 : 0,
    }).format(bytes);
  },

  formatSpeed(kilobytesPerSecond) {
    let unit = "kilobyte-per-second";
    let value = kilobytesPerSecond;
    if (value >= 1000) {
      value /= 1000;
      unit = "megabyte-per-second";
    }
    return new Intl.NumberFormat(undefined, {
      style: "unit",
      unit,
      unitDisplay: "short",
      maximumFractionDigits: unit == "kilobyte-per-second" ? 0 : 1,
    }).format(value);
  },

  formatDuration(seconds) {
    let steps = [
      ["second", 60],
      ["minute", 60],
      ["hour", 24],
      ["day", Infinity],
    ];
    let value = Math.max(0, seconds);
    let unit = "second";
    for (let [name, size] of steps) {
      unit = name;
      if (value < size) {
        break;
      }
      value /= size;
    }
    return new Intl.NumberFormat(undefined, {
      style: "unit",
      unit,
      unitDisplay: "short",
      maximumFractionDigits: unit == "second" || value >= 10 ? 0 : 1,
    }).format(value);
  },
};
