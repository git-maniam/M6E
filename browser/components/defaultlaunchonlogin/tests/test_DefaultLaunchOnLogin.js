/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

/* import-globals-from ../../../../testing/xpcshell/head.js */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { DefaultLaunchOnLogin, DEFAULT_LAUNCH_ON_LOGIN_PREF } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/defaultlaunchonlogin/DefaultLaunchOnLogin.sys.mjs"
  );
const { LaunchOnLogin } = ChromeUtils.importESModule(
  "resource://gre/modules/LaunchOnLogin.sys.mjs"
);
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

const CATEGORY_NAME = "browser-idle-startup";
const MODULE_URI =
  "moz-src:///browser/components/defaultlaunchonlogin/DefaultLaunchOnLogin.sys.mjs";

let registry = null;
add_setup(async () => {
  // FOG needs a profile
  do_get_profile();

  Services.fog.initializeFOG();
  Services.fog.testResetFOG();
  updateAppInfo();
});

// Runs enableOnFirstRunIfNeeded with the LaunchOnLogin side effects stubbed.
// Sets the defaultEnabled pref to prefValue.
async function runWith(
  isFirstRun,
  isOfficialBuild,
  approved,
  alreadyApplied,
  prefValue
) {
  let sandbox = sinon.createSandbox();
  let approvedStub = sandbox
    .stub(LaunchOnLogin, "isAllowed")
    .resolves(approved);
  let createStub = sandbox.stub(LaunchOnLogin, "enable").resolves();

  Services.prefs.setBoolPref(DEFAULT_LAUNCH_ON_LOGIN_PREF, prefValue);

  try {
    await DefaultLaunchOnLogin.enableOnFirstRunIfNeeded(
      isFirstRun,
      isOfficialBuild,
      alreadyApplied
    );
    return { approvedStub, createStub };
  } finally {
    sandbox.restore();
    Services.prefs.clearUserPref(DEFAULT_LAUNCH_ON_LOGIN_PREF);
  }
}

add_task(async function test_is_registered_in_idle_startup() {
  const entry = Services.catMan.getCategoryEntry(CATEGORY_NAME, MODULE_URI);
  Assert.equal(
    entry,
    "DefaultLaunchOnLogin.maybeEnableOnFirstRun",
    "Entry should point to `maybeEnableOnFirstRun` in `browser-idle-startup`"
  );
});

add_task(
  {
    skip_if: () =>
      !AppConstants.MOZ_NORMANDY || AppConstants.platform !== "linux",
  },
  async function test_disabled_when_pref_off() {
    let { createStub } = await runWith(true, true, true, false, false);
    Assert.ok(
      !createStub.called,
      "LaunchOnLogin.enable should not be called when the pref is off"
    );
  }
);

add_task(
  {
    skip_if: () =>
      !AppConstants.MOZ_NORMANDY || AppConstants.platform !== "linux",
  },
  async function test_enabled_when_pref_on() {
    let { createStub } = await runWith(true, true, true, false, true);
    Assert.ok(
      createStub.calledOnce,
      "LaunchOnLogin.enable should be called when the pref is on"
    );
  }
);

add_task(async function test_skips_when_not_first_run() {
  let { createStub } = await runWith(false, true, true, false, true);
  Assert.ok(
    !createStub.called,
    "LaunchOnLogin.enable should not be called when isFirstRun is false"
  );
});

add_task(async function test_skips_on_unofficial_build() {
  let { createStub } = await runWith(true, false, true, false, true);
  Assert.ok(
    !createStub.called,
    "LaunchOnLogin.enable should not be called on developer builds"
  );
});

add_task(
  {
    skip_if: () =>
      !AppConstants.MOZ_NORMANDY || AppConstants.platform !== "linux",
  },
  async function test_skips_when_windows_policy_denies() {
    let { createStub, approvedStub } = await runWith(
      true,
      true,
      false,
      false,
      true
    );
    Assert.ok(
      approvedStub.calledOnce,
      "policy approval should be consulted when first run and the pref is on"
    );
    Assert.ok(
      !createStub.called,
      "LaunchOnLogin should not be called when Windows policy denies"
    );
  }
);

add_task(
  {
    skip_if: () =>
      !AppConstants.MOZ_NORMANDY || AppConstants.platform !== "linux",
  },
  async function test_skips_when_already_applied() {
    let { createStub, approvedStub } = await runWith(
      true,
      true,
      true,
      true,
      true
    );
    Assert.ok(
      !approvedStub.called,
      "policy approval should not be consulted when already applied is true"
    );

    Assert.ok(
      !createStub.called,
      "LaunchOnLogin.enable should not be called when already applied is true"
    );
  }
);
