/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: about:torrents (Phase 8, D33). The page runs in the parent process and
// talks to M6ETorrentService directly. Torrent names, paths, trackers and peer
// clients are untrusted: they are only ever shown through textContent (or an
// attribute value) after displayName, never as part of a Fluent message value.

"use strict";

const m6eTorrentsModules = {};
ChromeUtils.defineESModuleGetters(m6eTorrentsModules, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  M6ETorrentService:
    "moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs",
});

const M6E_TORRENTS_TOPIC = "m6e-torrent-changed";
const M6E_TORRENTS_ENABLED_PREF = "m6e.torrent.enabled";
const M6E_TORRENTS_INVISIBLE = /[\p{Cc}\p{Cf}\u2028\u2029]/gu;
const M6E_TORRENTS_TID = /^[0-9a-f]{40}$/;
const M6E_TORRENTS_TRACKER_URL = /^(?:https?|udp|wss?):\/\//i;
const M6E_TORRENTS_KNOWN_STATES = [
  "checking",
  "queued",
  "downloading",
  "seeding",
  "paused",
  "completed",
  "error",
];
const M6E_TORRENTS_TRACKER_STATES = [
  "working",
  "updating",
  "error",
  "disabled",
];
const M6E_TORRENTS_PRIORITIES = { 0: "skip", 1: "low", 2: "normal", 3: "high" };
const M6E_TORRENTS_CAN_PAUSE = ["checking", "queued", "downloading", "seeding"];
const M6E_TORRENTS_CAN_RESUME = ["paused", "error"];
const M6E_TORRENTS_ACTIVE = ["downloading", "seeding"];
const M6E_TORRENTS_MAX_PEER_ROWS = 500;
const M6E_TORRENTS_GENERAL_FIELDS = [
  "name",
  "status",
  "progress",
  "size",
  "downloaded",
  "uploaded",
  "ratio",
  "down",
  "up",
  "eta",
  "seeds",
  "peers",
  "added",
  "path",
  "hash",
  "pieces",
  "private",
  "sequential",
  "error",
];
const M6E_TORRENTS_COLUMNS = [
  "name",
  "size",
  "progress",
  "status",
  "down",
  "up",
  "eta",
  "ratio",
  "seeds",
  "peers",
  "added",
];
const M6E_TORRENTS_NONE = "\u2013";

function m6eTorrentsIsErrored(t) {
  return t.state == "error" || !!t.error;
}

const M6E_TORRENTS_FILTERS = {
  all: () => true,
  downloading: t => t.state == "downloading",
  seeding: t => t.state == "seeding",
  completed: t =>
    t.state == "completed" ||
    t.state == "seeding" ||
    (t.progress >= 1 && !m6eTorrentsIsErrored(t)),
  paused: t => t.state == "paused",
  active: t => M6E_TORRENTS_ACTIVE.includes(t.state),
  inactive: t =>
    !M6E_TORRENTS_ACTIVE.includes(t.state) && !m6eTorrentsIsErrored(t),
  errored: m6eTorrentsIsErrored,
};

const M6E_TORRENTS_SORT_KEYS = {
  name: t => String(t.name ?? ""),
  size: t => t.size ?? 0,
  progress: t => t.progress ?? 0,
  status: t => String(t.state ?? ""),
  down: t => t.downKBps ?? 0,
  up: t => t.upKBps ?? 0,
  eta: t => t.etaSeconds ?? null,
  ratio: t => t.ratio ?? 0,
  seeds: t => t.seeds ?? -1,
  peers: t => t.peers ?? -1,
  added: t => t.addedAt ?? 0,
};

var gM6ETorrentsPage = {
  torrents: [],
  filter: "all",
  sort: null,
  selectedTid: null,
  tab: "general",
  detail: null,
  detailUnavailable: false,
  removeTid: null,
  _rows: new Map(),
  _fileRows: new Map(),
  _filesKey: "",
  _detailSeq: 0,
  _detailApplied: 0,
  _listSeq: 0,
  _listApplied: 0,
  _loaded: false,
  _observing: false,

  get service() {
    return m6eTorrentsModules.M6ETorrentService;
  },

  get enabled() {
    return Services.prefs.getBoolPref(M6E_TORRENTS_ENABLED_PREF, true);
  },

  get selected() {
    return this.torrents.find(t => t.tid == this.selectedTid) ?? null;
  },

  async init() {
    this.buildGeneral();
    this.attachEvents();
    this.selectedTid = this.tidFromHash();
    this.updateSortHeaders();
    if (this.enabled) {
      Services.obs.addObserver(this, M6E_TORRENTS_TOPIC);
      this._observing = true;
      await this.refreshList();
    } else {
      this.render();
    }
    this._loaded = true;
    document.documentElement.setAttribute("m6e-ready", "true");
  },

  uninit() {
    if (this._observing) {
      Services.obs.removeObserver(this, M6E_TORRENTS_TOPIC);
      this._observing = false;
    }
  },

  observe(subject, topic, data) {
    if (topic != M6E_TORRENTS_TOPIC) {
      return;
    }
    try {
      this.setTorrents(JSON.parse(data).torrents);
    } catch (e) {
      console.error("M6E: bad torrent notification", e);
    }
  },

  $(id) {
    return document.getElementById(id);
  },

  /**
   * Makes a name safe to show: invisible and direction-changing characters
   * become visible escapes, so a name cannot disguise itself.
   */
  displayName(name) {
    return String(name ?? "").replace(
      M6E_TORRENTS_INVISIBLE,
      ch =>
        `<U+${ch.codePointAt(0).toString(16).toUpperCase().padStart(4, "0")}>`
    );
  },

  tidFromHash() {
    let tid = "";
    try {
      tid = decodeURIComponent(location.hash.slice(1));
    } catch (e) {
      // A malformed hash selects nothing.
    }
    return M6E_TORRENTS_TID.test(tid) ? tid : null;
  },

  /* Data */

  async refreshList() {
    let seq = ++this._listSeq;
    let result;
    try {
      result = await this.service.list();
    } catch (e) {
      console.error("M6E: could not list torrents", e);
      return;
    }
    if (seq < this._listApplied) {
      return;
    }
    this._listApplied = seq;
    this.setTorrents(Array.isArray(result) ? result : result?.torrents);
  },

  setTorrents(list) {
    this.torrents = (Array.isArray(list) ? list : []).filter(
      t => t && typeof t.tid == "string"
    );
    let tid = this.selectedTid;
    if (tid && !this.torrents.some(t => t.tid == tid)) {
      this.selectedTid = null;
      this.updateHash();
      this.detail = null;
      this._detailSeq++;
    }
    this.render();
    if (this.selectedTid && !document.hidden) {
      this.loadDetail();
    }
  },

  async loadDetail() {
    let tid = this.selectedTid;
    if (!tid) {
      return;
    }
    let seq = ++this._detailSeq;
    let detail = null;
    let failed = false;
    try {
      detail = await this.service.detail(tid);
    } catch (e) {
      failed = true;
    }
    if (tid != this.selectedTid || seq < this._detailApplied) {
      return;
    }
    this._detailApplied = seq;
    this.detail = failed ? null : detail;
    this.detailUnavailable = failed || !detail;
    this.renderDetail();
  },

  /* Selection */

  select(tid, { focus = false } = {}) {
    if (tid == this.selectedTid) {
      if (focus) {
        this._rows.get(tid)?.tr.focus();
      }
      return;
    }
    this.selectedTid = tid;
    this.detail = null;
    this.detailUnavailable = false;
    this._filesKey = "";
    this._detailSeq++;
    this.$("m6e-torrents-info").textContent = "";
    this.updateHash();
    this.render();
    if (tid) {
      this.loadDetail();
    }
    if (focus && tid) {
      this._rows.get(tid)?.tr.focus();
    }
  },

  updateHash() {
    let hash = this.selectedTid ? "#" + this.selectedTid : "";
    if (location.hash != hash) {
      history.replaceState(null, "", hash || "#");
    }
  },

  visibleTorrents() {
    let list = this.torrents.filter(M6E_TORRENTS_FILTERS[this.filter]);
    if (this.sort) {
      let key = M6E_TORRENTS_SORT_KEYS[this.sort.key];
      let direction = this.sort.dir == "descending" ? -1 : 1;
      list.sort((a, b) => {
        let x = key(a);
        let y = key(b);
        if (x === null || y === null) {
          return x === y ? 0 : x === null ? 1 : -1;
        }
        let order =
          typeof x == "string"
            ? x.localeCompare(y, undefined, {
                numeric: true,
                sensitivity: "base",
              })
            : x - y;
        return order * direction;
      });
    }
    return list;
  },

  /* Rendering the table */

  render() {
    let visible = this.visibleTorrents();
    this.renderFilters();
    this.renderRows(visible);
    this.renderNotice(visible);
    this.renderToolbar();
    this.renderDetail();
  },

  renderFilters() {
    for (let button of document.querySelectorAll("button.filter")) {
      let name = button.dataset.filter;
      let count = this.torrents.filter(M6E_TORRENTS_FILTERS[name]).length;
      button.querySelector(".count").textContent = String(count);
      if (name == this.filter) {
        button.setAttribute("aria-current", "true");
      } else {
        button.removeAttribute("aria-current");
      }
    }
  },

  createRow(tid) {
    let tr = document.createElement("tr");
    tr.setAttribute("role", "row");
    tr.dataset.tid = tid;
    let cells = {};
    for (let column of M6E_TORRENTS_COLUMNS) {
      let td = document.createElement("td");
      td.className = column;
      cells[column] = td;
      tr.append(td);
    }
    let name = document.createElement("span");
    name.className = "name";
    cells.name.append(name);
    let bar = document.createElement("progress");
    bar.max = 1;
    bar.setAttribute("aria-hidden", "true");
    let percent = document.createElement("span");
    percent.className = "percent";
    cells.progress.append(bar, percent);
    let state = document.createElement("span");
    cells.status.append(state);
    return { tr, cells, name, bar, percent, state, last: {} };
  },

  setText(row, key, el, text) {
    if (row.last[key] !== text) {
      row.last[key] = text;
      el.textContent = text;
    }
  },

  updateRow(row, t) {
    let { cells } = row;
    let name = this.displayName(t.name);
    this.setText(row, "name", row.name, name);
    row.tr.title = name;
    this.setText(row, "size", cells.size, this.formatBytes(t.size ?? 0));

    let fraction = Math.min(1, Math.max(0, t.progress || 0));
    row.bar.value = fraction;
    this.setText(row, "percent", row.percent, this.formatPercent(fraction));

    if (row.last.state !== t.state) {
      row.last.state = t.state;
      if (M6E_TORRENTS_KNOWN_STATES.includes(t.state)) {
        document.l10n.setAttributes(
          row.state,
          `m6e-torrent-state-${t.state}`
        );
      } else {
        row.state.removeAttribute("data-l10n-id");
        row.state.textContent = this.displayName(t.state);
      }
    }
    row.cells.status.title = t.error
      ? this.displayName(t.error.message)
      : "";

    this.setText(row, "down", cells.down, this.speedOrNone(t.downKBps));
    this.setText(row, "up", cells.up, this.speedOrNone(t.upKBps));
    this.setText(row, "eta", cells.eta, this.etaText(t));
    this.setText(row, "ratio", cells.ratio, this.formatRatio(t.ratio));
    this.setText(row, "seeds", cells.seeds, this.countOrNone(t.seeds));
    this.setText(row, "peers", cells.peers, this.countOrNone(t.peers));
    this.setText(row, "added", cells.added, this.formatDate(t.addedAt));
  },

  renderRows(visible) {
    let tbody = this.$("m6e-torrents-rows");
    let known = new Set(this.torrents.map(t => t.tid));
    let lostFocus = false;
    for (let tid of [...this._rows.keys()]) {
      if (!known.has(tid)) {
        lostFocus ||= this._rows.get(tid).tr.contains(document.activeElement);
        this._rows.get(tid).tr.remove();
        this._rows.delete(tid);
      }
    }
    let wanted = [];
    for (let t of visible) {
      let row = this._rows.get(t.tid);
      if (!row) {
        row = this.createRow(t.tid);
        this._rows.set(t.tid, row);
      }
      this.updateRow(row, t);
      wanted.push(row.tr);
    }

    let same =
      tbody.children.length == wanted.length &&
      wanted.every((tr, i) => tbody.children[i] == tr);
    if (!same) {
      let focused = tbody.contains(document.activeElement)
        ? document.activeElement
        : null;
      tbody.replaceChildren(...wanted);
      focused?.focus({ preventScroll: true });
    }

    let stop = visible.some(t => t.tid == this.selectedTid)
      ? this.selectedTid
      : visible[0]?.tid;
    for (let [tid, row] of this._rows) {
      let isSelected = tid == this.selectedTid;
      row.tr.setAttribute("aria-selected", String(isSelected));
      row.tr.tabIndex = tid == stop ? 0 : -1;
    }
    if (lostFocus) {
      this._rows.get(stop)?.tr.focus({ preventScroll: true });
    }
    document.l10n.setAttributes(
      this.$("m6e-torrents-count"),
      "m6e-torrents-count",
      { count: visible.length }
    );
  },

  renderNotice(visible) {
    let notice = this.$("m6e-torrents-notice");
    let id = null;
    if (!this.enabled) {
      id = "m6e-torrents-disabled";
    } else if (!this.torrents.length) {
      id = "m6e-torrents-empty";
    } else if (!visible.length) {
      id = "m6e-torrents-no-match";
    }
    if (id) {
      document.l10n.setAttributes(notice, id);
    } else {
      notice.removeAttribute("data-l10n-id");
      notice.textContent = "";
    }
  },

  renderToolbar() {
    let t = this.selected;
    let state = t?.state;
    let disabled = {
      pause: !t || !M6E_TORRENTS_CAN_PAUSE.includes(state),
      resume: !t || !M6E_TORRENTS_CAN_RESUME.includes(state),
      remove: !t,
      "open-folder": !t || !t.savePath,
      "copy-magnet": !t,
    };
    for (let button of this.$("m6e-torrents-toolbar").querySelectorAll(
      "button"
    )) {
      button.disabled = disabled[button.dataset.action];
    }
  },

  updateSortHeaders() {
    for (let th of document.querySelectorAll("#m6e-torrents-table th")) {
      th.setAttribute(
        "aria-sort",
        this.sort?.key == th.dataset.sort ? this.sort.dir : "none"
      );
    }
  },

  /* Rendering the details */

  buildGeneral() {
    let list = this.$("m6e-torrents-general");
    for (let key of M6E_TORRENTS_GENERAL_FIELDS) {
      let dt = document.createElement("dt");
      dt.id = `m6e-torrents-general-label-${key}`;
      document.l10n.setAttributes(dt, `m6e-torrents-general-${key}`);
      let dd = document.createElement("dd");
      dd.id = `m6e-torrents-general-${key}`;
      dd.dataset.field = key;
      list.append(dt, dd);
    }
  },

  /**
   * Shows plain text or a Fluent message (an object with id and args) in an
   * element without redoing work when nothing changed.
   */
  setValue(el, value) {
    let key = typeof value == "string" ? "t:" + value : JSON.stringify(value);
    if (el._m6eKey === key) {
      return;
    }
    el._m6eKey = key;
    if (typeof value == "string") {
      el.removeAttribute("data-l10n-id");
      el.removeAttribute("data-l10n-args");
      el.textContent = value;
    } else {
      document.l10n.setAttributes(el, value.id, value.args);
    }
  },

  stateValue(state) {
    return M6E_TORRENTS_KNOWN_STATES.includes(state)
      ? { id: `m6e-torrent-state-${state}` }
      : this.displayName(state);
  },

  yesNo(flag) {
    return { id: flag ? "m6e-torrents-yes" : "m6e-torrents-no" };
  },

  generalValues(t, metadata, pieces) {
    let pieceLength = metadata?.pieceLength;
    let count =
      pieces?.count ??
      (pieceLength ? Math.ceil((metadata.totalSize ?? 0) / pieceLength) : null);
    return {
      name: this.displayName(t.name),
      status: this.stateValue(t.state),
      progress: this.formatPercent(Math.min(1, Math.max(0, t.progress || 0))),
      size: this.formatBytes(t.size ?? 0),
      downloaded: this.formatBytes(t.downloaded ?? 0),
      uploaded: this.formatBytes(t.uploaded ?? 0),
      ratio: this.formatRatio(t.ratio),
      down: this.formatSpeed(t.downKBps || 0),
      up: this.formatSpeed(t.upKBps || 0),
      eta: this.etaText(t),
      seeds: this.countOrNone(t.seeds),
      peers: this.countOrNone(t.peers),
      added: this.formatDate(t.addedAt),
      path: this.displayName(t.savePath ?? ""),
      hash: t.tid,
      pieces:
        count && pieceLength
          ? {
              id: "m6e-torrents-pieces-value",
              args: { count, size: this.formatBytes(pieceLength) },
            }
          : M6E_TORRENTS_NONE,
      private: this.yesNo(!!t.isPrivate),
      sequential: this.yesNo(!!t.sequential),
      error: t.error ? this.displayName(t.error.message) : "",
    };
  },

  renderDetail() {
    let t = this.selected;
    let hint = this.$("m6e-torrents-detail-hint");
    let body = this.$("m6e-torrents-detail-body");
    hint.hidden = !!t;
    body.hidden = !t;
    if (!t) {
      return;
    }
    this.$("m6e-torrents-detail-unavailable").hidden = !this.detailUnavailable;

    let status = this.detail?.status ?? t;
    let values = this.generalValues(
      { ...t, ...status },
      this.detail?.metadata,
      this.detail?.pieces
    );
    for (let key of M6E_TORRENTS_GENERAL_FIELDS) {
      let dd = this.$(`m6e-torrents-general-${key}`);
      let hide = key == "error" && !values.error;
      dd.hidden = hide;
      this.$(`m6e-torrents-general-label-${key}`).hidden = hide;
      this.setValue(dd, values[key]);
    }

    if (this.tab == "files") {
      this.renderFiles();
    } else if (this.tab == "trackers") {
      this.renderTrackers();
    } else if (this.tab == "peers") {
      this.renderPeers();
    }
  },

  makeCell(text, className) {
    let td = document.createElement("td");
    if (className) {
      td.className = className;
    }
    td.textContent = text;
    return td;
  },

  renderFiles() {
    let tbody = this.$("m6e-torrents-files").tBodies[0];
    let files = (this.detail?.metadata?.files ?? []).filter(f => !f.pad);
    this.$("m6e-torrents-files-empty").hidden = !!files.length;
    this.$("m6e-torrents-files").hidden = !files.length;
    let key = `${this.selectedTid}:${files.length}`;
    if (key != this._filesKey) {
      this._filesKey = key;
      this._fileRows.clear();
      tbody.replaceChildren();
      for (let file of files) {
        let tr = document.createElement("tr");
        let name = this.makeCell(this.displayName(file.path), "name");
        name.title = name.textContent;
        let size = this.makeCell(this.formatBytes(file.length ?? 0), "size");
        let progress = document.createElement("td");
        progress.className = "progress";
        let bar = document.createElement("progress");
        bar.max = 1;
        bar.setAttribute("aria-hidden", "true");
        let percent = document.createElement("span");
        percent.className = "percent";
        progress.append(bar, percent);
        let priority = document.createElement("td");
        priority.className = "priority";
        tr.append(name, size, progress, priority);
        tbody.append(tr);
        this._fileRows.set(file.index, { bar, percent, priority });
      }
    }
    for (let entry of this.detail?.files ?? []) {
      let row = this._fileRows.get(entry.index);
      if (!row) {
        continue;
      }
      let fraction = Math.min(1, Math.max(0, entry.progress || 0));
      row.bar.value = fraction;
      let percent = this.formatPercent(fraction);
      if (row.percent.textContent != percent) {
        row.percent.textContent = percent;
      }
      let name = M6E_TORRENTS_PRIORITIES[entry.priority];
      this.setValue(
        row.priority,
        name ? { id: `m6e-torrent-priority-${name}` } : String(entry.priority)
      );
    }
  },

  renderTrackers() {
    let table = this.$("m6e-torrents-trackers");
    let trackers = this.detail?.trackers ?? [];
    this.$("m6e-torrents-trackers-empty").hidden = !!trackers.length;
    table.hidden = !trackers.length;
    let rows = trackers.map(tracker => {
      let tr = document.createElement("tr");
      let url = this.makeCell(this.displayName(tracker.url), "name");
      url.title = url.textContent;
      let status = document.createElement("td");
      this.setValue(
        status,
        M6E_TORRENTS_TRACKER_STATES.includes(tracker.status)
          ? { id: `m6e-torrents-tracker-${tracker.status}` }
          : this.displayName(tracker.status)
      );
      tr.append(
        url,
        status,
        this.makeCell(this.countOrNone(tracker.seeds), "number"),
        this.makeCell(this.countOrNone(tracker.peers), "number"),
        this.makeCell(this.displayName(tracker.message ?? ""), "message")
      );
      return tr;
    });
    table.tBodies[0].replaceChildren(...rows);
  },

  renderPeers() {
    let table = this.$("m6e-torrents-peers");
    let peers = (this.detail?.peers ?? []).slice(0, M6E_TORRENTS_MAX_PEER_ROWS);
    this.$("m6e-torrents-peers-empty").hidden = !!peers.length;
    table.hidden = !peers.length;
    let rows = peers.map(peer => {
      let tr = document.createElement("tr");
      let encrypted = document.createElement("td");
      this.setValue(encrypted, {
        id: peer.encrypted
          ? "m6e-torrents-peer-encrypted"
          : "m6e-torrents-peer-plain",
      });
      tr.append(
        this.makeCell(this.displayName(peer.address), "name"),
        this.makeCell(this.displayName(peer.client ?? ""), "name"),
        encrypted,
        this.makeCell(this.speedOrNone(peer.downKBps), "number"),
        this.makeCell(this.speedOrNone(peer.upKBps), "number"),
        this.makeCell(
          this.formatPercent(Math.min(1, Math.max(0, peer.progress || 0))),
          "number"
        )
      );
      return tr;
    });
    table.tBodies[0].replaceChildren(...rows);
  },

  selectTab(name, { focus = false } = {}) {
    this.tab = name;
    let tabs = document.querySelectorAll("#m6e-torrents-tabs [role=tab]");
    for (let tab of tabs) {
      let on = tab.dataset.tab == name;
      tab.setAttribute("aria-selected", String(on));
      tab.tabIndex = on ? 0 : -1;
      this.$(tab.getAttribute("aria-controls")).hidden = !on;
      if (on && focus) {
        tab.focus();
      }
    }
    this.renderDetail();
  },

  /* Events */

  attachEvents() {
    let sidebar = this.$("m6e-torrents-filter-list");
    sidebar.addEventListener("click", event => {
      let button = event.target.closest("button.filter");
      if (button) {
        this.filter = button.dataset.filter;
        this.render();
      }
    });

    this.$("m6e-torrents-table")
      .tHead.addEventListener("click", event => {
        let th = event.target.closest("th[data-sort]");
        if (!th) {
          return;
        }
        let key = th.dataset.sort;
        let dir =
          this.sort?.key == key && this.sort.dir == "ascending"
            ? "descending"
            : "ascending";
        this.sort = { key, dir };
        this.updateSortHeaders();
        this.render();
      });

    let tbody = this.$("m6e-torrents-rows");
    tbody.addEventListener("click", event => {
      let tr = event.target.closest("tr");
      if (tr) {
        this.select(tr.dataset.tid);
      }
    });
    tbody.addEventListener("focusin", event => {
      let tr = event.target.closest("tr");
      if (tr && event.target == tr) {
        this.select(tr.dataset.tid);
      }
    });
    tbody.addEventListener("keydown", event => this.onTableKey(event));

    this.$("m6e-torrents-toolbar").addEventListener("click", event => {
      let button = event.target.closest("button[data-action]");
      if (button && !button.disabled) {
        this.doAction(button.dataset.action);
      }
    });

    let dialog = this.$("m6e-torrents-remove-dialog");
    dialog.addEventListener("click", event => {
      let button = event.target.closest("button[data-action]");
      if (!button) {
        return;
      }
      let tid = this.removeTid;
      dialog.close();
      if (button.dataset.action == "remove-keep") {
        this.runAction(() => this.service.remove(tid, false));
      } else if (button.dataset.action == "remove-delete") {
        this.runAction(() => this.service.remove(tid, true));
      }
    });
    dialog.addEventListener("close", () => {
      this.removeTid = null;
      let row = this._rows.get(this.selectedTid);
      (row?.tr ?? this.$("m6e-torrents-remove")).focus();
    });

    let tabs = this.$("m6e-torrents-tabs");
    tabs.addEventListener("click", event => {
      let tab = event.target.closest("[role=tab]");
      if (tab) {
        this.selectTab(tab.dataset.tab);
      }
    });
    tabs.addEventListener("keydown", event => {
      let all = [...tabs.querySelectorAll("[role=tab]")];
      let index = all.findIndex(tab => tab.dataset.tab == this.tab);
      let next;
      switch (event.key) {
        case "ArrowRight":
          next = (index + 1) % all.length;
          break;
        case "ArrowLeft":
          next = (index - 1 + all.length) % all.length;
          break;
        case "Home":
          next = 0;
          break;
        case "End":
          next = all.length - 1;
          break;
        default:
          return;
      }
      event.preventDefault();
      this.selectTab(all[next].dataset.tab, { focus: true });
    });

    window.addEventListener("hashchange", async () => {
      let tid = this.tidFromHash();
      if (tid && tid != this.selectedTid) {
        // M6E P8.4: a torrent added a moment ago may not be listed yet, and
        // an unlisted selection is dropped by setTorrents.
        if (!this.torrents.some(t => t.tid == tid)) {
          await this.refreshList();
        }
        if (this.tidFromHash() == tid) {
          this.select(tid);
        }
      }
    });
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden && this._loaded && this.enabled) {
        this.refreshList();
      }
    });
    window.addEventListener("unload", () => this.uninit(), { once: true });
  },

  onTableKey(event) {
    let visible = this.visibleTorrents();
    if (!visible.length || event.target.localName != "tr") {
      return;
    }
    let index = visible.findIndex(t => t.tid == event.target.dataset.tid);
    let next = null;
    switch (event.key) {
      case "ArrowDown":
        next = Math.min(visible.length - 1, index + 1);
        break;
      case "ArrowUp":
        next = Math.max(0, index - 1);
        break;
      case "PageDown":
        next = Math.min(visible.length - 1, index + 10);
        break;
      case "PageUp":
        next = Math.max(0, index - 10);
        break;
      case "Home":
        next = 0;
        break;
      case "End":
        next = visible.length - 1;
        break;
      case " ":
        event.preventDefault();
        this.doAction(
          M6E_TORRENTS_CAN_PAUSE.includes(visible[index].state)
            ? "pause"
            : "resume"
        );
        return;
      case "Delete":
      case "Backspace":
        event.preventDefault();
        this.doAction("remove");
        return;
      case "Enter":
        event.preventDefault();
        this.$(`m6e-torrents-tab-${this.tab}`).focus();
        return;
      default:
        return;
    }
    event.preventDefault();
    this.select(visible[next].tid, { focus: true });
  },

  /* Actions */

  async doAction(action) {
    let t = this.selected;
    if (!t) {
      return;
    }
    this.clearAlert();
    switch (action) {
      case "pause":
        if (M6E_TORRENTS_CAN_PAUSE.includes(t.state)) {
          await this.runAction(() => this.service.pause(t.tid));
        }
        break;
      case "resume":
        if (M6E_TORRENTS_CAN_RESUME.includes(t.state)) {
          await this.runAction(() => this.service.resume(t.tid));
        }
        break;
      case "remove":
        this.openRemoveDialog(t);
        break;
      case "open-folder":
        this.reveal(t);
        break;
      case "copy-magnet":
        await this.copyMagnet(t);
        break;
    }
  },

  async runAction(action) {
    try {
      await action();
    } catch (e) {
      console.error("M6E: torrent action failed", e);
      await this.showAlert(e);
    }
    await this.refreshList();
  },

  openRemoveDialog(t) {
    this.removeTid = t.tid;
    this.$("m6e-torrents-remove-name").textContent = this.displayName(t.name);
    this.$("m6e-torrents-remove-dialog").showModal();
    this.$("m6e-torrents-remove-cancel").focus();
  },

  async showAlert(error) {
    let alert = this.$("m6e-torrents-alert");
    let prefix = await document.l10n.formatValue("m6e-torrents-action-failed");
    alert.textContent = `${prefix} ${this.displayName(error?.message ?? "")}`;
    alert.hidden = false;
  },

  clearAlert() {
    let alert = this.$("m6e-torrents-alert");
    alert.hidden = true;
    alert.textContent = "";
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
      let { File } = m6eTorrentsModules.FileUtils;
      let target = new File(PathUtils.join(torrent.savePath, torrent.name));
      (target.exists() ? target : new File(torrent.savePath)).reveal();
    } catch (e) {
      console.error("M6E: could not show the torrent in Finder", e);
    }
  },

  /**
   * Builds a magnet link from the info hash, the name and the trackers of
   * the torrent file. A v2-only torrent is identified by the first 40
   * characters of its hash, so its link is not usable by other clients.
   */
  buildMagnet(tid, name, trackers) {
    let clean = String(name ?? "").replace(M6E_TORRENTS_INVISIBLE, "");
    let uri = `magnet:?xt=urn:btih:${tid}`;
    if (clean) {
      uri += `&dn=${encodeURIComponent(clean)}`;
    }
    for (let url of trackers) {
      if (M6E_TORRENTS_TRACKER_URL.test(url)) {
        uri += `&tr=${encodeURIComponent(url)}`;
      }
    }
    return uri;
  },

  async copyMagnet(t) {
    let trackers = [];
    try {
      let detail = await this.service.detail(t.tid);
      trackers = detail.metadata?.trackers ?? [];
      if (!trackers.length) {
        trackers = (detail.trackers ?? []).map(tracker => tracker.url);
      }
    } catch (e) {
      console.error("M6E: could not read the trackers for the magnet link", e);
    }
    this.copyText(this.buildMagnet(t.tid, t.name, trackers));
    document.l10n.setAttributes(
      this.$("m6e-torrents-info"),
      "m6e-torrents-magnet-copied"
    );
  },

  copyText(text) {
    Cc["@mozilla.org/widget/clipboardhelper;1"]
      .getService(Ci.nsIClipboardHelper)
      .copyString(text);
  },

  /* Formatting */

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

  speedOrNone(kilobytesPerSecond) {
    return kilobytesPerSecond > 0
      ? this.formatSpeed(kilobytesPerSecond)
      : M6E_TORRENTS_NONE;
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

  etaText(t) {
    return t.state == "downloading" && t.etaSeconds != null
      ? this.formatDuration(t.etaSeconds)
      : M6E_TORRENTS_NONE;
  },

  formatPercent(fraction) {
    return new Intl.NumberFormat(undefined, {
      style: "percent",
      maximumFractionDigits: 1,
    }).format(Math.floor(fraction * 1000) / 1000);
  },

  formatRatio(ratio) {
    return new Intl.NumberFormat(undefined, {
      minimumFractionDigits: 2,
      maximumFractionDigits: 2,
    }).format(ratio || 0);
  },

  countOrNone(count) {
    return typeof count == "number" && count >= 0
      ? String(count)
      : M6E_TORRENTS_NONE;
  },

  formatDate(ms) {
    return ms
      ? new Intl.DateTimeFormat(undefined, {
          dateStyle: "medium",
          timeStyle: "short",
        }).format(new Date(ms))
      : M6E_TORRENTS_NONE;
  },
};

gM6ETorrentsPage.init();
