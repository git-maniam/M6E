/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: Reader View strips clutter, shows black text on white at 125% of the
// stock size, and offers no colour schemes.

const TEST_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

add_task(async function test_m6e_reader_view() {
  await BrowserTestUtils.withNewTab(
    TEST_PATH + "readerModeArticleClutter.html",
    async function (browser) {
      let readerButton = document.getElementById("reader-mode-button");
      await TestUtils.waitForCondition(() => !readerButton.hidden);
      let pageShownPromise = BrowserTestUtils.waitForContentEvent(
        browser,
        "AboutReaderContentReady"
      );
      readerButton.click();
      await pageShownPromise;

      await SpecialPowers.spawn(browser, [], async function () {
        let doc = content.document;
        let article = doc.querySelector(".moz-reader-content");
        let photo = article.querySelector("img[src$='Photo.png']");
        await ContentTaskUtils.waitForCondition(
          () =>
            photo.complete && !article.querySelector("img[src$='Pixel.png']"),
          "Wait for the images to load"
        );

        for (let selector of [
          "nav",
          "aside",
          "iframe",
          "video",
          "#ad-slot-1",
        ]) {
          is(article.querySelectorAll(selector).length, 0, `No ${selector}`);
        }
        let text = article.textContent;
        ok(!text.includes("Buy our product"), "The id-marked ad is removed");
        ok(
          !text.includes("Sponsored banner"),
          "The class-marked ad is removed"
        );
        ok(
          ![...article.querySelectorAll("p")].some(
            p => p.textContent.trim() == "Advertisement"
          ),
          "The ad label is removed"
        );
        ok(article.querySelector("figure figcaption"), "The figure is kept");

        let style = content.getComputedStyle(doc.body);
        is(style.color, "rgb(0, 0, 0)", "Text is black");
        is(style.backgroundColor, "rgb(255, 255, 255)", "Background is white");
        let coloured = [...article.querySelectorAll("p")].find(p =>
          p.textContent.includes("page colours")
        );
        is(content.getComputedStyle(coloured).color, "rgb(0, 0, 0)");
        is(
          content.getComputedStyle(coloured).backgroundColor,
          "rgba(0, 0, 0, 0)",
          "Page colours are removed"
        );
        is(
          content.getComputedStyle(article.querySelector("p")).fontSize,
          "25px",
          "The default text size is 125% of 20px"
        );
        ok(doc.querySelector(".colors-dropdown").hidden, "No colours menu");
      });
    }
  );
});

add_task(async function test_menu_item_on_any_page() {
  await BrowserTestUtils.withNewTab(
    TEST_PATH + "readerModeNonArticle.html",
    async function () {
      let menuitem = document.getElementById("menu_readerModeItem");
      let readerButton = document.getElementById("reader-mode-button");
      await TestUtils.waitForCondition(() => !menuitem.hidden);
      ok(readerButton.hidden, "The urlbar button is only for articles");
      ok(
        !document
          .getElementById("key_toggleReaderMode")
          .hasAttribute("disabled"),
        "The shortcut works on any web page"
      );
    }
  );
});
