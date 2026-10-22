/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("push", e => {
  e.waitUntil(
    (async () => {
      await self.registration.showNotification("M6E push test", {
        body: "Delivered at " + new Date().toISOString(),
        tag: "m6e-" + Date.now(),
      });
      for (const c of await self.clients.matchAll({
        includeUncontrolled: true,
      })) {
        c.postMessage("push received");
      }
    })()
  );
});
