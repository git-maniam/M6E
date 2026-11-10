/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: about:torrents detail tabs, live updates and untrusted text (spec
// 8.1), driven by a mock service.

const ONE = tidOf("1");
const TWO = tidOf("2");

function field(doc, key) {
  return doc.getElementById(`m6e-torrents-general-${key}`);
}

function tabButton(doc, name) {
  return doc.getElementById(`m6e-torrents-tab-${name}`);
}

function panelVisible(doc, name) {
  return !doc.getElementById(`m6e-torrents-panel-${name}`).hidden;
}

add_task(async function test_general_tab() {
  const mock = installPageMock();
  const status = syntheticStatus({
    tid: ONE,
    name: "synthetic-one",
    uploaded: 375,
    ratio: 0.5,
    sequential: true,
    isPrivate: true,
  });
  mock.torrents = [status];
  mock.details.set(ONE, syntheticDetail(status));
  const { tab, doc, page } = await openTorrentsPage();

  is(
    doc.getElementById("m6e-torrents-tabs").getAttribute("role"),
    "tablist",
    "The tabs form a tab list"
  );
  is(
    tabButton(doc, "general").getAttribute("aria-selected"),
    "true",
    "General is selected"
  );

  rowFor(doc, ONE).click();
  await TestUtils.waitForCondition(
    () => field(doc, "pieces").textContent.includes("3"),
    "The detail has arrived"
  );
  is(field(doc, "name").textContent, "synthetic-one", "The name");
  is(field(doc, "hash").textContent, ONE, "The hash");
  is(field(doc, "path").textContent, "/mock/downloads", "The save location");
  is(field(doc, "size").textContent, page.formatBytes(3000), "The size");
  is(field(doc, "downloaded").textContent, page.formatBytes(750), "Downloaded");
  is(field(doc, "uploaded").textContent, page.formatBytes(375), "Uploaded");
  is(field(doc, "ratio").textContent, "0.50", "The ratio");
  is(field(doc, "seeds").textContent, "1", "The seeds");
  is(field(doc, "peers").textContent, "2", "The peers");
  await TestUtils.waitForCondition(
    () => field(doc, "private").textContent == "Yes",
    "The private flag is localized"
  );
  await TestUtils.waitForCondition(
    () => field(doc, "sequential").textContent == "Yes",
    "The sequential flag is localized"
  );
  ok(
    field(doc, "pieces").textContent.includes(page.formatBytes(1000)),
    "The piece size is shown"
  );
  ok(field(doc, "error").hidden, "No error row without an error");
  ok(
    doc.getElementById("m6e-torrents-detail-unavailable").hidden,
    "No unavailable notice when the detail is there"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_files_trackers_peers_tabs() {
  const mock = installPageMock();
  const status = syntheticStatus({ tid: ONE, name: "synthetic-one" });
  mock.torrents = [status];
  mock.details.set(ONE, syntheticDetail(status));
  const { tab, win, doc } = await openTorrentsPage("#" + ONE);

  ok(panelVisible(doc, "general"), "General is shown first");
  tabButton(doc, "general").focus();
  EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
  is(doc.activeElement, tabButton(doc, "files"), "Arrow Right moves a tab");
  ok(panelVisible(doc, "files"), "The Files panel is shown");
  ok(!panelVisible(doc, "general"), "The General panel is hidden");
  is(tabButton(doc, "files").getAttribute("aria-selected"), "true", "Selected");
  is(tabButton(doc, "files").tabIndex, 0, "Roving tab stop");
  is(tabButton(doc, "general").tabIndex, -1, "Other tabs are skipped");

  const files = [...doc.querySelectorAll("#m6e-torrents-files tbody tr")];
  is(files.length, 2, "The padding file is hidden");
  is(files[0].cells[0].textContent, "docs/readme.txt", "File name");
  is(files[1].cells[0].textContent, "data/blob.bin", "File name");
  is(files[0].cells[2].querySelector("span").textContent, "100%", "Progress");
  is(files[1].cells[2].querySelector("span").textContent, "50%", "Progress");
  is(files[1].cells[2].querySelector("progress").value, 0.5, "Progress bar");
  await TestUtils.waitForCondition(
    () =>
      files[0].cells[3].textContent == "Normal" &&
      files[1].cells[3].textContent == "High",
    "The priorities are localized"
  );

  EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
  ok(panelVisible(doc, "trackers"), "The Trackers panel is shown");
  const tracker = doc.querySelector("#m6e-torrents-trackers tbody tr");
  is(tracker.cells[0].textContent, SYNTHETIC_TRACKER, "Tracker address");
  is(tracker.cells[2].textContent, "4", "Tracker seeds");
  is(tracker.cells[3].textContent, "7", "Tracker peers");
  await TestUtils.waitForCondition(
    () => tracker.cells[1].textContent == "Working",
    "The tracker status is localized"
  );

  EventUtils.synthesizeKey("KEY_End", {}, win);
  is(doc.activeElement, tabButton(doc, "peers"), "End goes to the last tab");
  const peer = doc.querySelector("#m6e-torrents-peers tbody tr");
  is(peer.cells[0].textContent, "192.0.2.10:6881", "Peer address");
  is(peer.cells[1].textContent, "synthetic-client", "Peer client");
  is(peer.cells[5].textContent, "50%", "Peer progress");
  await TestUtils.waitForCondition(
    () => peer.cells[2].textContent == "Encrypted",
    "The peer encryption is localized"
  );

  mock.details.get(ONE).trackers = [];
  mock.details.get(ONE).peers = [];
  EventUtils.synthesizeKey("KEY_ArrowLeft", {}, win);
  mock.notify(mock.torrents);
  await TestUtils.waitForCondition(
    () => !doc.getElementById("m6e-torrents-trackers-empty").hidden,
    "A torrent without trackers says so"
  );
  EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
  await TestUtils.waitForCondition(
    () => !doc.getElementById("m6e-torrents-peers-empty").hidden,
    "A torrent without peers says so"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_live_updates() {
  const mock = installPageMock();
  const one = syntheticStatus({ tid: ONE, name: "synthetic-one" });
  const two = syntheticStatus({ tid: TWO, name: "synthetic-two" });
  mock.torrents = [one, two];
  mock.details.set(ONE, syntheticDetail(one));
  mock.details.set(TWO, syntheticDetail(two));
  const { tab, doc } = await openTorrentsPage("#" + ONE);

  is(rowFor(doc, ONE).getAttribute("aria-selected"), "true", "Hash selects");
  const percentOf = tid =>
    rowFor(doc, tid).querySelector("td.progress .percent").textContent;
  is(percentOf(ONE), "25%", "The progress");

  const detailCalls = mock.callsTo("detail").length;
  const faster = { ...one, progress: 0.5, downKBps: 2500, downloaded: 1500 };
  mock.details.get(ONE).status = faster;
  mock.notify([faster, two]);
  await TestUtils.waitForCondition(
    () =>
      rowFor(doc, ONE).querySelector("td.progress .percent").textContent ==
      "50%",
    "A notification updates the progress"
  );
  is(
    rowFor(doc, ONE).querySelector("td.down").textContent,
    "2.5 MB/s",
    "The speed"
  );
  is(field(doc, "progress").textContent, "50%", "The detail follows");
  ok(
    mock.callsTo("detail").length > detailCalls,
    "The detail of the selection is refreshed"
  );

  const three = syntheticStatus({ tid: tidOf("3"), name: "synthetic-three" });
  mock.notify([faster, two, three]);
  await waitForRows(doc, 3);
  is(filterCount(doc, "all"), 3, "The counts follow");

  mock.notify([two, three]);
  await waitForRows(doc, 2);
  is(
    doc.querySelectorAll('#m6e-torrents-rows tr[aria-selected="true"]').length,
    0,
    "A removed torrent is not selected any more"
  );
  ok(
    BrowserTestUtils.isVisible(doc.getElementById("m6e-torrents-detail-hint")),
    "The detail pane asks for a selection again"
  );
  is(doc.defaultView.location.hash, "", "The selection left the URL");

  rowFor(doc, TWO).click();
  const failing = mock.callsTo("detail").length;
  mock.details.delete(TWO);
  mock.notify([two, three]);
  await TestUtils.waitForCondition(
    () => !doc.getElementById("m6e-torrents-detail-unavailable").hidden,
    "A failing detail call is reported"
  );
  ok(mock.callsTo("detail").length > failing, "The detail was asked again");
  is(field(doc, "name").textContent, "synthetic-two", "The list data stays");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_unknown_hash_selects_nothing() {
  const mock = installPageMock();
  mock.torrents = [syntheticStatus({ tid: ONE, name: "synthetic-one" })];
  const { tab, doc } = await openTorrentsPage("#" + TWO);
  is(
    doc.querySelectorAll('#m6e-torrents-rows tr[aria-selected="true"]').length,
    0,
    "A hash that is not in the list selects nothing"
  );
  is(mock.callsTo("detail").length, 0, "No detail is requested for it");
  BrowserTestUtils.removeTab(tab);

  const bad = await openTorrentsPage("#not-a-hash");
  is(
    bad.doc.querySelectorAll('#m6e-torrents-rows tr[aria-selected="true"]')
      .length,
    0,
    "A malformed hash selects nothing"
  );
  BrowserTestUtils.removeTab(bad.tab);
});

add_task(async function test_untrusted_text_is_inert() {
  const hostile = "<img src=x onerror=alert(1)>&amp;\u202Eexe.txt\u200B";
  const shown = "<img src=x onerror=alert(1)>&amp;<U+202E>exe.txt<U+200B>";
  const mock = installPageMock();
  const status = syntheticStatus({
    tid: ONE,
    name: hostile,
    savePath: "/mock/<b>folder",
    state: "error",
    error: { code: "io-error", message: "<i>bad</i>\u202E" },
  });
  mock.torrents = [status];
  const detail = syntheticDetail(status);
  detail.metadata.files[0].path = "<u>dir</u>/\u202Efile.txt";
  detail.trackers[0].url = "http://tracker.invalid/<b>x</b>\u202E";
  detail.trackers[0].message = "<i>message</i>";
  detail.peers[0].client = "<b>client</b>\u202E";
  mock.details.set(ONE, detail);
  const { tab, doc } = await openTorrentsPage("#" + ONE);

  await TestUtils.waitForCondition(
    () => field(doc, "error").textContent,
    "The detail is shown"
  );
  is(rowFor(doc, ONE).querySelector(".name").textContent, shown, "Row name");
  is(field(doc, "name").textContent, shown, "Detail name");
  is(field(doc, "path").textContent, "/mock/<b>folder", "Save location");
  is(
    field(doc, "error").textContent,
    "<i>bad</i><U+202E>",
    "The error message is escaped"
  );

  tabButton(doc, "files").click();
  is(
    doc.querySelector("#m6e-torrents-files tbody tr").cells[0].textContent,
    "<u>dir</u>/<U+202E>file.txt",
    "File names are escaped"
  );
  tabButton(doc, "trackers").click();
  const tracker = doc.querySelector("#m6e-torrents-trackers tbody tr");
  is(
    tracker.cells[0].textContent,
    "http://tracker.invalid/<b>x</b><U+202E>",
    "Tracker addresses are escaped"
  );
  is(tracker.cells[4].textContent, "<i>message</i>", "Tracker messages");
  tabButton(doc, "peers").click();
  is(
    doc.querySelector("#m6e-torrents-peers tbody tr").cells[1].textContent,
    "<b>client</b><U+202E>",
    "Peer clients are escaped"
  );

  doc.getElementById("m6e-torrents-remove").click();
  is(
    doc.getElementById("m6e-torrents-remove-name").textContent,
    shown,
    "The remove dialog shows the escaped name"
  );
  doc.getElementById("m6e-torrents-remove-cancel").click();

  for (const selector of ["img", "b", "i", "u"]) {
    is(
      doc.body.querySelectorAll(
        `#m6e-torrents-main ${selector}, dialog ${selector}`
      ).length,
      0,
      `No <${selector}> element came from torrent text`
    );
  }
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_magnet_link_drops_hostile_characters() {
  const mock = installPageMock();
  mock.torrents = [];
  const { tab, page } = await openTorrentsPage();
  is(
    page.buildMagnet(ONE, "a\u202Eb c&d", [
      SYNTHETIC_TRACKER,
      "javascript:alert(1)",
      "file:///etc/passwd",
    ]),
    `magnet:?xt=urn:btih:${ONE}&dn=ab%20c%26d&tr=${encodeURIComponent(
      SYNTHETIC_TRACKER
    )}`,
    "Invisible characters and non-tracker addresses are dropped"
  );
  BrowserTestUtils.removeTab(tab);
});
