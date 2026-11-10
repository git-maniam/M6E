/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: helpers for the BitTorrent browser tests (Phase 8). Every test drives
// gM6ETorrent through a mock service and a mock risk assessor, with synthetic
// metadata only. No test names a real torrent, info hash or tracker, and none
// reaches the network or starts the helper (spec 8.9, D38).

const SYNTHETIC_HASH = "0".repeat(40);
const SYNTHETIC_MAGNET = `magnet:?xt=urn:btih:${"a".repeat(40)}`;

function syntheticMetadata(overrides = {}) {
  return {
    infoHash: SYNTHETIC_HASH,
    name: "synthetic-sample",
    totalSize: 3000,
    pieceLength: 16384,
    files: [
      { index: 0, path: "docs/readme.txt", length: 100 },
      { index: 1, path: "docs/guide.txt", length: 200 },
      { index: 2, path: "data/blob.bin", length: 2500 },
      { index: 3, path: "data/.pad/512", length: 512, pad: true },
      { index: 4, path: "top.txt", length: 200 },
    ],
    trackers: [],
    webSeeds: [],
    isPrivate: false,
    ...overrides,
  };
}

/**
 *
 */
class MockTorrentService {
  constructor() {
    this.calls = [];
    this.activeTorrentCount = 0;
    this.torrents = [];
    this.fileMetadata = syntheticMetadata();
    this.addError = null;
    this.confirmError = null;
  }

  record(name, ...args) {
    this.calls.push({ name, args });
  }

  callsTo(name) {
    return this.calls.filter(c => c.name == name);
  }

  init() {
    this.record("init");
  }

  async addFile(path, source) {
    this.record("addFile", path, source);
    if (this.addError) {
      throw this.addError;
    }
    return { tid: SYNTHETIC_HASH, metadata: this.fileMetadata };
  }

  async addMagnet(uri, source) {
    this.record("addMagnet", uri, source);
    if (this.addError) {
      throw this.addError;
    }
    return { tid: SYNTHETIC_HASH };
  }

  async confirmAdd(tid, options) {
    this.record("confirmAdd", tid, options);
    if (this.confirmError) {
      throw this.confirmError;
    }
    return { status: { tid, state: "downloading" } };
  }

  async cancelAdd(tid) {
    this.record("cancelAdd", tid);
  }

  async list() {
    this.record("list");
    return { torrents: this.torrents };
  }

  async pause(tid) {
    this.record("pause", tid);
  }

  async resume(tid) {
    this.record("resume", tid);
  }

  async remove(tid, deleteFiles) {
    this.record("remove", tid, deleteFiles);
  }

  async pauseAll() {
    this.record("pauseAll");
  }

  async resumeAll() {
    this.record("resumeAll");
  }
}

/**
 *
 */
class MockRisk {
  constructor() {
    this.calls = [];
    this.result = { verdict: "ok", trusted: false, reasons: [] };
  }

  assess(metadata, source, extra) {
    this.calls.push({ metadata, source, extra });
    return this.result;
  }
}

function installMocks() {
  const service = new MockTorrentService();
  const risk = new MockRisk();
  gM6ETorrent.service = service;
  gM6ETorrent.risk = risk;
  gM6ETorrent.lastAddDialog = null;
  registerCleanupFunction(() => {
    gM6ETorrent.service = null;
    gM6ETorrent.risk = null;
    gM6ETorrent.lastAddDialog = null;
  });
  Assert.strictEqual(
    gM6ETorrent.service,
    service,
    "The mock service is in place, so no real torrent service is used"
  );
  return { service, risk };
}

async function enableTorrents() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["m6e.torrent.enabled", true],
      ["m6e.torrent.trustedSites", ""],
    ],
  });
}

async function waitForAddDialog(previous = null) {
  await TestUtils.waitForCondition(
    () =>
      gM6ETorrent.lastAddDialog &&
      gM6ETorrent.lastAddDialog.dialog != previous?.dialog,
    "The Add dialog is opened"
  );
  const opened = gM6ETorrent.lastAddDialog;
  await opened.dialog._dialogReady;
  const win = opened.dialog._frame.contentWindow;
  await win.document.mozSubdialogReady;
  return { ...opened, win, doc: win.document };
}

async function waitForDetails(doc) {
  await TestUtils.waitForCondition(
    () => doc.documentElement.hasAttribute("m6e-ready"),
    "The Add dialog has shown the torrent"
  );
}

async function closeAddDialog(opened) {
  opened.doc.getElementById("m6e-torrent-add-cancel").click();
  await opened.closedPromise;
}

function visible(doc, id) {
  return !doc.getElementById(id).hidden;
}

function fileRows(doc) {
  return [...doc.querySelectorAll("#m6e-torrent-add-tree li.file")];
}

async function settle() {
  for (let i = 0; i < 10; i++) {
    await TestUtils.waitForTick();
  }
}
