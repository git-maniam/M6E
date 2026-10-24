/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

describe("settings ai features", () => {
  let doc, win;

  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.preferences.aiControls", true]],
    });
    await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
    doc = gBrowser.selectedBrowser.contentDocument;
    win = doc.documentGlobal;
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  });

  function waitForAnimationFrame() {
    return new Promise(r => win.requestAnimationFrame(r));
  }

  describe("block AI confirmation dialog", () => {
    it("closes dialog and does nothing on cancel", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
        ],
      });

      await openAiFeaturePanel(doc, win);

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
      Assert.equal(
        Services.prefs.getStringPref("browser.ai.control.default"),
        "available",
        "Pref unchanged after clicking toggle"
      );

      EventUtils.synthesizeMouseAtCenter(dialogEl.cancelButton, {}, win);

      Assert.ok(!dialogEl.dialog.open, "Dialog is closed after cancel");
      Assert.equal(
        Services.prefs.getStringPref("browser.ai.control.default"),
        "available",
        "Pref unchanged after cancel"
      );
    });

    it("blocks AI features on confirm, unblocks on toggle off", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
        ],
      });
      Services.fog.testResetFOG();

      await openAiFeaturePanel(doc, win);

      // Flip the toggle to show confirmation dialog.
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
      Assert.ok(!toggle.pressed, "Toggle is unpressed during confirmation");
      Assert.equal(
        Services.prefs.getStringPref("browser.ai.control.default"),
        "available",
        "Pref unchanged after clicking toggle"
      );
      Assert.ok(
        !Glean.browser.globalAiControlToggled.testGetValue(),
        "No telemetry recorded before confirmation"
      );

      // Confirm the dialog to block
      let defaultSetting = win.Preferences.getSetting("aiControlDefaultToggle");
      let translationsSetting = win.Preferences.getSetting(
        "aiControlTranslationsSelect"
      );
      Assert.equal(
        translationsSetting.value,
        "available",
        "Translations are enabled"
      );
      await waitForSettingChange(defaultSetting, () =>
        EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );
      Assert.ok(toggle.pressed, "Toggle is pressed after block");
      Assert.ok(!dialogEl.dialog.open, "Dialog is closed after confirm");
      Assert.equal(
        Services.prefs.getStringPref("browser.ai.control.default"),
        "blocked",
        "Pref set to blocked after confirm"
      );
      Assert.equal(
        translationsSetting.value,
        "blocked",
        "Translations are now blocked"
      );
      let telemetryEvents = Glean.browser.globalAiControlToggled.testGetValue();
      Assert.equal(telemetryEvents.length, 1, "One telemetry event recorded");
      Assert.equal(
        telemetryEvents[0].extra.blocked,
        "true",
        "Telemetry recorded blocked=true"
      );

      // Enable key points to confirm it stays enabled on un-block
      let keyPointsSetting = win.Preferences.getSetting(
        "aiControlLinkPreviewKeyPointsSelect"
      );
      Assert.equal(
        keyPointsSetting.value,
        "blocked",
        "Key points are blocked after global block"
      );
      await waitForAnimationFrame();
      const keyPointsControl = doc.getElementById("aiControlLinkPreviewKeyPointsSelect");
      keyPointsControl.focus();
      let pickerOpened = BrowserTestUtils.waitForSelectPopupShown(
        win.docShell.chromeEventHandler.documentGlobal
      );
      EventUtils.sendKey("space");
      const selectPopup = await pickerOpened;
      await waitForSettingChange(keyPointsSetting, () => {
        if (selectPopup.isNativeMenu) {
          selectPopup.activateItem(selectPopup.childNodes[1]);
        } else {
          EventUtils.sendKey("up");
          EventUtils.sendKey("return");
        }
      });
      Assert.equal(keyPointsSetting.value, "enabled", "Key points are now enabled");

      // Unblock to confirm reset to available and key points are still enabled
      toggle.buttonEl.scrollIntoView();
      await waitForAnimationFrame();
      await waitForSettingChange(defaultSetting, () =>
        EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win)
      );
      Assert.ok(!toggle.pressed, "Toggle is not pressed after unblocking");
      Assert.equal(
        Services.prefs.getStringPref("browser.ai.control.default"),
        "available",
        "Pref set to available after unblocking"
      );
      Assert.equal(
        translationsSetting.value,
        "available",
        "Translations are now available"
      );
      Assert.equal(keyPointsSetting.value, "enabled", "Key points stayed enabled");
      telemetryEvents = Glean.browser.globalAiControlToggled.testGetValue();
      Assert.equal(telemetryEvents.length, 2, "Two telemetry events recorded");
      Assert.equal(
        telemetryEvents[1].extra.blocked,
        "false",
        "Telemetry recorded blocked=false"
      );
    });
  });
});
