/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const DUMMY =
  "https://example.com/browser/browser/components/tabbrowser/test/browser/tabs/dummy_page.html";

function isBrowserAppTab(browser) {
  return browser.browsingContext.isAppTab;
}

add_task(async function navigate() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:robots");
  let browser = tab.linkedBrowser;
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserStopped(gBrowser);
  let isAppTab = isBrowserAppTab(browser);
  ok(!isAppTab, "Docshell shouldn't think it is an app tab");

  gBrowser.pinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  BrowserTestUtils.startLoadingURIString(gBrowser, DUMMY);
  await BrowserTestUtils.browserStopped(gBrowser);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  gBrowser.unpinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(!isAppTab, "Docshell shouldn't think it is an app tab");

  gBrowser.pinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  BrowserTestUtils.startLoadingURIString(gBrowser, "about:robots");
  await BrowserTestUtils.browserStopped(gBrowser);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  gBrowser.removeCurrentTab();
});
