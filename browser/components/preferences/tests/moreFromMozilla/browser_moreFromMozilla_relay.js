/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_relay_promo() {
  await clearPolicies();

  let { relayPromoCard } = await getPromoCards();
  ok(relayPromoCard, "The Relay promo is visible");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
