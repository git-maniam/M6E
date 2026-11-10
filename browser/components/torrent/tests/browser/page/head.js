/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: helpers for the about:torrents tests (Phase 8). The real
// M6ETorrentService singleton gets mock methods for the length of a test, so
// the page runs against synthetic data only. No test names a real torrent,
// info hash or tracker, and none reaches the network or starts the helper
// (spec 8.9, D38). Tracker hosts use the reserved ".invalid" top-level domain.

const { M6ETorrentService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs"
);

const TOPIC_CHANGED = "m6e-torrent-changed";
const SYNTHETIC_TRACKER = "http://tracker.invalid/announce";
const MOCKED_METHODS = ["list", "detail", "pause", "resume", "remove"];

function tidOf(char) {
  return char.repeat(40);
}

function syntheticStatus(overrides = {}) {
  return {
    tid: tidOf("1"),
    name: "synthetic-sample",
    state: "downloading",
    progress: 0.25,
    size: 3000,
    downloaded: 750,
    uploaded: 0,
    downKBps: 12,
    upKBps: 0,
    etaSeconds: 90,
    ratio: 0,
    seeds: 1,
    peers: 2,
    isPrivate: false,
    savePath: "/mock/downloads",
    sequential: false,
    addedAt: 1700000000000,
    error: null,
    ...overrides,
  };
}

function syntheticDetail(status, overrides = {}) {
  return {
    status,
    metadata: {
      infoHash: status.tid,
      name: status.name,
      totalSize: status.size,
      pieceLength: 1000,
      files: [
        { index: 0, path: "docs/readme.txt", length: 100 },
        { index: 1, path: "data/blob.bin", length: 2400 },
        { index: 2, path: "data/.pad/500", length: 500, pad: true },
      ],
      trackers: [SYNTHETIC_TRACKER],
      webSeeds: [],
      isPrivate: false,
    },
    trackers: [
      {
        url: SYNTHETIC_TRACKER,
        status: "working",
        seeds: 4,
        peers: 7,
        message: "",
      },
    ],
    peers: [
      {
        address: "192.0.2.10:6881",
        client: "synthetic-client",
        encrypted: true,
        downKBps: 5,
        upKBps: 1,
        progress: 0.5,
      },
    ],
    pieces: { count: 3, have: "AA==" },
    files: [
      { index: 0, priority: 2, progress: 1 },
      { index: 1, priority: 3, progress: 0.5 },
    ],
    ...overrides,
  };
}

class MockPageService {
  constructor() {
    this.calls = [];
    this.torrents = [];
    this.details = new Map();
    this.actionError = null;
  }

  callsTo(name) {
    return this.calls.filter(c => c.name == name);
  }

  record(name, ...args) {
    this.calls.push({ name, args });
  }

  async list() {
    this.record("list");
    return { torrents: this.torrents.map(t => ({ ...t })) };
  }

  async detail(tid) {
    this.record("detail", tid);
    const detail = this.details.get(tid);
    if (!detail) {
      throw new Error("not found");
    }
    return structuredClone(detail);
  }

  async pause(tid) {
    this.record("pause", tid);
    await this.act();
  }

  async resume(tid) {
    this.record("resume", tid);
    await this.act();
  }

  async remove(tid, deleteFiles) {
    this.record("remove", tid, deleteFiles);
    await this.act();
  }

  async act() {
    if (this.actionError) {
      throw this.actionError;
    }
  }

  notify(torrents) {
    this.torrents = torrents;
    Services.obs.notifyObservers(
      null,
      TOPIC_CHANGED,
      JSON.stringify({ torrents })
    );
  }
}

function installPageMock() {
  const mock = new MockPageService();
  for (const name of MOCKED_METHODS) {
    Object.defineProperty(M6ETorrentService, name, {
      configurable: true,
      writable: true,
      value: mock[name].bind(mock),
    });
  }
  registerCleanupFunction(() => {
    for (const name of MOCKED_METHODS) {
      delete M6ETorrentService[name];
    }
  });
  Assert.ok(
    Object.hasOwn(M6ETorrentService, "list"),
    "The mock list is in place, so the real service is not used"
  );
  return mock;
}

async function openTorrentsPage(hash = "") {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:torrents" + hash
  );
  const win = tab.linkedBrowser.contentWindow;
  await TestUtils.waitForCondition(
    () => win.document.documentElement.hasAttribute("m6e-ready"),
    "The page is ready"
  );
  await win.document.l10n.ready;
  return { tab, win, doc: win.document, page: win.gM6ETorrentsPage };
}

function tableRows(doc) {
  return [...doc.querySelectorAll("#m6e-torrents-rows tr")];
}

function rowNames(doc) {
  return tableRows(doc).map(row => row.querySelector(".name").textContent);
}

function filterCount(doc, name) {
  return Number(
    doc.querySelector(`button.filter[data-filter="${name}"] .count`).textContent
  );
}

function rowFor(doc, tid) {
  return doc.querySelector(`#m6e-torrents-rows tr[data-tid="${tid}"]`);
}

async function waitForRows(doc, count) {
  await TestUtils.waitForCondition(
    () => tableRows(doc).length == count,
    `The table has ${count} rows`
  );
}

async function settle() {
  for (let i = 0; i < 10; i++) {
    await TestUtils.waitForTick();
  }
}
