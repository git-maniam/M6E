/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

add_task(async function () {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:support" },
    async function (browser) {
      let keyLocationServiceGoogleStatus = await SpecialPowers.spawn(
        browser,
        [],
        async function () {
          let textBox = content.document.getElementById(
            "key-location-service-google-box"
          );
          await ContentTaskUtils.waitForCondition(
            () => content.document.l10n.getAttributes(textBox).id,
            "Google location service API key status loaded"
          );
          return content.document.l10n.getAttributes(textBox).id;
        }
      );
      ok(
        keyLocationServiceGoogleStatus,
        "Google location service API key status shown"
      );

      let keySafebrowsingGoogleStatus = await SpecialPowers.spawn(
        browser,
        [],
        async function () {
          let textBox = content.document.getElementById(
            "key-safebrowsing-google-box"
          );
          await ContentTaskUtils.waitForCondition(
            () => content.document.l10n.getAttributes(textBox).id,
            "Google Safebrowsing API key status loaded"
          );
          return content.document.l10n.getAttributes(textBox).id;
        }
      );
      ok(
        keySafebrowsingGoogleStatus,
        "Google Safebrowsing API key status shown"
      );

      let keyMozillaStatus = await SpecialPowers.spawn(
        browser,
        [],
        async function () {
          let textBox = content.document.getElementById("key-mozilla-box");
          await ContentTaskUtils.waitForCondition(
            () => content.document.l10n.getAttributes(textBox).id,
            "Mozilla API key status loaded"
          );
          return content.document.l10n.getAttributes(textBox).id;
        }
      );
      ok(keyMozillaStatus, "Mozilla API key status shown");
    }
  );
});

add_task(async function test_sanitize_paths() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:support" },
    async function (browser) {
      let snapshot = {
        secretPath: "/home/user",
        "bool.pref.ending.with.path": true,
        nullValue: null,
        emptyString: "",
        object: {
          SOME_DIR: "/home/kit",
        },
        array: [{ myDirectory: "/home/kit" }],
      };
      snapshot = await SpecialPowers.spawn(
        browser,
        [snapshot],
        async function (aSnapshot) {
          content.sanitizeSnapshot(aSnapshot);
          return aSnapshot;
        }
      );
      Assert.strictEqual(snapshot["bool.pref.ending.with.path"], true);
      Assert.strictEqual(snapshot.nullValue, null);
      Assert.equal(snapshot.secretPath, "<non-empty string>");
      Assert.equal(snapshot.object.SOME_DIR, "<non-empty string>");
      Assert.equal(snapshot.array[0].myDirectory, "<non-empty string>");
      Assert.equal(snapshot.emptyString, "");
    }
  );
});

add_task(async function test_remote_settings() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(RemoteSettings, "inspect").resolves({
    isSynchronizationBroken: false,
    lastCheck: 1715698289,
    localTimestamp: '"1715698176626"',
    history: {
      "settings-sync": [
        { status: "SUCCESS", datetime: "2024-05-14T14:49:36.626Z", infos: {} },
      ],
    },
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:support" },
    async browser => {
      const localTimestamp = await SpecialPowers.spawn(
        browser,
        [],
        async () => {
          const sel = "#support-remote-settings-local-timestamp";
          await ContentTaskUtils.waitForCondition(
            () => content.document.querySelector(sel)?.innerText
          );
          return content.document.querySelector(sel).innerText;
        }
      );
      Assert.equal(
        localTimestamp,
        '"1715698176626"',
        "Rendered the local timestamp"
      );
    }
  );

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});
