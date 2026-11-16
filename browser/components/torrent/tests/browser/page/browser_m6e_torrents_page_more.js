/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: about:torrents file priorities, per-torrent limits, queued and goal
// states and the Pieces, Speed and Log tabs (spec 8.1), driven by a mock
// service with synthetic data.

const ONE = tidOf("1");

function tabButton(doc, name) {
  return doc.getElementById(`m6e-torrents-tab-${name}`);
}

function fileSelects(doc) {
  return [...doc.querySelectorAll("#m6e-torrents-files tbody select")];
}

function changeTo(select, value) {
  select.value = value;
  select.dispatchEvent(
    new select.ownerDocument.defaultView.Event("change", { bubbles: true })
  );
}

async function openWithOne(overrides = {}, detailOverrides = {}) {
  const mock = installPageMock();
  const status = syntheticStatus({
    tid: ONE,
    name: "synthetic-one",
    ...overrides,
  });
  mock.torrents = [status];
  mock.details.set(ONE, syntheticDetail(status, detailOverrides));
  const page = await openTorrentsPage("#" + ONE);
  await TestUtils.waitForCondition(
    () => mock.callsTo("detail").length,
    "The detail is requested"
  );
  return { mock, ...page };
}

add_task(async function test_file_priority_is_editable() {
  const { mock, tab, doc } = await openWithOne();
  tabButton(doc, "files").click();
  await TestUtils.waitForCondition(
    () => fileSelects(doc).length == 2 && !fileSelects(doc)[0].disabled,
    "The priority selects are enabled once the detail is there"
  );
  const [first, second] = fileSelects(doc);
  is(first.value, "2", "The first file is Normal");
  is(second.value, "3", "The second file is High");
  is(
    first.getAttribute("aria-labelledby"),
    "m6e-torrents-file-name-0",
    "The select is named by the file name cell"
  );
  Assert.deepEqual(
    [...first.options].map(o => o.value),
    ["3", "2", "1", "0"],
    "High, Normal, Low, Do not download"
  );

  changeTo(second, "1");
  await TestUtils.waitForCondition(
    () => mock.callsTo("setFilePriority").length,
    "setFilePriority is called"
  );
  Assert.deepEqual(mock.callsTo("setFilePriority")[0].args, [
    ONE,
    [{ index: 1, priority: 1 }],
  ]);
  await TestUtils.waitForCondition(
    () => fileSelects(doc)[1].value == "1" && !fileSelects(doc)[1].disabled,
    "The new priority stays"
  );

  mock.actionError = new Error("synthetic failure");
  changeTo(fileSelects(doc)[0], "0");
  await TestUtils.waitForCondition(
    () => !doc.getElementById("m6e-torrents-alert").hidden,
    "A failure is shown"
  );
  await TestUtils.waitForCondition(
    () => fileSelects(doc)[0].value == "2",
    "The select goes back to the real priority"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_torrent_limits() {
  const { mock, tab, doc } = await openWithOne({
    downLimitKBps: 0,
    upLimitKBps: 30,
  });
  const down = doc.getElementById("m6e-torrents-limit-down");
  const up = doc.getElementById("m6e-torrents-limit-up");
  await TestUtils.waitForCondition(
    () => up.value == "30",
    "The inputs show the limits of the torrent"
  );
  is(down.value, "0", "No download limit");
  await TestUtils.waitForCondition(
    () => rowFor(doc, ONE).querySelector(".flag").textContent == "Limited",
    "A limited torrent is marked in the table"
  );

  down.value = "100";
  up.value = "0";
  doc.getElementById("m6e-torrents-limits-apply").click();
  await TestUtils.waitForCondition(
    () => mock.callsTo("setTorrentLimits").length,
    "setTorrentLimits is called"
  );
  Assert.deepEqual(mock.callsTo("setTorrentLimits")[0].args, [
    ONE,
    { downKBps: 100, upKBps: 0 },
  ]);

  down.value = "-5";
  doc.getElementById("m6e-torrents-limits-apply").click();
  await settle();
  is(mock.callsTo("setTorrentLimits").length, 1, "A negative limit is refused");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_queued_and_goal_states() {
  const { mock, tab, doc } = await openWithOne({ state: "queued" });
  const note = doc.getElementById("m6e-torrents-detail-note");
  await TestUtils.waitForCondition(
    () => !note.hidden && note.textContent.includes("waiting"),
    "A queued torrent explains the wait"
  );
  await TestUtils.waitForCondition(
    () => rowFor(doc, ONE).cells[3].textContent.includes("Queued"),
    "The state is Queued"
  );

  const reached = syntheticStatus({
    tid: ONE,
    name: "synthetic-one",
    state: "completed",
    goalReached: true,
  });
  mock.details.set(ONE, syntheticDetail(reached));
  mock.notify([reached]);
  await TestUtils.waitForCondition(
    () => note.textContent.includes("goal"),
    "A reached seeding goal is explained"
  );
  is(
    rowFor(doc, ONE).querySelector(".flag").textContent,
    "Goal reached",
    "The goal is marked in the table"
  );

  const plain = syntheticStatus({ tid: ONE, name: "synthetic-one" });
  mock.details.set(ONE, syntheticDetail(plain));
  mock.notify([plain]);
  await TestUtils.waitForCondition(() => note.hidden, "The note goes away");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_pieces_tab() {
  const have = btoa(String.fromCharCode(0b10110000));
  const { mock, tab, doc } = await openWithOne(
    {},
    { pieces: { count: 8, have } }
  );
  tabButton(doc, "pieces").click();
  const canvas = doc.getElementById("m6e-torrents-pieces-map");
  await TestUtils.waitForCondition(
    () => canvas.dataset.have,
    "The map is drawn"
  );
  is(canvas.dataset.have, "3", "Three pieces are there");
  is(canvas.dataset.count, "8", "Of eight");
  is(canvas.width, 8 * 6, "One cell per piece");
  const summary = doc.getElementById("m6e-torrents-pieces-summary");
  await TestUtils.waitForCondition(
    () => summary.textContent.includes("3 of 8"),
    "The counts are written out for screen readers"
  );
  is(canvas.getAttribute("role"), "img", "The map is an image");
  is(
    canvas.getAttribute("aria-labelledby"),
    "m6e-torrents-pieces-summary",
    "The text describes it"
  );

  delete mock.details.get(ONE).pieces;
  mock.notify(mock.torrents);
  await TestUtils.waitForCondition(
    () => !doc.getElementById("m6e-torrents-pieces-empty").hidden,
    "Unknown pieces are said to be unknown"
  );
  ok(canvas.hidden, "The map is hidden");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_speed_tab() {
  const { tab, doc, page } = await openWithOne();
  tabButton(doc, "speed").click();
  const now = Date.now();
  page._speed.set(ONE, [
    { t: now - 120000, down: 10, up: 0 },
    { t: now - 60000, down: 50, up: 5 },
    { t: now - 1000, down: 30, up: 10 },
  ]);
  page.renderSpeed(now);
  const down = doc.getElementById("m6e-torrents-speed-line-down");
  const up = doc.getElementById("m6e-torrents-speed-line-up");
  is(down.getAttribute("points").split(" ").length, 4, "Samples and the edge");
  ok(up.getAttribute("points"), "The upload line is drawn");
  const summary = doc.getElementById("m6e-torrents-speed-summary");
  await TestUtils.waitForCondition(
    () => summary.textContent.includes("peak"),
    "A text summary is there"
  );
  ok(summary.textContent.includes(page.formatSpeed(50)), "The peak is 50");
  is(
    doc.getElementById("m6e-torrents-speed-graph").getAttribute("role"),
    "img",
    "The graph is an image"
  );

  page.recordSamples(now + 10 * 60 * 1000);
  is(
    page._speed.get(ONE).length,
    1,
    "Only the last five minutes are kept"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_speed_is_sampled_from_notifications() {
  const { mock, tab, doc, page } = await openWithOne();
  const before = page._speed.get(ONE).length;
  await new Promise(resolve => setTimeout(resolve, 600));
  mock.notify([
    syntheticStatus({ tid: ONE, name: "synthetic-one", downKBps: 77 }),
  ]);
  await TestUtils.waitForCondition(
    () => page._speed.get(ONE).length > before,
    "A notification adds a sample"
  );
  is(page._speed.get(ONE).at(-1).down, 77, "The sample has the speed");
  ok(doc, "The page is alive");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_log_tab_shows_text_only() {
  const { mock, tab, doc } = await openWithOne();
  mock.log = [
    { time: Date.now() - 2000, level: "info", message: "synthetic note" },
    {
      time: Date.now() - 1000,
      level: "warn",
      message: "<img src=x> ‮evil",
    },
  ];
  tabButton(doc, "log").click();
  const items = () => [...doc.querySelectorAll("#m6e-torrents-log-list li")];
  await TestUtils.waitForCondition(() => items().length == 2, "Two lines");
  ok(items()[0].textContent.includes("synthetic note"), "The first line");
  is(items()[1].className, "warn", "The level is a class");
  ok(items()[1].textContent.includes("<U+202E>"), "Hidden characters show");
  is(doc.querySelectorAll("#m6e-torrents-log-list img").length, 0, "Inert");

  mock.log = [];
  mock.notify(mock.torrents);
  await TestUtils.waitForCondition(
    () => !doc.getElementById("m6e-torrents-log-empty").hidden,
    "An empty log says so"
  );
  BrowserTestUtils.removeTab(tab);
});
