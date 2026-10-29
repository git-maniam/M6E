"use strict";

const { AppMenuNotifications } = ChromeUtils.importESModule(
  "resource://gre/modules/AppMenuNotifications.sys.mjs"
);

registerCleanupFunction(() => {
  AppMenuNotifications.removeNotification(/.*/);
});

add_task(async function testBadgeOnlyNotification() {
  is(
    PanelUI.menuButton.hasAttribute("badge-status"),
    false,
    "No badge is shown initially."
  );

  AppMenuNotifications.showBadgeOnlyNotification("addon-installed");
  is(
    PanelUI.notificationPanel.state,
    "closed",
    "A badge-only notification does not open the doorhanger."
  );
  is(
    PanelUI.menuButton.getAttribute("badge-status"),
    "addon-installed",
    "The badge reflects the notification."
  );

  AppMenuNotifications.removeNotification("addon-installed");
  is(
    PanelUI.menuButton.hasAttribute("badge-status"),
    false,
    "The badge is removed with the notification."
  );
});

add_task(async function testNewestBadgeWins() {
  AppMenuNotifications.showBadgeOnlyNotification("addon-installed");
  AppMenuNotifications.showBadgeOnlyNotification("theme-installed");
  is(
    PanelUI.menuButton.getAttribute("badge-status"),
    "theme-installed",
    "The most recent notification is badged."
  );

  AppMenuNotifications.removeNotification("theme-installed");
  is(
    PanelUI.menuButton.getAttribute("badge-status"),
    "addon-installed",
    "The older notification is badged once the newer one is removed."
  );

  AppMenuNotifications.removeNotification("addon-installed");
  is(
    PanelUI.menuButton.hasAttribute("badge-status"),
    false,
    "The badge is removed with the last notification."
  );
});

add_task(async function testBadgeOnlyNotificationIsActive() {
  is(
    AppMenuNotifications.activeNotification,
    null,
    "No notification is active initially."
  );
  AppMenuNotifications.showBadgeOnlyNotification("addon-installed");
  is(
    AppMenuNotifications.activeNotification.id,
    "addon-installed",
    "A badge-only notification is the active notification."
  );
  AppMenuNotifications.removeNotification("addon-installed");
  is(
    AppMenuNotifications.activeNotification,
    null,
    "No notification is active after removal."
  );
});
