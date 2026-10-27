/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

describe("AI Controls telemetry", () => {
  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.preferences.aiControls", true],
        ["browser.ai.control.default", "available"],
      ],
    });
    Services.fog.testResetFOG();
    await openPreferencesViaOpenPreferencesAPI("ai", { leaveOpen: true });
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
});
