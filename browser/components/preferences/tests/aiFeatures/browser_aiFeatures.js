/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(3);

add_setup(async function setupPrefs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.preferences.aiControls", true],
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
      ["browser.ai.control.pdfjsAltText", "default"],
      ["browser.ai.control.smartTabGroups", "default"],
    ],
  });
});

describe("settings ai features", () => {
  describe("managed by policy", () => {
    async function runPolicyTest(doc, name, pref, settingId) {
      try {
        Services.prefs.lockPref(pref);
        doc.documentGlobal.Preferences.getSetting(settingId).emit("change");
        await new Promise(r => doc.documentGlobal.requestAnimationFrame(r));

        const control = doc.getElementById(settingId);
        Assert.ok(control, `${name} control exists`);
        Assert.ok(
          BrowserTestUtils.isVisible(control),
          `${name} control is visible when locked`
        );
        Assert.ok(
          control.disabled,
          `${name} control is disabled when pref is locked`
        );
      } finally {
        Services.prefs.unlockPref(pref);
      }
    }

    it("disables based on enterprise policies", async () => {
      await withPrefsPane("ai", async doc => {
        await runPolicyTest(
          doc,
          "Smart Tab Groups",
          "browser.tabs.groups.smart.userEnabled",
          "aiControlSmartTabGroupsSelect"
        );
        await runPolicyTest(
          doc,
          "Translations",
          "browser.translations.enable",
          "aiControlTranslationsSelect"
        );
      });
    });
  });

  describe("tab group suggestions visibility", () => {
    it("hides the toggle when tab groups are disabled and shows it when enabled", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.tabs.groups.enabled", true],
          ["browser.preferences.aiControls.showUnavailable", true],
        ],
      });

      await withPrefsPane("ai", async doc => {
        let win = doc.documentGlobal;
        const setting = win.Preferences.getSetting(
          "aiControlSmartTabGroupsSelect"
        );
        const control = doc.getElementById("aiControlSmartTabGroupsSelect");
        Assert.ok(
          BrowserTestUtils.isVisible(control),
          "Tab group suggestions control is visible when tab groups are enabled"
        );

        let settingChanged = waitForSettingChange(setting);
        await SpecialPowers.pushPrefEnv({
          set: [["browser.tabs.groups.enabled", false]],
        });
        await settingChanged;
        await new Promise(r => win.requestAnimationFrame(r));
        Assert.ok(
          !BrowserTestUtils.isVisible(control),
          "Tab group suggestions control is hidden when tab groups are disabled"
        );

        settingChanged = waitForSettingChange(setting);
        await SpecialPowers.pushPrefEnv({
          set: [["browser.tabs.groups.enabled", true]],
        });
        await settingChanged;
        await new Promise(r => win.requestAnimationFrame(r));
        Assert.ok(
          BrowserTestUtils.isVisible(control),
          "Tab group suggestions control is visible again when tab groups are re enabled"
        );
      });
    });

    it("is hidden on initial load when tab groups are already disabled", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.tabs.groups.enabled", false],
          ["browser.preferences.aiControls.showUnavailable", true],
        ],
      });

      await withPrefsPane("ai", async doc => {
        const control = doc.getElementById("aiControlSmartTabGroupsSelect");
        Assert.ok(
          !BrowserTestUtils.isVisible(control),
          "Tab group suggestions control is hidden on initial render when tab groups are disabled"
        );
      });
    });
  });
});
