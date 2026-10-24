/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* globals browser */

ChromeUtils.defineLazyGetter(this, "SidebarTestUtils", () => {
  const { SidebarTestUtils: utils } = ChromeUtils.importESModule(
    "resource://testing-common/SidebarTestUtils.sys.mjs"
  );
  utils.init(this);
  return utils;
});

// Verifies the alternate tab context menu layout
// (browser.tabs.contextmenu.altstructure.enabled): the tab "move" operations
// are relocated into the "Move Tab to" submenu, and WebExtension items land in
// the trailing extensions section.

const ALT_PREF = "browser.tabs.contextmenu.altstructure.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [ALT_PREF, true],
      ["browser.tabs.groups.enabled", true],
    ],
  });
});

// The "move" operations (positions, tab groups, Select All Tabs) are flattened
// into the #context_moveTabOptions ("Move Tab to") submenu rather than sitting
// at the top level as in the classic layout.
add_task(async function test_move_operations_in_submenu() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await openTabContextMenu(gBrowser.selectedTab);

    Assert.equal(
      document.getElementById("context_moveTabOptions").parentNode.id,
      "tabContextMenu",
      "'Move Tab to' is a top-level submenu"
    );

    for (const id of [
      "context_moveToStart",
      "context_moveToEnd",
      "context_openTabInWindow",
      "context_moveTabToGroupNewGroup",
      "context_moveTabToSavedGroup",
      "context_selectAllTabs",
    ]) {
      Assert.equal(
        document.getElementById(id).parentNode.id,
        "moveTabOptionsMenu",
        `#${id} is inside the "Move Tab to" submenu`
      );
    }

    // Close Duplicates is flattened into the "Close Multiple" submenu.
    Assert.equal(
      document.getElementById("context_closeDuplicateTabs").parentNode.id,
      "closeTabOptions",
      "Close Duplicates is inside the Close Multiple submenu"
    );

    await closeTabContextMenu();
  });
});

// A WebExtension tab context menu item appears in the trailing extensions
// section (after the layout's managed items), which the alt layout leaves open.
add_task(async function test_extension_item_in_extensions_section() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["menus"] },
    background() {
      browser.menus.create(
        { id: "alt-ext-item", title: "Alt Ext Item", contexts: ["tab"] },
        () => browser.test.sendMessage("ready")
      );
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    const menu = await openTabContextMenu(gBrowser.selectedTab);

    let extItem;
    await BrowserTestUtils.waitForMutationCondition(
      menu,
      { childList: true },
      () => {
        extItem = menu.querySelector(':scope > menuitem[label="Alt Ext Item"]');
        return !!extItem;
      }
    );

    Assert.equal(
      extItem.parentNode,
      menu,
      "extension item is a top-level child of the tab context menu"
    );
    Assert.ok(
      document
        .getElementById("context_undoCloseTab")
        .compareDocumentPosition(extItem) & Node.DOCUMENT_POSITION_FOLLOWING,
      "extension item follows the layout's managed items (trailing extensions section)"
    );

    await closeTabContextMenu();
  });

  await extension.unload();
  // Close the panel opened by installing the extension
  SidebarTestUtils.closePanel(window);
});
