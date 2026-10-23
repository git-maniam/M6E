/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: the page telemetry button and panel (D29).

const { M6EPageTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/pagetelemetry/M6EPageTelemetry.sys.mjs"
);
const TEST_PATH =
  "https://example.com/browser/browser/components/pagetelemetry/tests/browser/file_page_telemetry.html";
// A fresh URL per load, so that the measured load isn't served from cache.
let gLoads = 0;
const testURL = () => `${TEST_PATH}?load=${++gLoads}&t=${Date.now()}`;

add_task(async function test_button_tooltip() {
  let button = document.getElementById("m6e-page-telemetry-button");
  await BrowserTestUtils.withNewTab(testURL(), async () => {
    await TestUtils.waitForCondition(
      () => button.getAttribute("tooltiptext") == "Telemetry",
      "The button's tooltip says Telemetry"
    );
    ok(BrowserTestUtils.isVisible(button), "The button is shown on a web page");
  });
});

add_task(async function test_panel_values() {
  await BrowserTestUtils.withNewTab(testURL(), async browser => {
    let data;
    await TestUtils.waitForCondition(
      async () => {
        data = await M6EPageTelemetry.collect(browser);
        return data?.loaded && data.tti != null;
      },
      "Wait for the load event and time to interactive",
      500,
      40
    );

    Assert.greater(data.ttfb, 0, "TTFB is measured");
    Assert.greater(data.fcp, 0, "FCP is measured");
    Assert.greaterOrEqual(data.tti, data.fcp, "TTI comes after FCP");
    Assert.greater(data.scriptParse, 0, "Script parse time is measured");
    Assert.greaterOrEqual(
      data.scriptExecution,
      25,
      "The inline loop's 30 ms are counted as execution"
    );
    Assert.greaterOrEqual(
      data.size.requests,
      3,
      "The document, the script and the image are counted"
    );
    Assert.greater(data.size.transferred, 0, "Transferred bytes are counted");
    ok(
      ["handshake", "reused"].includes(data.tlsState),
      `The https page reports its handshake (${data.tlsState})`
    );
    if (data.tlsState == "handshake") {
      Assert.greater(data.tls, 0, "The handshake time is measured");
    }

    let panel = document.getElementById("m6e-page-telemetry-panel");
    let shown = BrowserTestUtils.waitForEvent(panel, "popupshown");
    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("m6e-page-telemetry-button"),
      {}
    );
    await shown;
    let ttfbCell = panel.querySelector('tr[data-metric="ttfb"] > td');
    await TestUtils.waitForCondition(
      () => /ms$/.test(ttfbCell.textContent),
      "The panel shows the TTFB"
    );
    let sizeCell = panel.querySelector('tr[data-metric="size"] > td');
    await TestUtils.waitForCondition(
      () => /requests/.test(sizeCell.textContent),
      "The panel shows the page size"
    );
    let hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  });
});

add_task(async function test_not_a_web_page() {
  await BrowserTestUtils.withNewTab("about:robots", async browser => {
    is(
      await M6EPageTelemetry.collect(browser),
      null,
      "Nothing is measured for non-web pages"
    );
  });
});
