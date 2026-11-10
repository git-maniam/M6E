/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: opening magnet links and .torrent files (spec 8.2). A web page can
// only open the Add dialog, never add or start anything; the mock service
// proves that confirmAdd is never reached without the user.

const { M6ETorrentHandler } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentHandler.sys.mjs"
);

const HTTPS_ORIGIN = "https://example.com";

function pageOpenReset() {
  M6ETorrentHandler._lastPageOpen = new WeakMap();
}

function principalFor(origin) {
  return Services.scriptSecurityManager.createContentPrincipalFromOrigin(
    origin
  );
}

function magnetURI() {
  return Services.io.newURI(SYNTHETIC_MAGNET);
}

function fakeLauncher(
  spec,
  { type = "application/x-bittorrent", name = "" } = {}
) {
  return {
    source: Services.io.newURI(spec),
    MIMEInfo: { MIMEType: type },
    suggestedFileName: name,
    browsingContextId: gBrowser.selectedBrowser.browsingContext.id,
    cancelled: false,
    cancel() {
      this.cancelled = true;
    },
  };
}

async function closeAndSettle(opened) {
  await closeAddDialog(opened);
  await TestUtils.waitForCondition(
    () => !M6ETorrentHandler._busy.has(window),
    "The window accepts another page request"
  );
}

add_setup(async function () {
  await enableTorrents();
  await SimpleTest.promiseFocus(window);
});

add_task(async function test_magnet_without_user_activation_is_ignored() {
  const { service } = installMocks();
  pageOpenReset();
  const handled = M6ETorrentHandler.handleMagnet(
    magnetURI(),
    principalFor(HTTPS_ORIGIN),
    gBrowser.selectedBrowser.browsingContext,
    false,
    false
  );
  ok(handled, "The navigation is consumed, so no external app prompt appears");
  await settle();
  Assert.deepEqual(
    service.calls.filter(c => c.name != "init"),
    [],
    "A page without a user gesture reaches nothing"
  );
  is(gM6ETorrent.lastAddDialog, null, "No dialog opened");
  is(torrentsTabs().length, 0, "The torrents page is not opened either");
});

add_task(async function test_magnet_from_page_records_source() {
  const { service } = installMocks();
  pageOpenReset();
  ok(
    M6ETorrentHandler.handleMagnet(
      magnetURI(),
      principalFor(HTTPS_ORIGIN),
      gBrowser.selectedBrowser.browsingContext,
      false,
      true
    ),
    "The navigation is handled"
  );
  const opened = await waitForAddDialog();
  Assert.deepEqual(
    service.callsTo("addMagnet")[0].args,
    [SYNTHETIC_MAGNET, { kind: "magnet-page", origin: HTTPS_ORIGIN }],
    "The page's https origin is recorded as the source"
  );
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "A page cannot confirm: only the Add dialog does"
  );
  is(opened.params.source.origin, HTTPS_ORIGIN, "The dialog holds the source");
  await closeAndSettle(opened);
  is(service.callsTo("cancelAdd").length, 1, "Cancel drops the request");
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Still nothing added");
});

add_task(async function test_magnet_from_http_page_has_no_origin() {
  const { service } = installMocks();
  pageOpenReset();
  M6ETorrentHandler.handleMagnet(
    magnetURI(),
    principalFor("http://example.com"),
    gBrowser.selectedBrowser.browsingContext,
    false,
    true
  );
  const opened = await waitForAddDialog();
  Assert.deepEqual(
    service.callsTo("addMagnet")[0].args[1],
    { kind: "magnet-page", origin: null },
    "A non-https page is never recorded as an origin"
  );
  await closeAndSettle(opened);
});

add_task(async function test_second_page_request_is_blocked() {
  const { service } = installMocks();
  pageOpenReset();
  const bc = gBrowser.selectedBrowser.browsingContext;
  M6ETorrentHandler.handleMagnet(
    magnetURI(),
    principalFor(HTTPS_ORIGIN),
    bc,
    false,
    true
  );
  const opened = await waitForAddDialog();
  M6ETorrentHandler.handleMagnet(
    magnetURI(),
    principalFor(HTTPS_ORIGIN),
    bc,
    false,
    true
  );
  await settle();
  is(
    service.callsTo("addMagnet").length,
    1,
    "A page cannot stack Add dialogs while one is open"
  );
  await closeAndSettle(opened);

  M6ETorrentHandler.handleMagnet(
    magnetURI(),
    principalFor(HTTPS_ORIGIN),
    bc,
    false,
    true
  );
  await settle();
  is(
    service.callsTo("addMagnet").length,
    1,
    "A page cannot reopen the dialog straight after it closed"
  );
});

add_task(async function test_magnet_from_other_app_is_unknown_source() {
  const { service } = installMocks();
  pageOpenReset();
  M6ETorrentHandler.handleMagnet(
    magnetURI(),
    Services.scriptSecurityManager.getSystemPrincipal(),
    gBrowser.selectedBrowser.browsingContext,
    true,
    false
  );
  const opened = await waitForAddDialog();
  Assert.deepEqual(
    service.callsTo("addMagnet")[0].args[1],
    { kind: "unknown", origin: null },
    "A link from another application has the unknown source"
  );
  await closeAndSettle(opened);
});

add_task(async function test_other_schemes_and_disabled_pref() {
  const { service } = installMocks();
  pageOpenReset();
  ok(
    !M6ETorrentHandler.handleMagnet(
      Services.io.newURI("mailto:someone@example.com"),
      principalFor(HTTPS_ORIGIN),
      gBrowser.selectedBrowser.browsingContext,
      false,
      true
    ),
    "Only magnet links are handled"
  );
  await SpecialPowers.pushPrefEnv({ set: [["m6e.torrent.enabled", false]] });
  ok(
    !M6ETorrentHandler.handleMagnet(
      magnetURI(),
      principalFor(HTTPS_ORIGIN),
      gBrowser.selectedBrowser.browsingContext,
      false,
      true
    ),
    "With BitTorrent off the navigation is left alone"
  );
  ok(
    !M6ETorrentHandler.handleCommandLineURI(magnetURI(), false),
    "With BitTorrent off the command line is left alone"
  );
  await SpecialPowers.popPrefEnv();
  Assert.deepEqual(
    service.calls.filter(c => c.name != "init"),
    [],
    "Nothing reached the service"
  );
});

add_task(async function test_command_line_uris() {
  const { service } = installMocks();
  pageOpenReset();

  ok(
    !M6ETorrentHandler.handleCommandLineURI(
      Services.io.newURI("https://example.com/"),
      false
    ),
    "A web address is not a torrent"
  );
  ok(
    !M6ETorrentHandler.handleCommandLineURI(
      Services.io.newURI("file:///mock/readme.txt"),
      false
    ),
    "A file that is not a .torrent is not handled"
  );

  ok(
    M6ETorrentHandler.handleCommandLineURI(magnetURI(), false),
    "A magnet link from the command line is handled"
  );
  let opened = await waitForAddDialog();
  Assert.deepEqual(
    service.callsTo("addMagnet")[0].args,
    [SYNTHETIC_MAGNET, { kind: "unknown", origin: null }],
    "The command line source is unknown"
  );
  await closeAddDialog(opened);

  ok(
    M6ETorrentHandler.handleCommandLineURI(
      Services.io.newURI("file:///mock/sample.torrent"),
      false
    ),
    "A .torrent file from Finder is handled"
  );
  opened = await waitForAddDialog(opened);
  Assert.deepEqual(
    service.callsTo("addFile")[0].args,
    ["/mock/sample.torrent", { kind: "finder", origin: null }],
    "A file from Finder has the finder source"
  );
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing is added");
  await closeAddDialog(opened);
});

add_task(async function test_download_of_local_torrent_file() {
  const { service } = installMocks();
  pageOpenReset();
  const launcher = fakeLauncher("file:///mock/dir/sample.torrent");
  ok(
    M6ETorrentHandler.handleDownload(launcher, null),
    "A .torrent opened from disk is taken over"
  );
  ok(launcher.cancelled, "The browser's own download is cancelled");
  const opened = await waitForAddDialog();
  Assert.deepEqual(
    service.callsTo("addFile")[0].args,
    ["/mock/dir/sample.torrent", { kind: "finder", origin: null }],
    "The file is opened with the finder source"
  );
  await closeAddDialog(opened);
});

add_task(async function test_download_detection() {
  installMocks();
  pageOpenReset();
  const plain = fakeLauncher("https://example.com/file.zip", {
    type: "application/zip",
  });
  ok(
    !M6ETorrentHandler.handleDownload(plain, null),
    "Other downloads are left to the browser"
  );
  ok(!plain.cancelled, "and are not cancelled");

  const background = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  const bgContext = background.linkedBrowser.browsingContext;
  await BrowserTestUtils.switchTab(gBrowser, gBrowser.tabs[0]);
  const hidden = fakeLauncher("https://example.com/sample.torrent");
  hidden.browsingContextId = bgContext.id;
  ok(
    !M6ETorrentHandler.handleDownload(hidden, null),
    "A .torrent requested by a tab in the background is not taken over"
  );
  ok(!hidden.cancelled, "and is left to the normal download flow");
  BrowserTestUtils.removeTab(background);
});

add_task(async function test_page_download_origin() {
  installMocks();
  pageOpenReset();
  const saved = [];
  const original = M6ETorrentHandler._saveToTemp;
  M6ETorrentHandler._saveToTemp = async (launcher, win, source) => {
    saved.push(source);
  };
  registerCleanupFunction(() => {
    M6ETorrentHandler._saveToTemp = original;
  });

  await BrowserTestUtils.withNewTab("https://example.com/", async () => {
    await SimpleTest.promiseFocus(window);
    const launcher = fakeLauncher("https://example.com/sample.torrent");
    ok(
      M6ETorrentHandler.handleDownload(launcher, null),
      "A .torrent from the foreground tab is taken over"
    );
    Assert.deepEqual(
      saved[0],
      { kind: "page", origin: "https://example.com" },
      "The page's origin is the source when the file is on the same site"
    );

    pageOpenReset();
    const foreign = fakeLauncher("https://example.net/sample.torrent");
    M6ETorrentHandler.handleDownload(foreign, null);
    Assert.deepEqual(
      saved[1],
      { kind: "page", origin: "https://example.net" },
      "A file from another site is credited to its own origin, not the page"
    );

    pageOpenReset();
    const insecure = fakeLauncher("http://example.com/sample.torrent");
    M6ETorrentHandler.handleDownload(insecure, null);
    Assert.deepEqual(
      saved[2],
      { kind: "page", origin: null },
      "An insecure file has no origin"
    );

    const second = fakeLauncher("https://example.com/other.torrent");
    ok(
      M6ETorrentHandler.handleDownload(second, null),
      "A second request inside the cooldown is consumed"
    );
    is(saved.length, 3, "but nothing is saved for it");
    ok(second.cancelled, "and the burst of page downloads is cancelled");
  });
  M6ETorrentHandler._saveToTemp = original;
});

add_task(async function test_magnet_goes_to_the_torrents_page_first() {
  const { service } = installMocks();
  pageOpenReset();
  const original = gBrowser.selectedTab;
  is(torrentsTabs().length, 0, "No torrents page is open yet");

  ok(
    M6ETorrentHandler.handleCommandLineURI(magnetURI(), false),
    "A magnet link is handled"
  );
  const opened = await waitForAddDialog();
  const tabs = torrentsTabs();
  is(tabs.length, 1, "A torrents page was opened");
  is(gBrowser.selectedTab, tabs[0], "and it is in the foreground");
  isnot(gBrowser.selectedTab, original, "instead of the tab the user was on");
  is(
    opened.dialog._frame.closest(".browserSidebarContainer"),
    dialogContainer(tabs[0].linkedBrowser),
    "The Add dialog belongs to the torrents page, so it shows over it"
  );
  Assert.deepEqual(
    service.callsTo("confirmAdd"),
    [],
    "Going to the page adds nothing"
  );

  await closeAddDialog(opened);
  is(torrentsTabs().length, 1, "Cancel leaves the torrents page open");
  is(gBrowser.selectedTab, tabs[0], "and selected");
  is(
    tabs[0].linkedBrowser.currentURI.spec,
    "about:torrents",
    "with nothing selected in it"
  );
  await TestUtils.waitForCondition(
    () => service.callsTo("cancelAdd").length == 1,
    "Cancel drops the request"
  );
  Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing was added");
});

add_task(async function test_an_open_torrents_page_is_reused() {
  const { service } = installMocks();
  pageOpenReset();
  const page = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:torrents"
  );
  await BrowserTestUtils.switchTab(gBrowser, gBrowser.tabs[0]);
  isnot(gBrowser.selectedTab, page, "The torrents page is in the background");

  M6ETorrentHandler.handleCommandLineURI(
    Services.io.newURI("file:///mock/sample.torrent"),
    false
  );
  const opened = await waitForAddDialog();
  is(torrentsTabs().length, 1, "No second torrents page was opened");
  is(gBrowser.selectedTab, page, "The existing page was selected");
  is(
    opened.dialog._frame.closest(".browserSidebarContainer"),
    dialogContainer(page.linkedBrowser),
    "The dialog shows over the existing page"
  );
  Assert.deepEqual(
    service.callsTo("addFile")[0].args,
    ["/mock/sample.torrent", { kind: "finder", origin: null }],
    "The file is opened with the finder source"
  );
  await closeAddDialog(opened);
  is(torrentsTabs().length, 1, "The page stays open after Cancel");
});

add_task(async function test_page_initiated_magnet_goes_to_the_page() {
  const { service } = installMocks();
  pageOpenReset();
  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    await SimpleTest.promiseFocus(window);
    ok(
      M6ETorrentHandler.handleMagnet(
        magnetURI(),
        principalFor(HTTPS_ORIGIN),
        browser.browsingContext,
        false,
        true
      ),
      "The navigation is handled"
    );
    const opened = await waitForAddDialog();
    const tabs = torrentsTabs();
    is(tabs.length, 1, "The torrents page was opened");
    is(gBrowser.selectedTab, tabs[0], "and selected");
    Assert.deepEqual(
      service.callsTo("addMagnet")[0].args,
      [SYNTHETIC_MAGNET, { kind: "magnet-page", origin: HTTPS_ORIGIN }],
      "The page's origin is still recorded as the source"
    );
    Assert.deepEqual(service.callsTo("confirmAdd"), [], "Nothing is added");
    await closeAndSettle(opened);
  });
});

add_task(async function test_add_selects_the_new_torrent_in_the_page() {
  const { service } = installMocks();
  pageOpenReset();

  M6ETorrentHandler.handleCommandLineURI(
    Services.io.newURI("file:///mock/sample.torrent"),
    false
  );
  const opened = await waitForAddDialog();
  await waitForDetails(opened.doc);
  const tab = gBrowser.selectedTab;
  const win = tab.linkedBrowser.contentWindow;
  is(win.location.hash, "", "No torrent is selected before Add");

  service.torrents = [syntheticStatus(SYNTHETIC_HASH)];
  opened.doc.getElementById("m6e-torrent-add-confirm").click();
  await opened.closedPromise;
  is(service.callsTo("confirmAdd").length, 1, "Add confirmed the torrent");

  await TestUtils.waitForCondition(
    () => win.location.hash == "#" + SYNTHETIC_HASH,
    "The new torrent is in the page's URL"
  );
  await TestUtils.waitForCondition(
    () => win.gM6ETorrentsPage.selectedTid == SYNTHETIC_HASH,
    "The page selected the new torrent"
  );
  is(gBrowser.selectedTab, tab, "The user stays on the torrents page");
  is(torrentsTabs().length, 1, "No other torrents page was opened");
});

add_task(async function test_duplicate_selects_the_existing_torrent() {
  const { service } = installMocks();
  pageOpenReset();
  service.torrents = [syntheticStatus(SYNTHETIC_HASH)];
  service.addError = Object.assign(new Error("already added"), {
    code: "duplicate",
    tid: SYNTHETIC_HASH,
  });

  M6ETorrentHandler.handleCommandLineURI(
    Services.io.newURI("file:///mock/sample.torrent"),
    false
  );
  await TestUtils.waitForCondition(
    () => torrentsTabs().length == 1,
    "The torrents page is opened"
  );
  const tab = torrentsTabs()[0];
  const win = tab.linkedBrowser.contentWindow;
  await TestUtils.waitForCondition(
    () => win.gM6ETorrentsPage?.selectedTid == SYNTHETIC_HASH,
    "The torrent that is already added is selected"
  );
  is(win.location.hash, "#" + SYNTHETIC_HASH, "and shown in the URL");
  is(gM6ETorrent.lastAddDialog, null, "No Add dialog is shown for a duplicate");
  ok(!gM6ETorrent.panelShown, "The panel stays closed");
  is(gBrowser.selectedTab, tab, "The user is on the torrents page");
});

add_task(async function test_disabled_torrents_do_not_open_the_page() {
  const { service } = installMocks();
  pageOpenReset();
  await SpecialPowers.pushPrefEnv({ set: [["m6e.torrent.enabled", false]] });
  ok(
    !M6ETorrentHandler.handleCommandLineURI(magnetURI(), false),
    "With BitTorrent off the link is left alone"
  );
  await settle();
  is(torrentsTabs().length, 0, "and the torrents page is not opened");
  Assert.deepEqual(
    service.calls.filter(c => c.name != "init"),
    [],
    "Nothing reached the service"
  );
  await SpecialPowers.popPrefEnv();
});
