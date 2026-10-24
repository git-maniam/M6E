/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(3);

describe("settings ai features", () => {
  let doc, win;

  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.preferences.aiControls", true]],
    });
    let aiControlsLoaded = TestUtils.topicObserved("ai-pane-loaded");
    await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
    await aiControlsLoaded;
    doc = gBrowser.selectedBrowser.contentDocument;
    win = doc.documentGlobal;
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  });

  function waitForAnimationFrame() {
    return new Promise(r => win.requestAnimationFrame(r));
  }

  describe("AI Controls visibility on General pane", () => {
    it("hides Tab Group Suggestions when globally blocked", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
          ["browser.ai.control.smartTabGroups", "default"],
          ["browser.tabs.groups.enabled", true],
          ["browser.tabs.groups.smart.enabled", true],
          ["browser.tabs.groups.smart.userEnabled", true],
        ],
      });

      // Tab Group Suggestions is only available in en-* locales
      if (!Services.locale.appLocaleAsBCP47.startsWith("en")) {
        Assert.ok(true, "Skipping: locale is not en-*");
        return;
      }

      let aiControlsTab = gBrowser.selectedTab;
      await openAiFeaturePanel(doc, win);

      await new Promise(resolve => open_preferences(resolve));
      let generalTab = gBrowser.selectedTab;
      let generalDoc = gBrowser.selectedBrowser.contentDocument;
      let generalWin = generalDoc.documentGlobal;
      await maybeNavigateToPane("tabsBrowsing", generalWin);

      let tabGroupSetting = generalWin.Preferences.getSetting(
        "tabGroupSuggestions"
      );
      let tabGroupControl = generalDoc.getElementById("tabGroupSuggestions");
      Assert.ok(
        BrowserTestUtils.isVisible(tabGroupControl),
        "Tab Group Suggestions control is visible"
      );

      gBrowser.selectedTab = aiControlsTab;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
      await dialogEl.updateComplete;
      let dialogShown = BrowserTestUtils.waitForEvent(
        dialogEl.dialog,
        "toggle"
      );
      EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
      await dialogShown;
      Assert.ok(dialogEl.dialog.open, "Dialog is open");
      await waitForSettingChange(tabGroupSetting, () =>
        EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );

      gBrowser.selectedTab = generalTab;
      Assert.ok(
        !BrowserTestUtils.isVisible(tabGroupControl),
        "Tab Group Suggestions control is hidden after blocking"
      );

      BrowserTestUtils.removeTab(generalTab);
    });

    it("hides Translations setting when globally blocked via AI Controls toggle", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
          ["browser.ai.control.translations", "default"],
          ["browser.translations.enable", true],
        ],
      });

      let aiControlsTab = gBrowser.selectedTab;
      await openAiFeaturePanel(doc, win);

      await new Promise(resolve => open_preferences(resolve));
      let generalTab = gBrowser.selectedTab;
      let generalDoc = gBrowser.selectedBrowser.contentDocument;
      let generalWin = generalDoc.documentGlobal;
      await maybeNavigateToPane("languages", generalWin);

      let translationsSetting =
        generalWin.Preferences.getSetting("offerTranslations");
      let translationsGroup = generalDoc.querySelector(
        'setting-group[groupid="translations"]'
      );
      Assert.ok(
        BrowserTestUtils.isVisible(translationsGroup),
        "Translations group is visible"
      );

      gBrowser.selectedTab = aiControlsTab;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
      await dialogEl.updateComplete;
      let dialogShown = BrowserTestUtils.waitForEvent(
        dialogEl.dialog,
        "toggle"
      );
      EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
      await dialogShown;
      Assert.ok(dialogEl.dialog.open, "Dialog is open");
      await waitForSettingChange(translationsSetting, () =>
        EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );

      gBrowser.selectedTab = generalTab;
      await TestUtils.waitForCondition(
        () => !BrowserTestUtils.isVisible(translationsGroup),
        "Translations group is hidden after blocking"
      );

      // Explicitly enable Translations while globally blocked
      gBrowser.selectedTab = aiControlsTab;
      const translationsSelect = doc.getElementById(
        "aiControlTranslationsSelect"
      );
      translationsSelect.scrollIntoView();
      await waitForAnimationFrame();
      translationsSelect.focus();
      let pickerOpened = BrowserTestUtils.waitForSelectPopupShown(
        win.docShell.chromeEventHandler.documentGlobal
      );
      EventUtils.sendKey("space");
      const selectPopup = await pickerOpened;
      await waitForSettingChange(translationsSetting, () => {
        if (selectPopup.isNativeMenu) {
          selectPopup.activateItem(selectPopup.childNodes[0]);
        } else {
          EventUtils.sendKey("up");
          EventUtils.sendKey("return");
        }
      });

      gBrowser.selectedTab = generalTab;
      await TestUtils.waitForCondition(
        () => BrowserTestUtils.isVisible(translationsGroup),
        "Translations group is visible after explicitly enabling"
      );

      BrowserTestUtils.removeTab(generalTab);
    });
  });
});
