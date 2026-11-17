/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: the BT button and panel (spec 8.1), driven by a mock service.

function syntheticStatus(overrides = {}) {
  return {
    tid: SYNTHETIC_HASH,
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
    addedAt: 0,
    error: null,
    ...overrides,
  };
}

async function openPanel() {
  const panel = document.getElementById("m6e-torrent-panel");
  const shown = BrowserTestUtils.waitForEvent(panel, "popupshown");
  gM6ETorrent.openPanel();
  await shown;
  return panel;
}

async function closePanel(panel) {
  const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  panel.hidePopup();
  await hidden;
}

add_setup(async function () {
  await enableTorrents();
});

add_task(async function test_button_toggles_panel() {
  const { service } = installMocks();
  const button = document.getElementById("m6e-torrent-button");
  ok(button, "The BT button exists");
  is(button.getAttribute("role"), "button", "The button has a role");
  ok(button.getAttribute("aria-label"), "The button has an accessible name");

  const panel = document.getElementById("m6e-torrent-panel");
  const shown = BrowserTestUtils.waitForEvent(panel, "popupshown");
  gM6ETorrent.handleButtonEvent(
    new KeyboardEvent("keypress", {
      key: "Enter",
      keyCode: KeyEvent.DOM_VK_RETURN,
      charCode: 0,
    })
  );
  await shown;
  is(button.getAttribute("aria-expanded"), "true", "The button reports open");
  ok(
    BrowserTestUtils.isVisible(document.getElementById("m6e-torrent-empty")),
    "An empty list shows the hint"
  );
  ok(
    document.getElementById("m6e-torrent-list").hidden,
    "The list is hidden when empty"
  );
  Assert.deepEqual(
    service.callsTo("confirmAdd").concat(service.callsTo("addFile")),
    [],
    "Opening the panel adds nothing"
  );

  const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  gM6ETorrent.handleButtonEvent(
    new KeyboardEvent("keypress", {
      key: " ",
      charCode: KeyEvent.DOM_VK_SPACE,
      keyCode: 0,
    })
  );
  await hidden;
  is(
    button.getAttribute("aria-expanded"),
    "false",
    "The button reports closed"
  );

  gM6ETorrent.handleButtonEvent(
    new KeyboardEvent("keypress", { key: "a", charCode: 97, keyCode: 0 })
  );
  is(panel.state, "closed", "Other keys do not open the panel");
});

add_task(async function test_rows_and_button_state() {
  const { service } = installMocks();
  const button = document.getElementById("m6e-torrent-button");
  service.torrents = [syntheticStatus()];
  const panel = await openPanel();
  await TestUtils.waitForCondition(
    () => document.querySelectorAll("#m6e-torrent-list li").length == 1,
    "The torrent is listed"
  );
  const row = document.querySelector("#m6e-torrent-list li");
  is(
    row.querySelector(".row-name").textContent,
    "synthetic-sample",
    "The row shows the name"
  );
  is(button.getAttribute("state"), "active", "The button is active");
  is(
    document.getElementById("m6e-torrent-badge").getAttribute("value"),
    "1",
    "The badge counts running torrents"
  );
  is(row.querySelector(".row-progress").value, 0.25, "Progress is shown");
  ok(
    row.querySelector(".row-progress").getAttribute("data-l10n-args"),
    "The progress bar is named for its torrent"
  );

  gM6ETorrent.setTorrents([
    syntheticStatus({ state: "pending-confirm" }),
    syntheticStatus({
      tid: "1".repeat(40),
      name: "second",
      state: "error",
      error: { code: "io-error", message: "synthetic error" },
    }),
  ]);
  is(
    document.querySelectorAll("#m6e-torrent-list li").length,
    1,
    "A torrent waiting in the Add dialog is not listed"
  );
  is(button.getAttribute("state"), "attention", "An error sets attention");
  ok(
    !document.getElementById("m6e-torrent-error-banner").hidden,
    "The error banner is shown"
  );

  gM6ETorrent.setTorrents([]);
  is(button.getAttribute("state"), "idle", "The button is idle again");
  await closePanel(panel);
});

add_task(async function test_row_actions() {
  const { service } = installMocks();
  service.torrents = [syntheticStatus()];
  const panel = await openPanel();
  await TestUtils.waitForCondition(
    () => document.querySelector("#m6e-torrent-list li"),
    "The torrent is listed"
  );
  const row = () => document.querySelector("#m6e-torrent-list li");

  row().querySelector(".row-toggle").click();
  await TestUtils.waitForCondition(
    () => service.callsTo("pause").length == 1,
    "Pause reaches the service"
  );
  is(service.callsTo("pause")[0].args[0], SYNTHETIC_HASH, "For this torrent");

  const choices = row().querySelector(".row-remove-choices");
  ok(choices.hidden, "The remove choices start hidden");
  row().querySelector(".row-remove").click();
  ok(!choices.hidden, "Remove asks first");
  Assert.deepEqual(service.callsTo("remove"), [], "Nothing is removed yet");
  row().querySelector(".row-remove-cancel").click();
  ok(choices.hidden, "Cancel hides the choices");
  Assert.deepEqual(service.callsTo("remove"), [], "Nothing was removed");

  row().querySelector(".row-remove").click();
  row().querySelector(".row-remove-delete").click();
  await TestUtils.waitForCondition(
    () => service.callsTo("remove").length == 1,
    "Remove reaches the service"
  );
  Assert.deepEqual(
    service.callsTo("remove")[0].args,
    [SYNTHETIC_HASH, true],
    "Remove and delete files asks the service to delete the files"
  );
  await closePanel(panel);
});

add_task(async function test_magnet_field() {
  const { service } = installMocks();
  const panel = await openPanel();
  const input = document.getElementById("m6e-torrent-magnet-input");
  const error = document.getElementById("m6e-torrent-magnet-error");

  document.getElementById("m6e-torrent-add-magnet").click();
  ok(
    !document.getElementById("m6e-torrent-magnet-row").hidden,
    "The paste field opens"
  );
  is(
    document
      .getElementById("m6e-torrent-add-magnet")
      .getAttribute("aria-expanded"),
    "true",
    "The toggle reports expanded"
  );
  is(input.getAttribute("aria-describedby"), error.id, "Errors are linked");

  input.value = "https://example.com/not-a-magnet";
  document.getElementById("m6e-torrent-magnet-submit").click();
  ok(!error.hidden, "An invalid link shows an error");
  is(input.getAttribute("aria-invalid"), "true", "The field is marked invalid");
  Assert.deepEqual(service.callsTo("addMagnet"), [], "Nothing was added");

  input.value = SYNTHETIC_MAGNET;
  const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  document.getElementById("m6e-torrent-magnet-submit").click();
  await hidden;
  const opened = await waitForAddDialog();
  is(service.callsTo("addMagnet").length, 1, "The link reaches the service");
  Assert.deepEqual(
    service.callsTo("addMagnet")[0].args,
    [SYNTHETIC_MAGNET, { kind: "unknown", origin: null }],
    "A pasted link has the unknown source"
  );
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "The pasted link waits for the Add dialog"
  );
  await closeAddDialog(opened);
});

add_task(async function test_add_file_button_opens_the_dialog_only() {
  const { service } = installMocks();
  const original = gM6ETorrent.pickTorrentFile;
  gM6ETorrent.pickTorrentFile = async () => "/mock/picked.torrent";
  registerCleanupFunction(() => {
    gM6ETorrent.pickTorrentFile = original;
  });
  const panel = await openPanel();
  document.getElementById("m6e-torrent-add-file").click();
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  Assert.deepEqual(
    service.callsTo("addFile")[0].args,
    ["/mock/picked.torrent", { kind: "unknown", origin: null }],
    "The picked file is read with the unknown source"
  );
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing is started");
  is(panel.state, "closed", "The panel closed for the dialog");
  await closeAddDialog(opened);
  gM6ETorrent.pickTorrentFile = original;
});

add_task(async function test_duplicate_shows_existing_row() {
  const { service } = installMocks();
  const error = new Error("already added");
  error.code = "duplicate";
  error.tid = SYNTHETIC_HASH;
  service.addError = error;
  service.torrents = [syntheticStatus()];
  const result = await gM6ETorrent.addMagnet(SYNTHETIC_MAGNET);
  is(result, false, "A duplicate is not added again");
  const panel = document.getElementById("m6e-torrent-panel");
  await TestUtils.waitForCondition(
    () => panel.state == "open",
    "The panel opens"
  );
  await TestUtils.waitForCondition(
    () => document.querySelector("#m6e-torrent-list li[highlight]"),
    "The existing torrent's row is marked"
  );
  is(gM6ETorrent.lastAddDialog, null, "No Add dialog was opened");
  await closePanel(panel);
});

add_task(async function test_disabled_pref_blocks_adding() {
  const { service } = installMocks();
  await SpecialPowers.pushPrefEnv({ set: [["m6e.torrent.enabled", false]] });
  is(
    await gM6ETorrent.addMagnet(SYNTHETIC_MAGNET),
    false,
    "Nothing is added while BitTorrent is off"
  );
  is(
    await gM6ETorrent.addTorrentFile("/mock/sample.torrent"),
    false,
    "Nothing is opened while BitTorrent is off"
  );
  Assert.deepEqual(service.callsTo("addMagnet"), [], "No magnet request");
  Assert.deepEqual(service.callsTo("addFile"), [], "No file request");
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_queued_limit_and_goal_indicators() {
  const { service } = installMocks();
  service.torrents = [
    syntheticStatus({
      state: "queued",
      downKBps: 0,
      downLimitKBps: 100,
      upLimitKBps: 0,
    }),
    syntheticStatus({
      tid: "2".repeat(40),
      name: "second",
      state: "completed",
      progress: 1,
      goalReached: true,
      upLimitKBps: 20,
    }),
  ];
  const panel = await openPanel();
  await TestUtils.waitForCondition(
    () => document.querySelectorAll("#m6e-torrent-list li").length == 2,
    "Both torrents are listed"
  );
  const [queued, done] = document.querySelectorAll("#m6e-torrent-list li");
  await TestUtils.waitForCondition(
    () => queued.querySelector(".row-state").textContent == "Queued",
    "The queued state is named"
  );
  is(queued.dataset.state, "queued", "The row carries the state");
  ok(!queued.querySelector(".row-limit-down").hidden, "Download limit shown");
  ok(queued.querySelector(".row-limit-up").hidden, "No upload limit shown");
  ok(queued.querySelector(".row-goal").hidden, "No goal marker");
  await TestUtils.waitForCondition(
    () => queued.querySelector(".row-limit-down").textContent.includes("100"),
    "The limit is written out"
  );
  ok(!done.querySelector(".row-goal").hidden, "The goal marker shows");
  ok(!done.querySelector(".row-limit-up").hidden, "Upload limit shown");
  ok(done.querySelector(".row-limit-down").hidden, "No download limit shown");

  gM6ETorrent.setTorrents([syntheticStatus()]);
  const row = document.querySelector("#m6e-torrent-list li");
  ok(row.querySelector(".row-limit-down").hidden, "No limit, no indicator");
  ok(row.querySelector(".row-limit-up").hidden, "No limit, no indicator");
  await closePanel(panel);
});

add_task(async function test_seed_action_choice() {
  installMocks();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["m6e.torrent.seedAction", "stop"],
      ["m6e.torrent.seedRatioPercent", 100],
      ["m6e.torrent.seedHours", 0],
    ],
  });
  const panel = await openPanel();
  const action = document.getElementById("m6e-torrent-seed-action");
  ok(!action.hidden, "The action shows with a goal");
  is(action.value, "stop", "Stop is the default");
  is(action.options.length, 2, "Stop or slow");

  action.value = "slow";
  action.dispatchEvent(new Event("change", { bubbles: true }));
  is(
    Services.prefs.getStringPref("m6e.torrent.seedAction"),
    "slow",
    "The choice is saved"
  );
  action.value = "stop";
  action.dispatchEvent(new Event("change", { bubbles: true }));
  is(
    Services.prefs.getStringPref("m6e.torrent.seedAction"),
    "stop",
    "The choice can be undone"
  );

  const goal = document.getElementById("m6e-torrent-seed-goal");
  goal.value = "never";
  goal.dispatchEvent(new Event("change", { bubbles: true }));
  ok(action.hidden, "Without a goal there is no action");
  await closePanel(panel);
});

add_task(async function test_interface_down_notice() {
  installMocks();
  const banner = document.getElementById("m6e-torrent-network-banner");
  const panel = await openPanel();
  ok(banner.hidden, "No notice while the interface is there");

  const notify = interfaceDown =>
    Services.obs.notifyObservers(
      null,
      "m6e-torrent-network",
      JSON.stringify({ listening: null, interfaceDown })
    );
  notify("utun9");
  ok(!banner.hidden, "The notice shows when the interface is down");
  is(
    JSON.parse(banner.getAttribute("data-l10n-args")).interface,
    "utun9",
    "A plain interface name goes in"
  );
  await TestUtils.waitForCondition(
    () => banner.textContent.includes("utun9"),
    "The notice names the interface"
  );

  notify("<b>x y</b>");
  is(
    JSON.parse(banner.getAttribute("data-l10n-args")).interface,
    "",
    "A name that is not plain is dropped"
  );

  notify(null);
  ok(banner.hidden, "The notice goes when the interface returns");
  await closePanel(panel);
});

add_task(async function test_private_source_flag_is_kept() {
  installMocks();
  Assert.deepEqual(
    gM6ETorrent.sanitizeSource({
      kind: "page",
      origin: "https://example.com",
      private: true,
    }),
    { kind: "page", origin: "https://example.com", private: true },
    "A private source keeps its flag"
  );
  Assert.deepEqual(
    gM6ETorrent.sanitizeSource({
      kind: "page",
      origin: "https://example.com",
      private: "yes",
    }),
    { kind: "page", origin: "https://example.com" },
    "Only a true private flag is kept"
  );
});

add_task(async function test_dht_forced_off_notice() {
  installMocks();
  const banner = document.getElementById("m6e-torrent-forced-off-banner");
  const panel = await openPanel();
  ok(banner.hidden, "No notice when nothing is held off");
  const notify = forcedOff =>
    Services.obs.notifyObservers(
      null,
      "m6e-torrent-network",
      JSON.stringify({ listening: null, interfaceDown: null, forcedOff })
    );
  notify(["dht", "utp"]);
  ok(!banner.hidden, "The panel says DHT is off");
  await TestUtils.waitForCondition(
    () => banner.textContent.includes("DHT is off"),
    "The notice is worded"
  );
  notify(["utp"]);
  ok(banner.hidden, "Other held-off features show no DHT notice");
  notify([]);
  ok(banner.hidden, "The notice goes with the proxy");
  await closePanel(panel);
});

add_task(async function test_torrent_text_in_rows_is_not_markup() {
  const { service } = installMocks();
  const name = "Evil&#x202E;gpj.exe <b>bold</b>";
  const message = "cannot write " + name;
  service.torrents = [
    syntheticStatus({
      name,
      state: "error",
      error: { code: "io-error", message },
    }),
  ];
  const panel = await openPanel();
  await TestUtils.waitForCondition(
    () => document.querySelector("#m6e-torrent-list li"),
    "The torrent is listed"
  );
  const row = document.querySelector("#m6e-torrent-list li");
  const question = row.querySelector(".row-remove-question");
  const error = row.querySelector(".row-error");
  await TestUtils.waitForCondition(
    () =>
      question.textContent.includes("Evil") &&
      error.textContent.includes("Evil"),
    "The remove question and the error are translated"
  );
  for (const [element, what] of [
    [question, "remove question"],
    [error, "error"],
  ]) {
    ok(
      element.textContent.includes(name),
      `The ${what} shows the torrent's text as written`
    );
    is(element.querySelector("b"), null, `The ${what} gets no element from it`);
    ok(
      !element.textContent.includes(String.fromCodePoint(0x202e)),
      `The ${what} gets no direction override from it`
    );
  }
  is(
    row.querySelector(".row-remove-choices").title,
    name,
    "The full name is a tooltip on the untranslated parent"
  );
  is(
    row.querySelector(".row-details").title,
    message,
    "and so is the full error"
  );
  await closePanel(panel);
});
