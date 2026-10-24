/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

describe("AI Controls telemetry", () => {
  let doc;

  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.preferences.aiControls", true],
        ["browser.ai.control.default", "available"],
        ["browser.ai.control.translations", "default"],
        ["browser.ai.control.pdfjsAltText", "default"],
        ["browser.ai.control.smartTabGroups", "default"],
        ["browser.translations.enable", true],
        ["browser.tabs.groups.smart.optin", true],
      ],
    });
    Services.fog.testResetFOG();
    await openPreferencesViaOpenPreferencesAPI("ai", { leaveOpen: true });
    doc = gBrowser.selectedBrowser.contentDocument;
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  });

  it("records no event before interaction", async () => {
    Assert.equal(
      Glean.browser.aiControlChanged.testGetValue(),
      null,
      "No events recorded before any interaction"
    );
  });

  it("records events when translations set to blocked", async () => {
    let selectEl = doc.getElementById("aiControlTranslationsSelect");
    await changeMozSelectValue(selectEl, "blocked");

    let events = Glean.browser.aiControlChanged.testGetValue();
    Assert.equal(events.length, 1, "One event recorded");
    Assert.equal(
      events[0].extra.feature,
      "translations",
      "Feature is translations"
    );
    Assert.equal(events[0].extra.selection, "blocked", "Selection is blocked");
  });

  it("records event when smartTabGroups set to enabled", async () => {
    let selectEl = doc.getElementById("aiControlSmartTabGroupsSelect");
    await changeMozSelectValue(selectEl, "enabled");

    let events = Glean.browser.aiControlChanged.testGetValue();
    Assert.equal(events.length, 1, "One event recorded");
    Assert.equal(
      events[0].extra.feature,
      "smartTabGroups",
      "Feature is smartTabGroups"
    );
    Assert.equal(events[0].extra.selection, "enabled", "Selection is enabled");
  });
});
