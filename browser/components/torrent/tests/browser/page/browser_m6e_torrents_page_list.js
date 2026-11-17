/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: about:torrents sidebar, table, keyboard and row actions (spec 8.1),
// driven by a mock service.

const ALPHA = tidOf("a");
const BRAVO = tidOf("b");
const CHARLIE = tidOf("c");
const DELTA = tidOf("d");
const ECHO = tidOf("e");

function fiveTorrents() {
  return [
    syntheticStatus({ tid: ALPHA, name: "synthetic-alpha" }),
    syntheticStatus({
      tid: BRAVO,
      name: "synthetic-bravo",
      state: "seeding",
      progress: 1,
      size: 2000000,
      downKBps: 0,
      upKBps: 1500,
      etaSeconds: null,
      ratio: 1.5,
      seeds: -1,
      peers: 4,
      addedAt: 1700000100000,
    }),
    syntheticStatus({
      tid: CHARLIE,
      name: "synthetic-charlie",
      state: "paused",
      progress: 0.4,
      downKBps: 0,
      etaSeconds: null,
      addedAt: 1700000200000,
    }),
    syntheticStatus({
      tid: DELTA,
      name: "synthetic-delta",
      state: "error",
      progress: 0.1,
      downKBps: 0,
      etaSeconds: null,
      error: { code: "io-error", message: "synthetic failure" },
      addedAt: 1700000300000,
    }),
    syntheticStatus({
      tid: ECHO,
      name: "synthetic-echo",
      state: "completed",
      progress: 1,
      downKBps: 0,
      etaSeconds: null,
      addedAt: 1700000400000,
    }),
  ];
}

add_task(async function test_filters_counts_and_columns() {
  const mock = installPageMock();
  mock.torrents = fiveTorrents();
  const { tab, doc } = await openTorrentsPage();

  Assert.deepEqual(
    rowNames(doc),
    [
      "synthetic-alpha",
      "synthetic-bravo",
      "synthetic-charlie",
      "synthetic-delta",
      "synthetic-echo",
    ],
    "All five torrents are listed in the service order"
  );
  is(
    doc.getElementById("m6e-torrents-table").getAttribute("role"),
    "grid",
    "The table is a grid"
  );
  ok(
    doc.getElementById("m6e-torrents-table").getAttribute("aria-label"),
    "The table has an accessible name"
  );
  ok(
    BrowserTestUtils.isHidden(doc.getElementById("m6e-torrents-notice")),
    "No notice is shown when there are torrents"
  );

  const counts = {
    all: 5,
    downloading: 1,
    seeding: 1,
    completed: 2,
    paused: 1,
    active: 2,
    inactive: 2,
    errored: 1,
  };
  for (const [name, count] of Object.entries(counts)) {
    is(filterCount(doc, name), count, `The ${name} filter counts ${count}`);
  }

  const bravo = rowFor(doc, BRAVO);
  const cell = name => bravo.querySelector(`td.${name}`).textContent;
  is(cell("name"), "synthetic-bravo", "The name column");
  is(cell("size"), "2 MB", "The size column");
  is(cell("progress"), "100%", "The done column");
  is(cell("down"), "\u2013", "A zero speed shows a dash");
  is(cell("up"), "1.5 MB/s", "The upload speed");
  is(cell("eta"), "\u2013", "A torrent that is not downloading has no ETA");
  is(cell("ratio"), "1.50", "The ratio");
  is(cell("seeds"), "\u2013", "An unknown seed count shows a dash");
  is(cell("peers"), "4", "The peer count");
  ok(cell("added"), "The added date is shown");
  await TestUtils.waitForCondition(
    () => bravo.querySelector("td.status").textContent == "Seeding",
    "The status is localized"
  );
  is(
    rowFor(doc, ALPHA).querySelector("td.eta").textContent,
    doc.defaultView.gM6ETorrentsPage.formatDuration(90),
    "A downloading torrent shows its ETA"
  );
  is(
    rowFor(doc, DELTA).querySelector("td.status").title,
    "synthetic failure",
    "An errored torrent shows its error as a tooltip"
  );

  const expected = {
    downloading: [ALPHA],
    seeding: [BRAVO],
    completed: [BRAVO, ECHO],
    paused: [CHARLIE],
    active: [ALPHA, BRAVO],
    inactive: [CHARLIE, ECHO],
    errored: [DELTA],
    all: [ALPHA, BRAVO, CHARLIE, DELTA, ECHO],
  };
  for (const [name, tids] of Object.entries(expected)) {
    const button = doc.querySelector(`button.filter[data-filter="${name}"]`);
    button.click();
    Assert.deepEqual(
      tableRows(doc).map(row => row.dataset.tid),
      tids,
      `The ${name} filter shows its torrents`
    );
    is(button.getAttribute("aria-current"), "true", "The filter is current");
    is(
      doc.querySelectorAll("button.filter[aria-current]").length,
      1,
      "Only one filter is current"
    );
  }

  Assert.deepEqual(
    [].concat(
      mock.callsTo("pause"),
      mock.callsTo("resume"),
      mock.callsTo("remove")
    ),
    [],
    "Looking at the page changes nothing"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_empty_no_match_and_disabled() {
  const mock = installPageMock();
  const { tab, doc } = await openTorrentsPage();
  const notice = doc.getElementById("m6e-torrents-notice");
  await TestUtils.waitForCondition(
    () => notice.getAttribute("data-l10n-id") == "m6e-torrents-empty",
    "An empty list shows the hint"
  );
  ok(BrowserTestUtils.isVisible(notice), "The hint is visible");
  is(tableRows(doc).length, 0, "There are no rows");
  for (const button of doc.querySelectorAll("#m6e-torrents-toolbar button")) {
    ok(button.disabled, `${button.id} is disabled with no selection`);
  }

  mock.notify([syntheticStatus({ tid: ALPHA, name: "synthetic-alpha" })]);
  await waitForRows(doc, 1);
  doc.querySelector('button.filter[data-filter="errored"]').click();
  await TestUtils.waitForCondition(
    () => notice.getAttribute("data-l10n-id") == "m6e-torrents-no-match",
    "A filter with no torrents says so"
  );
  BrowserTestUtils.removeTab(tab);

  await SpecialPowers.pushPrefEnv({ set: [["m6e.torrent.enabled", false]] });
  const before = mock.callsTo("list").length;
  const off = await openTorrentsPage();
  await TestUtils.waitForCondition(
    () =>
      off.doc
        .getElementById("m6e-torrents-notice")
        .getAttribute("data-l10n-id") == "m6e-torrents-disabled",
    "A page opened while BitTorrent is off says so"
  );
  is(mock.callsTo("list").length, before, "The service is not asked at all");
  BrowserTestUtils.removeTab(off.tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_sorting() {
  const mock = installPageMock();
  mock.torrents = fiveTorrents();
  const { tab, doc } = await openTorrentsPage();
  const header = key => doc.querySelector(`th[data-sort="${key}"]`);

  header("name").querySelector("button").click();
  is(header("name").getAttribute("aria-sort"), "ascending", "Sorted ascending");
  header("name").querySelector("button").click();
  is(
    header("name").getAttribute("aria-sort"),
    "descending",
    "Sorted descending"
  );
  Assert.deepEqual(
    rowNames(doc),
    [
      "synthetic-echo",
      "synthetic-delta",
      "synthetic-charlie",
      "synthetic-bravo",
      "synthetic-alpha",
    ],
    "Descending name order"
  );

  header("progress").querySelector("button").click();
  is(header("name").getAttribute("aria-sort"), "none", "Only one sort column");
  const names = rowNames(doc);
  is(names[0], "synthetic-delta", "The least done torrent comes first");

  header("eta").querySelector("button").click();
  is(
    rowNames(doc)[0],
    "synthetic-alpha",
    "A torrent with an ETA comes before those without"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_selection_and_keyboard() {
  const mock = installPageMock();
  mock.torrents = fiveTorrents();
  mock.details.set(ALPHA, syntheticDetail(mock.torrents[0]));
  const { tab, win, doc } = await openTorrentsPage();
  const selectedTids = () =>
    tableRows(doc)
      .filter(row => row.getAttribute("aria-selected") == "true")
      .map(row => row.dataset.tid);

  Assert.deepEqual(selectedTids(), [], "Nothing is selected at first");
  ok(
    BrowserTestUtils.isVisible(doc.getElementById("m6e-torrents-detail-hint")),
    "The detail pane asks for a selection"
  );
  is(
    tableRows(doc).filter(row => row.tabIndex == 0).length,
    1,
    "One row is the tab stop"
  );
  is(mock.callsTo("detail").length, 0, "No detail is requested unselected");

  rowFor(doc, ALPHA).click();
  Assert.deepEqual(selectedTids(), [ALPHA], "A click selects a row");
  is(win.location.hash, "#" + ALPHA, "The selection is in the URL");
  ok(
    BrowserTestUtils.isVisible(doc.getElementById("m6e-torrents-detail-body")),
    "The detail pane is shown"
  );
  await TestUtils.waitForCondition(
    () => mock.callsTo("detail").length,
    "The detail is requested"
  );

  rowFor(doc, ALPHA).focus();
  EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  Assert.deepEqual(selectedTids(), [BRAVO], "Arrow Down moves the selection");
  is(doc.activeElement, rowFor(doc, BRAVO), "The focus follows");
  EventUtils.synthesizeKey("KEY_End", {}, win);
  Assert.deepEqual(selectedTids(), [ECHO], "End selects the last row");
  EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  Assert.deepEqual(selectedTids(), [ECHO], "Arrow Down stops at the end");
  EventUtils.synthesizeKey("KEY_Home", {}, win);
  Assert.deepEqual(selectedTids(), [ALPHA], "Home selects the first row");
  EventUtils.synthesizeKey("KEY_ArrowUp", {}, win);
  Assert.deepEqual(selectedTids(), [ALPHA], "Arrow Up stops at the start");

  EventUtils.synthesizeKey(" ", {}, win);
  await TestUtils.waitForCondition(
    () => mock.callsTo("pause").length,
    "Space pauses a downloading torrent"
  );
  Assert.deepEqual(mock.callsTo("pause")[0].args, [ALPHA], "The right torrent");

  EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  EventUtils.synthesizeKey(" ", {}, win);
  await TestUtils.waitForCondition(
    () => mock.callsTo("resume").length,
    "Space resumes a paused torrent"
  );
  Assert.deepEqual(
    mock.callsTo("resume")[0].args,
    [CHARLIE],
    "The right torrent"
  );

  const dialog = doc.getElementById("m6e-torrents-remove-dialog");
  EventUtils.synthesizeKey("KEY_Delete", {}, win);
  ok(dialog.open, "Delete opens the remove choice");
  is(
    doc.activeElement.id,
    "m6e-torrents-remove-cancel",
    "The safe choice has the focus"
  );
  EventUtils.synthesizeKey("KEY_Escape", {}, win);
  ok(!dialog.open, "Escape closes it");
  is(mock.callsTo("remove").length, 0, "Nothing was removed");
  is(doc.activeElement, rowFor(doc, CHARLIE), "The focus returns to the row");

  EventUtils.synthesizeKey("KEY_Enter", {}, win);
  is(
    doc.activeElement.id,
    "m6e-torrents-tab-general",
    "Enter moves into the detail tabs"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_toolbar_actions() {
  const mock = installPageMock();
  mock.torrents = fiveTorrents();
  for (const t of mock.torrents) {
    mock.details.set(t.tid, syntheticDetail(t));
  }
  const { tab, doc, page } = await openTorrentsPage();
  const button = id => doc.getElementById(`m6e-torrents-${id}`);

  rowFor(doc, ALPHA).click();
  ok(!button("pause").disabled, "Pause is enabled for a downloading torrent");
  ok(button("resume").disabled, "Resume is disabled for it");
  rowFor(doc, CHARLIE).click();
  ok(button("pause").disabled, "Pause is disabled for a paused torrent");
  ok(!button("resume").disabled, "Resume is enabled for it");
  rowFor(doc, ECHO).click();
  ok(button("pause").disabled, "Pause is disabled for a completed torrent");
  ok(button("resume").disabled, "Resume is disabled for it");
  ok(!button("remove").disabled, "Remove is always possible");

  rowFor(doc, ALPHA).click();
  button("pause").click();
  await TestUtils.waitForCondition(
    () => mock.callsTo("pause").length == 1,
    "Pause calls the service"
  );
  Assert.deepEqual(mock.callsTo("pause")[0].args, [ALPHA], "Pause arguments");
  rowFor(doc, CHARLIE).click();
  button("resume").click();
  await TestUtils.waitForCondition(
    () => mock.callsTo("resume").length == 1,
    "Resume calls the service"
  );
  Assert.deepEqual(mock.callsTo("resume")[0].args, [CHARLIE], "Resume args");

  const dialog = doc.getElementById("m6e-torrents-remove-dialog");
  rowFor(doc, BRAVO).click();
  button("remove").click();
  ok(dialog.open, "Remove asks first");
  is(
    doc.getElementById("m6e-torrents-remove-name").textContent,
    "synthetic-bravo",
    "The dialog names the torrent"
  );
  button("remove-cancel").click();
  ok(!dialog.open, "Cancel closes the dialog");
  is(mock.callsTo("remove").length, 0, "Cancel removes nothing");

  button("remove").click();
  button("remove-keep").click();
  await TestUtils.waitForCondition(
    () => mock.callsTo("remove").length == 1,
    "Remove keeps the files"
  );
  Assert.deepEqual(
    mock.callsTo("remove")[0].args,
    [BRAVO, false],
    "Remove without deleting files"
  );

  rowFor(doc, ECHO).click();
  button("remove").click();
  button("remove-delete").click();
  await TestUtils.waitForCondition(
    () => mock.callsTo("remove").length == 2,
    "Remove and delete files calls the service"
  );
  Assert.deepEqual(
    mock.callsTo("remove")[1].args,
    [ECHO, true],
    "Remove with deleting files"
  );

  const revealed = [];
  page.reveal = torrent => revealed.push(torrent.tid);
  rowFor(doc, CHARLIE).click();
  button("open-folder").click();
  Assert.deepEqual(revealed, [CHARLIE], "Open folder reveals the torrent");

  await SimpleTest.promiseClipboardChange(
    `magnet:?xt=urn:btih:${CHARLIE}&dn=synthetic-charlie` +
      `&tr=${encodeURIComponent(SYNTHETIC_TRACKER)}`,
    () => button("copy-magnet").click()
  );
  await TestUtils.waitForCondition(
    () => doc.getElementById("m6e-torrents-info").textContent,
    "The page confirms the copy"
  );

  mock.actionError = new Error("synthetic failure");
  rowFor(doc, ALPHA).click();
  button("pause").click();
  const alert = doc.getElementById("m6e-torrents-alert");
  await TestUtils.waitForCondition(
    () => !alert.hidden,
    "A failed action is reported"
  );
  ok(alert.textContent.includes("synthetic failure"), "The reason is shown");
  BrowserTestUtils.removeTab(tab);
});
