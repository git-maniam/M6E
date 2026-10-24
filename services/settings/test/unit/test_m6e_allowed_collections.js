/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: only security and tracking-protection data may sync from Remote
// Settings (D2).

add_setup(() => {
  // head_settings.js allows every collection for the upstream tests.
  Utils.isAllowedCollection = M6E_IS_ALLOWED_COLLECTION;
});

add_task(function test_allowed_collections() {
  for (let [bucket, collection] of [
    ["security-state", "intermediates"],
    ["security-state-preview", "onecrl"],
    ["blocklists", "addons-bloomfilters"],
    ["main", "tracking-protection-lists"],
    ["main-preview", "query-stripping"],
  ]) {
    ok(
      Utils.isAllowedCollection(bucket, collection),
      `${bucket}/${collection} is allowed`
    );
  }
});

add_task(function test_blocked_collections() {
  for (let [bucket, collection] of [
    ["main", "search-config-v2"],
    ["main", "nimbus-desktop-experiments"],
    ["main", "password-fields"],
    ["main", "ms-images"],
    ["main", "Tracking-Protection-Lists"],
    ["main", "tracking-protection-lists-preview"],
    ["mainx", "tracking-protection-lists"],
    ["main-workspace", "tracking-protection-lists"],
    ["security-state-staging", "intermediates"],
    ["", "tracking-protection-lists"],
  ]) {
    ok(
      !Utils.isAllowedCollection(bucket, collection),
      `${bucket}/${collection} is blocked`
    );
  }
});

add_task(async function test_blocked_collection_does_not_fetch() {
  for (let [collection, shouldFetch] of [
    ["search-config-v2", false],
    ["tracking-protection-lists", true],
  ]) {
    const client = RemoteSettings(collection);
    let fetched = false;
    client._fetchChangeset = () => {
      fetched = true;
      throw new Error("Stop after the allowlist check");
    };
    await client.maybeSync(42).catch(() => {});
    Assert.equal(
      fetched,
      shouldFetch,
      `main/${collection} ${shouldFetch ? "fetches" : "never fetches"}`
    );
  }
});
