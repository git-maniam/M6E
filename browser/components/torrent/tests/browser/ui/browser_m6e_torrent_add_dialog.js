/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: the Add torrent dialog (spec 8.1, 8.2, 8.3), driven by a mock service.

const FILE_SOURCE = { kind: "finder", origin: null };
const PAGE_SOURCE = { kind: "page", origin: "https://example.org" };

add_setup(async function () {
  await enableTorrents();
});

add_task(async function test_file_tree_and_confirm() {
  const { service, risk } = installMocks();
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  const doc = opened.doc;
  await waitForDetails(doc);

  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Opening the dialog adds and starts nothing"
  );
  is(
    doc.getElementById("m6e-torrent-add-name").textContent,
    "synthetic-sample",
    "The name is shown"
  );
  const names = fileRows(doc).map(
    row => row.querySelector(".name").textContent
  );
  Assert.deepEqual(
    names.sort(),
    ["blob.bin", "guide.txt", "readme.txt", "top.txt"],
    "Padding files are hidden"
  );
  ok(
    !visible(doc, "m6e-torrent-shady"),
    "No banner for a torrent the assessor calls ok"
  );
  ok(
    !doc.getElementById("m6e-torrent-add-confirm").disabled,
    "Add is available once a save folder is known"
  );
  is(risk.calls.length, 1, "The torrent was assessed once");
  Assert.deepEqual(
    risk.calls[0].source,
    FILE_SOURCE,
    "The recorded source reaches the assessor"
  );
  Assert.deepEqual(risk.calls[0].extra, [], "No extra trusted sites yet");

  doc.getElementById("m6e-torrent-add-start-now").checked = true;
  doc.getElementById("m6e-torrent-add-sequential").checked = true;
  doc.getElementById("m6e-torrent-add-confirm").click();
  is(await done, true, "addTorrentFile reports that the torrent was added");

  const confirms = service.callsTo("confirmAdd");
  is(confirms.length, 1, "confirmAdd ran once");
  const [tid, options] = confirms[0].args;
  is(tid, SYNTHETIC_HASH, "The pending torrent is confirmed");
  is(options.start, true, "Start now is passed on");
  is(options.sequential, true, "Sequential download is passed on");
  ok(options.savePath, "A save path is passed on");
  Assert.deepEqual(
    options.files.map(f => f.index).sort(),
    [0, 1, 2, 4],
    "Padding files are not part of the selection"
  );
  ok(
    options.files.every(f => f.priority == 2),
    "Files default to normal priority"
  );
  Assert.deepEqual(
    service.callsTo("cancelAdd"),
    [],
    "A confirmed torrent is not cancelled"
  );
});

add_task(async function test_selection_and_priority() {
  const { service } = installMocks();
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  const doc = opened.doc;
  await waitForDetails(doc);

  const rowFor = name =>
    fileRows(doc).find(r => r.querySelector(".name").textContent == name);

  rowFor("blob.bin").querySelector("input[type=checkbox]").click();
  rowFor("top.txt").querySelector("select").value = "3";
  rowFor("top.txt")
    .querySelector("select")
    .dispatchEvent(new doc.defaultView.Event("change", { bubbles: true }));
  rowFor("guide.txt").querySelector("select").value = "0";
  rowFor("guide.txt")
    .querySelector("select")
    .dispatchEvent(new doc.defaultView.Event("change", { bubbles: true }));
  ok(
    !rowFor("guide.txt").querySelector("input[type=checkbox]").checked,
    "Choosing Do not download unchecks the file"
  );

  const folderBoxes = [
    ...doc.querySelectorAll("#m6e-torrent-add-tree li.folder > .row input"),
  ];
  is(folderBoxes.length, 2, "Two folders are listed");
  const docsBox = folderBoxes.find(
    box => box.parentElement.querySelector(".name").textContent == "docs"
  );
  ok(docsBox.indeterminate, "A folder with some files chosen is indeterminate");

  doc.getElementById("m6e-torrent-add-confirm").click();
  await done;

  const [, options] = service.callsTo("confirmAdd")[0].args;
  const byIndex = Object.fromEntries(
    options.files.map(f => [f.index, f.priority])
  );
  Assert.deepEqual(
    byIndex,
    { 0: 2, 1: 0, 2: 0, 4: 3 },
    "Unchecked files are sent with priority 0 and the chosen priority is kept"
  );
});

add_task(async function test_cancel_adds_nothing() {
  const { service } = installMocks();
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  await closeAddDialog(opened);
  is(await done, false, "Cancel reports that nothing was added");
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing was confirmed");
  is(service.callsTo("cancelAdd").length, 1, "The pending torrent was dropped");
});

add_task(async function test_shady_banner() {
  const { service, risk } = installMocks();
  service.fileMetadata = syntheticMetadata({
    files: [
      { index: 0, path: "movie.mkv.exe", length: 100 },
      { index: 1, path: "other‮gpj.exe", length: 100 },
    ],
  });
  risk.result = {
    verdict: "shady",
    trusted: false,
    reasons: [
      {
        code: "windows-program",
        severity: "high",
        files: ["movie.mkv.exe", "other‮gpj.exe"],
      },
      {
        code: "a-code-this-build-does-not-know",
        severity: "medium",
        files: [],
      },
    ],
  };
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", PAGE_SOURCE);
  const opened = await waitForAddDialog();
  const doc = opened.doc;
  await waitForDetails(doc);

  ok(visible(doc, "m6e-torrent-shady"), "The Shady Torrent banner is shown");
  is(
    doc.getElementById("m6e-torrent-shady-title").textContent,
    "Shady Torrent",
    "The banner is headed Shady Torrent"
  );
  const reasons = [...doc.querySelectorAll("#m6e-torrent-shady-reasons li")];
  is(reasons.length, 2, "Every reason is listed in words");
  ok(
    reasons[0].textContent.includes("movie.mkv.exe"),
    "The reason names the files"
  );
  ok(
    reasons[0].textContent.includes("<U+202E>") &&
      !reasons[0].textContent.includes("‮"),
    "A direction override in a file name is shown as a visible escape"
  );
  ok(
    reasons[1]
      .querySelector("span")
      .getAttribute("data-l10n-id")
      .endsWith("-other"),
    "A reason code this build does not know falls back to the generic text"
  );
  ok(
    doc.getElementById("m6e-torrent-add-confirm").disabled,
    "The plain Add button is disabled while the banner is shown"
  );
  ok(
    doc.getElementById("m6e-torrent-add-footer").hidden,
    "The plain Add and Cancel buttons are hidden while the banner is shown"
  );

  doc.getElementById("m6e-torrent-add-form").requestSubmit?.();
  await settle();
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Pressing Enter in the form does not add a shady torrent"
  );

  doc.getElementById("m6e-torrent-shady-add-anyway").click();
  is(await done, true, "Add anyway adds the torrent");
  is(service.callsTo("confirmAdd").length, 1, "confirmAdd ran once");
  ok(
    gM6ETorrent._acknowledged.has(SYNTHETIC_HASH),
    "The choice is remembered for the session"
  );
  gM6ETorrent._acknowledged.clear();
});

add_task(async function test_shady_cancel_and_remembered_choice() {
  const { service, risk } = installMocks();
  risk.result = {
    verdict: "shady",
    trusted: false,
    reasons: [{ code: "piracy-tool-name", severity: "high", files: ["a.bin"] }],
  };
  let done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  let opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  opened.doc.getElementById("m6e-torrent-shady-cancel").click();
  await opened.closedPromise;
  is(await done, false, "Cancel on the banner adds nothing");
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing was confirmed");
  is(service.callsTo("cancelAdd").length, 1, "The torrent was dropped");

  gM6ETorrent._acknowledged.add(SYNTHETIC_HASH);
  done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  opened = await waitForAddDialog(opened);
  await waitForDetails(opened.doc);
  ok(
    !visible(opened.doc, "m6e-torrent-shady"),
    "A torrent already accepted this session shows no banner"
  );
  await closeAddDialog(opened);
  await done;
  gM6ETorrent._acknowledged.clear();
});

add_task(async function test_trusted_source_no_banner_and_trust_checkbox() {
  const { service, risk } = installMocks();
  risk.result = { verdict: "ok", trusted: true, reasons: [] };
  let done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", PAGE_SOURCE);
  let opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  ok(!visible(opened.doc, "m6e-torrent-shady"), "No banner for trusted");
  ok(
    opened.doc.getElementById("m6e-torrent-add-trust-label").hidden,
    "A trusted site is not offered for trust again"
  );
  await closeAddDialog(opened);
  await done;

  risk.result = { verdict: "ok", trusted: false, reasons: [] };
  done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", PAGE_SOURCE);
  opened = await waitForAddDialog(opened);
  await waitForDetails(opened.doc);
  const label = opened.doc.getElementById("m6e-torrent-add-trust-label");
  ok(!label.hidden, "An untrusted https site can be trusted");
  ok(
    label.textContent.includes("https://example.org"),
    "The label names the site"
  );
  opened.doc.getElementById("m6e-torrent-add-trust").click();
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  await done;
  Assert.deepEqual(
    gM6ETorrent.trustedSites,
    ["https://example.org"],
    "The site is stored in the pref after Add"
  );
  is(service.callsTo("confirmAdd").length, 1, "The torrent was added");
  Services.prefs.clearUserPref("m6e.torrent.trustedSites");
});

add_task(async function test_untrusted_origins_are_dropped() {
  const { service, risk } = installMocks();
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", {
    kind: "page",
    origin: "http://example.org",
  });
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  Assert.deepEqual(
    service.callsTo("addFile")[0].args[1],
    { kind: "page", origin: null },
    "A non-https origin is not recorded"
  );
  Assert.deepEqual(
    risk.calls[0].source,
    { kind: "page", origin: null },
    "The assessor never sees a non-https origin"
  );
  ok(
    opened.doc.getElementById("m6e-torrent-add-trust-label").hidden,
    "Such a site cannot be trusted"
  );
  await closeAddDialog(opened);
  await done;

  Assert.deepEqual(
    gM6ETorrent.sanitizeSource({
      kind: "bogus",
      origin: "https://a.example/x",
    }),
    { kind: "unknown", origin: null },
    "Unknown kinds and origins with a path are dropped"
  );
});

add_task(async function test_magnet_waits_for_metadata() {
  const { service } = installMocks();
  const done = gM6ETorrent.addMagnet(SYNTHETIC_MAGNET, {
    kind: "magnet-page",
    origin: "https://example.org",
  });
  const opened = await waitForAddDialog();
  const doc = opened.doc;

  ok(
    visible(doc, "m6e-torrent-add-fetching"),
    "The fetching state is shown at once"
  );
  ok(!visible(doc, "m6e-torrent-add-details"), "No file list yet");
  ok(
    doc.getElementById("m6e-torrent-add-confirm").disabled,
    "Add cannot be pressed before the metadata has arrived"
  );
  doc.getElementById("m6e-torrent-add-form").requestSubmit?.();
  await settle();
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Submitting the form before the metadata arrives adds nothing"
  );

  Services.obs.notifyObservers(
    null,
    "m6e-torrent-metadata",
    JSON.stringify({
      tid: "1".repeat(40),
      metadata: syntheticMetadata({ name: "other" }),
      source: {},
    })
  );
  await settle();
  ok(
    !visible(doc, "m6e-torrent-add-details"),
    "Metadata for a different torrent is ignored"
  );

  Services.obs.notifyObservers(
    null,
    "m6e-torrent-metadata",
    JSON.stringify({
      tid: SYNTHETIC_HASH,
      metadata: syntheticMetadata(),
      source: {},
    })
  );
  await waitForDetails(doc);
  ok(!visible(doc, "m6e-torrent-add-fetching"), "The fetching state is gone");
  is(fileRows(doc).length, 4, "The file list appears");
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Receiving metadata alone does not start the torrent"
  );

  await closeAddDialog(opened);
  is(await done, false, "Cancel adds nothing");
  is(service.callsTo("cancelAdd").length, 1, "The metadata fetch is dropped");
});

add_task(async function test_magnet_cancel_while_fetching() {
  const { service } = installMocks();
  const done = gM6ETorrent.addMagnet(SYNTHETIC_MAGNET);
  const opened = await waitForAddDialog();
  await closeAddDialog(opened);
  is(await done, false, "Cancel while fetching adds nothing");
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing was confirmed");
  is(service.callsTo("cancelAdd").length, 1, "The fetch is dropped");
});

add_task(async function test_magnet_error_while_fetching() {
  installMocks();
  const done = gM6ETorrent.addMagnet(SYNTHETIC_MAGNET);
  const opened = await waitForAddDialog();
  Services.obs.notifyObservers(
    null,
    "m6e-torrent-error",
    JSON.stringify({
      tid: SYNTHETIC_HASH,
      code: "engine-error",
      message: "synthetic failure",
    })
  );
  await TestUtils.waitForCondition(
    () => visible(opened.doc, "m6e-torrent-add-error"),
    "The error is shown"
  );
  await TestUtils.waitForCondition(
    () =>
      opened.doc
        .getElementById("m6e-torrent-add-error")
        .textContent.includes("synthetic failure"),
    "The error text is shown"
  );
  ok(
    opened.doc.getElementById("m6e-torrent-add-confirm").disabled,
    "Add stays disabled"
  );
  await closeAddDialog(opened);
  await done;
});

add_task(async function test_free_space_warning() {
  installMocks();
  const original = gM6ETorrent.getFreeSpace;
  gM6ETorrent.getFreeSpace = () => 10;
  registerCleanupFunction(() => {
    gM6ETorrent.getFreeSpace = original;
  });
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  ok(
    visible(opened.doc, "m6e-torrent-add-no-space"),
    "A warning appears when the files do not fit"
  );
  ok(
    !opened.doc.getElementById("m6e-torrent-add-confirm").disabled,
    "The warning does not block Add"
  );
  gM6ETorrent.getFreeSpace = () => 1e12;
  opened.doc.querySelector("#m6e-torrent-add-tree input").click();
  ok(
    !visible(opened.doc, "m6e-torrent-add-no-space"),
    "The warning goes away when there is room"
  );
  await closeAddDialog(opened);
  await done;
  gM6ETorrent.getFreeSpace = original;
});

add_task(async function test_confirm_error_keeps_dialog_open() {
  const { service } = installMocks();
  const error = new Error("synthetic disk problem");
  error.code = "disk-full";
  service.confirmError = error;
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  await TestUtils.waitForCondition(
    () => visible(opened.doc, "m6e-torrent-add-error"),
    "The error is shown in the dialog"
  );
  await TestUtils.waitForCondition(
    () =>
      opened.doc
        .getElementById("m6e-torrent-add-error")
        .textContent.includes("synthetic disk problem"),
    "The error text is shown"
  );
  ok(
    !opened.doc.getElementById("m6e-torrent-add-confirm").disabled,
    "The user can try again"
  );
  service.confirmError = null;
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  is(await done, true, "A second attempt adds the torrent");
});

add_task(async function test_dialog_is_labelled_and_focusable() {
  installMocks();
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  const doc = opened.doc;
  await waitForDetails(doc);
  for (const box of doc.querySelectorAll("#m6e-torrent-add-tree input")) {
    ok(box.getAttribute("aria-label"), "Every checkbox has an accessible name");
  }
  for (const select of doc.querySelectorAll("#m6e-torrent-add-tree select")) {
    ok(select.getAttribute("aria-label"), "Every priority has a name");
  }
  ok(
    doc.querySelector("label[for=m6e-torrent-add-save-path]"),
    "The save location field has a label"
  );
  ok(
    doc.getElementById("m6e-torrent-add-save-path").readOnly,
    "The save location is changed only through the folder picker"
  );
  await closeAddDialog(opened);
  await done;
});

const REAL_PICK_FOLDER = gM6ETorrent.pickFolder;

registerCleanupFunction(() => {
  gM6ETorrent.pickFolder = REAL_PICK_FOLDER;
});

function stubPickFolder(result) {
  const calls = [];
  gM6ETorrent.pickFolder = async startPath => {
    calls.push(startPath);
    return result;
  };
  return { calls };
}

function savePathOf(doc) {
  return doc.getElementById("m6e-torrent-add-save-path").value;
}

add_task(async function test_download_dir_pref_sets_the_save_folder() {
  const { service } = installMocks();
  await SpecialPowers.pushPrefEnv({
    set: [["m6e.torrent.downloadDir", "/mock/chosen-dir"]],
  });
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  is(
    savePathOf(opened.doc),
    "/mock/chosen-dir",
    "The configured folder is the starting save location"
  );
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  is(await done, true, "The torrent is added");
  is(
    service.callsTo("confirmAdd")[0].args[1].savePath,
    "/mock/chosen-dir",
    "and it is saved there"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_empty_or_relative_download_dir_uses_downloads() {
  installMocks();
  const { Downloads } = ChromeUtils.importESModule(
    "resource://gre/modules/Downloads.sys.mjs"
  );
  const expected = await Downloads.getPreferredDownloadsDirectory();
  ok(expected, "The browser has a download folder");
  for (const value of ["", "relative/folder"]) {
    await SpecialPowers.pushPrefEnv({
      set: [["m6e.torrent.downloadDir", value]],
    });
    const done = gM6ETorrent.addTorrentFile(
      "/mock/sample.torrent",
      FILE_SOURCE
    );
    const opened = await waitForAddDialog();
    await waitForDetails(opened.doc);
    is(
      savePathOf(opened.doc),
      expected,
      `"${value}" falls back to the browser's download folder`
    );
    await closeAddDialog(opened);
    await done;
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_start_now_pref_is_the_checkbox_default() {
  const { service } = installMocks();
  await SpecialPowers.pushPrefEnv({ set: [["m6e.torrent.startNow", false]] });
  let done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  let opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  ok(
    !opened.doc.getElementById("m6e-torrent-add-start-now").checked,
    "Start now is off when the setting is off"
  );
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  await done;
  is(
    service.callsTo("confirmAdd")[0].args[1].start,
    false,
    "The torrent is added paused"
  );
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({ set: [["m6e.torrent.startNow", true]] });
  done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  opened = await waitForAddDialog(opened);
  await waitForDetails(opened.doc);
  ok(
    opened.doc.getElementById("m6e-torrent-add-start-now").checked,
    "Start now is on when the setting is on"
  );
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  await done;
  is(
    service.callsTo("confirmAdd")[1].args[1].start,
    true,
    "The torrent is started"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_ask_where_opens_the_folder_chooser_first() {
  const { service } = installMocks();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["m6e.torrent.askWhereToSave", true],
      ["m6e.torrent.downloadDir", "/mock/default-dir"],
    ],
  });
  const picker = stubPickFolder("/mock/picked-dir");
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  await TestUtils.waitForCondition(
    () => savePathOf(opened.doc) == "/mock/picked-dir",
    "The folder chosen first becomes the save location"
  );
  Assert.deepEqual(
    picker.calls,
    ["/mock/default-dir"],
    "The chooser opened once, in the default folder"
  );
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Choosing a folder adds nothing"
  );
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  await done;
  is(
    service.callsTo("confirmAdd")[0].args[1].savePath,
    "/mock/picked-dir",
    "The torrent is saved in the chosen folder"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_ask_where_cancelled_keeps_the_default() {
  installMocks();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["m6e.torrent.askWhereToSave", true],
      ["m6e.torrent.downloadDir", "/mock/default-dir"],
    ],
  });
  const picker = stubPickFolder(null);
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  await TestUtils.waitForCondition(
    () => picker.calls.length == 1,
    "The chooser opened"
  );
  await settle();
  is(
    savePathOf(opened.doc),
    "/mock/default-dir",
    "Cancelling the chooser keeps the default folder"
  );
  ok(
    !opened.doc.getElementById("m6e-torrent-add-confirm").disabled,
    "and the dialog can still add"
  );
  await closeAddDialog(opened);
  await done;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_ask_where_off_does_not_open_the_chooser() {
  installMocks();
  await SpecialPowers.pushPrefEnv({
    set: [["m6e.torrent.askWhereToSave", false]],
  });
  const picker = stubPickFolder("/mock/picked-dir");
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  await settle();
  is(picker.calls.length, 0, "The chooser stays closed");
  await closeAddDialog(opened);
  await done;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_unsupported_setting_errors_are_ignored() {
  installMocks();
  const done = gM6ETorrent.addMagnet(SYNTHETIC_MAGNET);
  const opened = await waitForAddDialog();
  for (const tid of [undefined, SYNTHETIC_HASH]) {
    Services.obs.notifyObservers(
      null,
      "m6e-torrent-error",
      JSON.stringify({
        tid,
        code: "unsupported-setting",
        message: "the helper cannot apply seedGoal",
      })
    );
  }
  await settle();
  ok(
    !visible(opened.doc, "m6e-torrent-add-error"),
    "An unsupported-setting event does not show an error in the dialog"
  );
  ok(
    visible(opened.doc, "m6e-torrent-add-fetching"),
    "The dialog keeps waiting for the metadata"
  );
  await closeAddDialog(opened);
  await done;
});

const RLO = String.fromCodePoint(0x202e);

add_task(async function test_banner_shortens_names_and_counts_more() {
  const { risk } = installMocks();
  const long = "Folder/" + "x".repeat(300) + ".exe";
  const emoji = "\u{1F600}".repeat(150) + ".dmg";
  const files = [long, emoji, "a.exe", "b.exe", "c.exe", "d.exe", "e.exe"];
  risk.result = {
    verdict: "shady",
    trusted: false,
    reasons: [
      { code: "windows-program", severity: "high", files, moreFiles: 20 },
    ],
  };
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  const doc = opened.doc;
  await waitForDetails(doc);
  const item = doc.querySelector("#m6e-torrent-shady-reasons li");
  const text = item.textContent;
  ok(
    text.includes("and 22 more"),
    "The names past the first five and those the check left out are counted"
  );
  ok(
    text.includes("c.exe") && !text.includes("d.exe"),
    "Only the first five names are shown"
  );
  ok(
    text.includes("\u{2026}") && text.includes("xxx.exe"),
    "A long name is shortened in the middle and keeps its extension"
  );
  ok(text.includes("\u{1F600}.dmg"), "So is a long name of emoji");
  ok(!/[\u{D800}-\u{DFFF}]/u.test(text), "No character is cut in half");
  ok(
    item.title.includes(long) && item.title.includes(emoji),
    "The full names are the item's tooltip"
  );
  ok(
    !item.hasAttribute("data-l10n-id"),
    "on an untranslated element, so Fluent keeps it"
  );
  doc.getElementById("m6e-torrent-shady-cancel").click();
  await opened.closedPromise;
  await done;
});

add_task(async function test_trust_is_not_offered_when_it_would_be_unsafe() {
  const { risk } = installMocks();
  const fine = { verdict: "ok", trusted: false, reasons: [] };
  const shady = {
    verdict: "shady",
    trusted: false,
    reasons: [{ code: "windows-program", severity: "high", files: ["a.exe"] }],
  };
  const cases = [
    [shady, PAGE_SOURCE, "a shady torrent"],
    [fine, { kind: "page", origin: "https://co.uk" }, "a public suffix"],
    [fine, { kind: "page", origin: "https://github.io" }, "a hosting suffix"],
  ];
  let previous = null;
  for (const [result, source, what] of cases) {
    risk.result = result;
    const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", source);
    const opened = await waitForAddDialog(previous);
    await waitForDetails(opened.doc);
    ok(
      opened.doc.getElementById("m6e-torrent-add-trust-label").hidden,
      `"Always trust" is not offered for ${what}`
    );
    opened.doc
      .getElementById(
        result == shady ? "m6e-torrent-shady-cancel" : "m6e-torrent-add-cancel"
      )
      .click();
    await opened.closedPromise;
    await done;
    previous = opened;
  }
  is(gM6ETorrent.trustedSites.length, 0, "No site was trusted");
});

add_task(async function test_page_torrent_waits_for_a_focused_delay() {
  const { service } = installMocks();
  await SimpleTest.promiseFocus(window);
  await SpecialPowers.pushPrefEnv({
    set: [["security.dialog_enable_delay", 60000]],
  });
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", PAGE_SOURCE);
  const opened = await waitForAddDialog();
  const { doc, win } = opened;
  await waitForDetails(doc);
  const confirm = doc.getElementById("m6e-torrent-add-confirm");
  const sequential = doc.getElementById("m6e-torrent-add-sequential");
  await TestUtils.waitForCondition(
    () => doc.activeElement?.id == "m6e-torrent-add-cancel",
    "A torrent that a page started gets the focus on Cancel, not on Add"
  );
  ok(
    doc.getElementById("m6e-torrent-add-start-now").checked,
    "Start now is on, so a stray Add would also start it"
  );
  ok(confirm.disabled, "Add waits for the delay");

  sequential.focus();
  EventUtils.synthesizeKey("KEY_Enter", {}, win);
  confirm.click();
  doc.getElementById("m6e-torrent-add-form").requestSubmit();
  await settle();
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Enter, a click and a submit during the delay add nothing"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["security.dialog_enable_delay", 50]],
  });
  gURLBar.focus();
  await TestUtils.waitForCondition(
    () => !doc.hasFocus(),
    "The dialog has lost the focus"
  );
  ok(confirm.disabled, "Add stays disabled without the focus");
  sequential.focus();
  await TestUtils.waitForCondition(
    () => !confirm.disabled,
    "Add works once the dialog has had the focus for the delay"
  );
  gURLBar.focus();
  await TestUtils.waitForCondition(
    () => confirm.disabled,
    "Losing the focus disables Add again"
  );
  sequential.focus();
  await TestUtils.waitForCondition(
    () => !confirm.disabled,
    "and the delay starts again when the focus comes back"
  );
  confirm.click();
  is(await done, true, "Add then adds the torrent");
  is(service.callsTo("confirmAdd").length, 1, "once");
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_shady_page_torrent_waits_for_a_focused_delay() {
  const { service, risk } = installMocks();
  await SimpleTest.promiseFocus(window);
  await SpecialPowers.pushPrefEnv({
    set: [["security.dialog_enable_delay", 60000]],
  });
  risk.result = {
    verdict: "shady",
    trusted: false,
    reasons: [{ code: "windows-program", severity: "high", files: ["a.exe"] }],
  };
  const done = gM6ETorrent.addMagnet(SYNTHETIC_MAGNET, {
    kind: "magnet-page",
    origin: "https://example.org",
  });
  const opened = await waitForAddDialog();
  const { doc } = opened;
  Services.obs.notifyObservers(
    null,
    "m6e-torrent-metadata",
    JSON.stringify({ tid: SYNTHETIC_HASH, metadata: syntheticMetadata() })
  );
  await waitForDetails(doc);
  const anyway = doc.getElementById("m6e-torrent-shady-add-anyway");
  await TestUtils.waitForCondition(
    () => doc.activeElement?.id == "m6e-torrent-shady-title",
    "The banner's heading has the focus"
  );
  ok(anyway.disabled, "Add anyway waits for the delay");
  anyway.click();
  await settle();
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing is added");

  await SpecialPowers.pushPrefEnv({
    set: [["security.dialog_enable_delay", 50]],
  });
  gURLBar.focus();
  await TestUtils.waitForCondition(() => !doc.hasFocus(), "Focus left");
  doc.getElementById("m6e-torrent-shady-title").focus();
  await TestUtils.waitForCondition(
    () => !anyway.disabled,
    "Add anyway works after the delay with the focus"
  );
  anyway.click();
  is(await done, true, "Add anyway adds the torrent");
  gM6ETorrent._acknowledged.clear();
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_own_torrent_focuses_add() {
  installMocks();
  await SimpleTest.promiseFocus(window);
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  await TestUtils.waitForCondition(
    () => opened.doc.activeElement?.id == "m6e-torrent-add-confirm",
    "A torrent the user opened from Finder gets the focus on Add"
  );
  await closeAddDialog(opened);
  await done;
});

add_task(async function test_torrent_text_in_messages_is_not_markup() {
  installMocks();
  const hostile = "cannot write Evil&#x202E;gpj.exe <b>bold</b> " + RLO + "x";
  const done = gM6ETorrent.addMagnet(SYNTHETIC_MAGNET);
  const opened = await waitForAddDialog();
  Services.obs.notifyObservers(
    null,
    "m6e-torrent-error",
    JSON.stringify({ tid: SYNTHETIC_HASH, code: "io-error", message: hostile })
  );
  const error = opened.doc.getElementById("m6e-torrent-add-error");
  await TestUtils.waitForCondition(
    () => error.textContent.includes("Evil"),
    "The error is shown"
  );
  ok(
    error.textContent.includes("Evil&#x202E;gpj.exe"),
    "A character reference in torrent text is shown as written"
  );
  ok(error.textContent.includes("<b>bold</b>"), "and so is markup");
  is(error.querySelector("b"), null, "which does not become an element");
  ok(
    !error.textContent.includes(RLO) && error.textContent.includes("<U+202E>"),
    "A real direction override is shown as a visible escape"
  );
  await closeAddDialog(opened);
  await done;
});

add_task(async function test_add_error_text_is_not_markup() {
  const { service } = installMocks();
  service.confirmError = new Error("disk problem in <b>Evil</b>&#x202E;.exe");
  const done = gM6ETorrent.addTorrentFile("/mock/sample.torrent", FILE_SOURCE);
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  const error = opened.doc.getElementById("m6e-torrent-add-error");
  await TestUtils.waitForCondition(
    () => error.textContent.includes("disk problem"),
    "The error is shown"
  );
  ok(
    error.textContent.includes("<b>Evil</b>&#x202E;.exe"),
    "The helper's message is shown as written"
  );
  is(error.querySelector("b"), null, "without markup");
  await closeAddDialog(opened);
  await done;
});
