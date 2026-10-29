/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  Services.fog.initializeFOG();
});

add_task(async function test_SUBMIT_ONBOARDING_OPT_OUT_PING() {
  let { promise, resolve } = Promise.withResolvers();

  GleanPings.onboardingOptOut.testBeforeNextSubmit(() => resolve(true));

  await SMATestUtils.executeAndValidateAction({
    type: "SUBMIT_ONBOARDING_OPT_OUT_PING",
  });

  ok(await promise, "`onboarding-opt-out` ping was submitted");
});
