/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// XPCOMUtils and AppConstants, and overrides importESModule
// to be a no-op (which can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

import { FeatureCalloutMessages } from "resource:///modules/asrouter/FeatureCalloutMessages.sys.mjs";
import {
  WIN_OS_PIN_PROMPT_ENABLED,
  SET_DEFAULT_OS_PROMPT_ENABLED,
} from "resource:///modules/asrouter/MessagingTargetingConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "usesFirefoxSync",
  "services.sync.username"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "mobileDevices",
  "services.sync.clients.devices.mobile",
  0
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hidePrivatePin",
  "browser.startup.upgradeDialog.pinPBM.disabled",
  false
);

const L10N = new Localization([
  "branding/brand.ftl",
  "browser/newtab/onboarding.ftl",
  "toolkit/branding/brandings.ftl",
]);

const HOMEPAGE_PREF = "browser.startup.homepage";
const NEWTAB_PREF = "browser.newtabpage.enabled";
const isMSIX =
  AppConstants.platform === "win" &&
  Services.sysinfo.getProperty("hasWinPackageId", false);

const BASE_MESSAGES = () => [
  {
    id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
    template: "menu_message",
    content: {
      layout: "column",
      messageType: "default_cta",
      imageURL:
        "chrome://browser/content/asrouter/assets/fox-with-checkmark.svg",
      imageWidth: 68,
      primaryText: {
        string_id: "set-default-menu-message-row-layout-title",
      },
      secondaryText: {
        string_id: "set-default-menu-message-row-layout-subtitle",
      },
      primaryActionText: {
        string_id: "set-default-menu-message-primary-button-variant",
      },
      primaryButtonSize: "small",
      primaryAction: {
        type: "MULTI_ACTION",
        data: {
          actions: [
            {
              type: "SET_DEFAULT_BROWSER",
            },
            {
              type: "BLOCK_MESSAGE",
              data: {
                id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
              },
            },
          ],
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
        },
      },
    },
    targeting:
      "'browser.nova.enabled'|preferenceValue != true && source == 'app_menu' && os.isWindows && os.windowsVersion >= 10 && !isDefaultBrowser && !hasAttemptedSetDefault && !hasActiveEnterprisePolicies && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false",
    trigger: {
      id: "menuOpened",
    },
    groups: [],
    skip_in_tests: "menu messages need the app menu open",
  },
  {
    id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
    template: "menu_message",
    content: {
      layout: "column",
      imageURL:
        "chrome://browser/content/asrouter/assets/fox-with-checkmark.svg",
      imageWidth: 68,
      messageType: "default_cta",
      primaryText: {
        string_id: "set-default-menu-message-row-layout-title",
      },
      secondaryText: {
        string_id: "set-default-menu-message-row-layout-subtitle-variant",
      },
      primaryActionText: {
        string_id: "set-default-menu-message-primary-button-variant",
      },
      primaryButtonSize: "small",
      primaryAction: {
        type: "MULTI_ACTION",
        data: {
          actions: [
            {
              type: "SET_DEFAULT_BROWSER",
            },
            {
              type: "PIN_FIREFOX_TO_TASKBAR",
            },
            {
              type: "BLOCK_MESSAGE",
              data: {
                id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
              },
            },
          ],
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
        },
      },
    },
    targeting:
      "'browser.nova.enabled'|preferenceValue != true && source == 'app_menu' && os.isMac && !isDefaultBrowser && !hasAttemptedSetDefault && !hasActiveEnterprisePolicies && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false",
    trigger: {
      id: "menuOpened",
    },
    groups: [],
    skip_in_tests: "menu messages need the app menu open",
  },
  // Nova variant of MENU_MESSAGE_DEFAULT_CTA messaging:
  {
    id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
    template: "menu_message",
    content: {
      layout: "split",
      imageURL: "chrome://browser/content/asrouter/assets/kit-checkmark.svg",
      rtlImageURL:
        "chrome://browser/content/asrouter/assets/kit-checkmark-flipped.svg",
      imageWidth: 91,
      imageVerticalBottomOffset: -12,
      imagePosition: "bottom",
      messageType: "default_cta",
      primaryText: {
        string_id: "set-default-menu-message-split-layout-title",
      },
      secondaryText: {
        string_id: "set-default-menu-message-split-layout-subtitle",
      },
      primaryActionText: {
        string_id: "set-default-menu-message-primary-button-short-variant",
      },
      primaryAction: {
        type: "MULTI_ACTION",
        data: {
          actions: [
            {
              type: "SET_DEFAULT_BROWSER",
            },
            {
              type: "BLOCK_MESSAGE",
              data: {
                id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
              },
            },
          ],
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
        },
      },
    },
    targeting:
      "'browser.nova.enabled'|preferenceValue == true && source == 'app_menu' && os.isWindows && os.windowsVersion >= 10 && !isDefaultBrowser && !hasAttemptedSetDefault && !hasActiveEnterprisePolicies && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features'| preferenceValue != false",
    trigger: {
      id: "menuOpened",
    },
    groups: [],
    skip_in_tests: "menu messages need the app menu open",
  },
  {
    id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
    template: "menu_message",
    content: {
      layout: "split",
      imageURL: "chrome://browser/content/asrouter/assets/kit-checkmark.svg",
      rtlImageURL:
        "chrome://browser/content/asrouter/assets/kit-checkmark-flipped.svg",
      imageWidth: 91,
      imageVerticalBottomOffset: -12,
      imagePosition: "bottom",
      messageType: "default_cta",
      primaryText: {
        string_id: "set-default-menu-message-split-layout-title",
      },
      secondaryText: {
        string_id: "set-default-menu-message-split-layout-subtitle",
      },
      primaryActionText: {
        string_id: "set-default-menu-message-primary-button-short-variant",
      },
      primaryAction: {
        type: "MULTI_ACTION",
        data: {
          actions: [
            {
              type: "SET_DEFAULT_BROWSER",
            },
            {
              type: "PIN_FIREFOX_TO_TASKBAR",
            },
            {
              type: "BLOCK_MESSAGE",
              data: {
                id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
              },
            },
          ],
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
        },
      },
    },
    targeting:
      "'browser.nova.enabled'|preferenceValue == true && source == 'app_menu' && os.isMac && !isDefaultBrowser && !hasAttemptedSetDefault && !hasActiveEnterprisePolicies && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features'| preferenceValue != false",
    trigger: {
      id: "menuOpened",
    },
    groups: [],
    skip_in_tests: "menu messages need the app menu open",
  },
  {
    id: "FX_MR_106_UPGRADE",
    template: "spotlight",
    targeting: "true",
    content: {
      template: "multistage",
      id: "FX_MR_106_UPGRADE",
      transitions: true,
      modal: "tab",
      screens: [
        {
          id: "UPGRADE_PIN_FIREFOX",
          content: {
            position: "split",
            split_narrow_bkg_position: "-155px",
            image_alt_text: {
              string_id: "mr2022-onboarding-pin-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-set-default-fox-heart.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1))",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-existing-pin-header",
            },
            subtitle: {
              string_id: "mr2022-onboarding-existing-pin-subtitle",
            },
            primary_button: {
              label: {
                string_id: isMSIX
                  ? "mr2022-onboarding-pin-primary-button-label-msix"
                  : "mr2022-onboarding-pin-primary-button-label",
              },
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                    },
                    {
                      type: "PIN_FIREFOX_TO_START_MENU",
                    },
                  ],
                },
              },
            },
            checkbox: {
              label: {
                string_id: "mr2022-onboarding-existing-pin-checkbox-label",
              },
              defaultValue: true,
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                      data: {
                        privatePin: true,
                      },
                    },
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                    },
                    {
                      type: "PIN_FIREFOX_TO_START_MENU",
                    },
                  ],
                },
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_SET_DEFAULT",
          content: {
            position: "split",
            split_narrow_bkg_position: "-60px",
            image_alt_text: {
              string_id: "mr2022-onboarding-default-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-gratitude-fox-rock.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1)",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-set-default-title",
            },
            subtitle: {
              string_id: "mr2022-onboarding-set-default-subtitle",
            },
            primary_button: {
              label: {
                string_id: "mr2022-onboarding-set-default-primary-button-label",
              },
              action: {
                navigate: true,
                type: "SET_DEFAULT_BROWSER",
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_IMPORT_SETTINGS_EMBEDDED",
          content: {
            tiles: { type: "migration-wizard" },
            position: "split",
            split_narrow_bkg_position: "-42px",
            image_alt_text: {
              string_id: "mr2022-onboarding-import-image-alt",
            },
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-import-fox-house.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1)",
            progress_bar: true,
            hide_secondary_section: "responsive",
            title: {
              string_id: "onboarding-refresh-import-title",
            },
            subtitle: {
              string_id: "onboarding-refresh-import-subtitle",
            },
            migrate_start: {
              action: {},
            },
            migrate_close: {
              action: {
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_MOBILE_DOWNLOAD",
          content: {
            position: "split",
            split_narrow_bkg_position: "-160px",
            image_alt_text: {
              string_id: "mr2022-onboarding-mobile-download-image-alt",
            },
            background:
              "url('chrome://activity-stream/content/data/content/assets/backdrop-adaptive.svg') no-repeat center / cover",
            progress_bar: true,
            logo: {},
            title: {
              string_id:
                "onboarding-mobile-download-security-and-privacy-title",
            },
            subtitle: {
              string_id:
                "onboarding-mobile-download-security-and-privacy-subtitle",
            },
            hero_image: {
              url: "chrome://activity-stream/content/data/content/assets/mobile-download-qr-existing-user.svg",
            },
            cta_paragraph: {
              text: {
                string_id: "mr2022-onboarding-mobile-download-cta-text",
                string_name: "download-label",
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://www.mozilla.org/firefox/mobile/get-app/?utm_medium=firefox-desktop&utm_source=onboarding-modal&utm_campaign=mr2022&utm_content=existing-global",
                  where: "tab",
                },
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_PIN_PRIVATE_WINDOW",
          content: {
            position: "split",
            split_narrow_bkg_position: "-100px",
            image_alt_text: {
              string_id: "mr2022-onboarding-pin-private-image-alt",
            },
            progress_bar: "true",
            background:
              "url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260630170340--kit-lock-hold--ee927c95-6161-4aa0-9b3a-12dec66b7cad.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1)",
            logo: {},
            title: {
              string_id: "mr2022-upgrade-onboarding-pin-private-window-header",
            },
            subtitle: {
              string_id:
                "mr2022-upgrade-onboarding-pin-private-window-subtitle",
            },
            primary_button: {
              label: {
                string_id:
                  "mr2022-upgrade-onboarding-pin-private-window-primary-button-label",
              },
              action: {
                type: "PIN_FIREFOX_TO_TASKBAR",
                data: {
                  privatePin: true,
                },
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_DATA_RECOMMENDATION",
          content: {
            position: "split",
            split_narrow_bkg_position: "-80px",
            image_alt_text: {
              string_id: "mr2022-onboarding-privacy-segmentation-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-migration-fox-juggle.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1)",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-privacy-segmentation-title",
            },
            subtitle: {
              string_id: "mr2022-onboarding-privacy-segmentation-subtitle",
            },
            cta_paragraph: {
              text: {
                string_id: "mr2022-onboarding-privacy-segmentation-text-cta",
              },
            },
            primary_button: {
              label: {
                string_id:
                  "mr2022-onboarding-privacy-segmentation-button-primary-label",
              },
              action: {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "browser.dataFeatureRecommendations.enabled",
                    value: true,
                  },
                },
                navigate: true,
              },
            },
            additional_button: {
              label: {
                string_id:
                  "mr2022-onboarding-privacy-segmentation-button-secondary-label",
              },
              style: "secondary",
              action: {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "browser.dataFeatureRecommendations.enabled",
                    value: false,
                  },
                },
                navigate: true,
              },
            },
          },
        },
        {
          id: "UPGRADE_GRATITUDE",
          content: {
            position: "split",
            progress_bar: "true",
            split_narrow_bkg_position: "-228px",
            image_alt_text: {
              string_id: "mr2022-onboarding-gratitude-image-alt",
            },
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-kit-with-heart.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1)",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-gratitude-title",
            },
            subtitle: {
              string_id: "mr2022-onboarding-gratitude-subtitle",
            },
            primary_button: {
              label: {
                string_id: "mr2022-onboarding-gratitude-primary-button-label",
              },
              action: {
                type: "OPEN_FIREFOX_VIEW",
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-gratitude-secondary-button-label",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
  },
  {
    id: "FX_100_UPGRADE",
    template: "spotlight",
    targeting: "false",
    content: {
      template: "multistage",
      id: "FX_100_UPGRADE",
      transitions: true,
      screens: [
        {
          id: "UPGRADE_PIN_FIREFOX",
          content: {
            logo: {
              imageURL:
                "chrome://activity-stream/content/data/content/assets/heart.webp",
              height: "73px",
            },
            has_noodles: true,
            title: {
              fontSize: "36px",
              string_id: "fx100-upgrade-thanks-header",
            },
            title_style: "fancy shine",
            background:
              "url('chrome://activity-stream/content/data/content/assets/confetti.svg') top / 100% no-repeat var(--background-color-canvas)",
            subtitle: {
              string_id: "fx100-upgrade-thanks-keep-body",
            },
            primary_button: {
              label: {
                string_id: "fx100-thank-you-pin-primary-button-label",
              },
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                    },
                    {
                      type: "PIN_FIREFOX_TO_START_MENU",
                    },
                  ],
                },
              },
            },
            secondary_button: {
              label: {
                string_id: "onboarding-not-now-button-label",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
  },
  {
    id: "PB_NEWTAB_PIN_PROMO",
    template: "pb_newtab",
    type: "default",
    groups: ["pbNewtab"],
    content: {
      promoEnabled: true,
      promoType: "PIN",
      promoHeader: "fluent:about-private-browsing-pin-promo-header",
      promoImageLarge:
        "chrome://browser/content/assets/private-promo-asset.svg",
      promoLinkText: "fluent:about-private-browsing-pin-promo-link-text",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle: "fluent:about-private-browsing-pin-promo-title",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "MULTI_ACTION",
          data: {
            actions: [
              {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "browser.privateWindowSeparation.enabled",
                    value: true,
                  },
                },
              },
              {
                type: "PIN_FIREFOX_TO_TASKBAR",
                data: {
                  privatePin: true,
                },
              },
              {
                type: "BLOCK_MESSAGE",
                data: {
                  id: "PB_NEWTAB_PIN_PROMO",
                },
              },
              {
                type: "OPEN_ABOUT_PAGE",
                data: { args: "privatebrowsing", where: "current" },
              },
            ],
          },
        },
      },
    },
    priority: 3,
    frequency: {
      custom: [
        {
          cap: 3,
          period: 604800000, // Max 3 per week
        },
      ],
      lifetime: 12,
    },
    targeting:
      "doesAppNeedPrivatePin && !'browser.privateWindowRedesign.enabled'|preferenceValue",
  },
  {
    id: "PB_NEWTAB_RELAY_PROMO",
    template: "pb_newtab",
    type: "default",
    groups: ["pbNewtab"],
    content: {
      promoEnabled: true,
      promoType: "RELAY",
      promoHeader: "fluent:about-private-browsing-relay-promo-header",
      promoImageLarge: "chrome://browser/content/assets/relay-promo.svg",
      promoLinkText: "fluent:about-private-browsing-relay-promo-link-text",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle: "fluent:about-private-browsing-relay-promo-title",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "OPEN_URL",
          data: {
            args: "https://relay.firefox.com/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=pbw&utm_content=prevent-inbox-spam-global",
            where: "tabshifted",
          },
        },
      },
    },
    priority: 3,
    frequency: {
      custom: [
        {
          cap: 1,
          period: 86400000,
        },
      ],
      lifetime: 12,
    },
    targeting:
      "'browser.privateWindowRedesign.enabled'|preferenceValue && 'signon.firefoxRelay.feature'|preferenceValue != 'enabled' && 'signon.firefoxRelay.feature'|preferenceValue != 'disabled'",
  },
  {
    id: "PRIVATE_WINDOW_BASICS_SPOTLIGHT",
    template: "spotlight",
    trigger: {
      id: "privateWindowBasicsLinkClick",
    },
    targeting: "'browser.privateWindowRedesign.enabled'|preferenceValue",
    content: {
      template: "multistage",
      id: "PRIVATE_WINDOW_BASICS_SPOTLIGHT",
      modal: "tab",
      screens: [
        {
          id: "PRIVATE_WINDOW_BASICS_SCREEN",
          force_hide_steps_indicator: true,
          content: {
            position: "center",
            zap_border: true,
            zap_shadow: true,
            screen_style: {
              width: "639px",
            },
            dismiss_button: { size: "small", action: { dismiss: true } },
            title: {
              string_id: "about-private-browsing-spotlight-basics-title",
              letterSpacing: "revert",
              lineHeight: "20px",
              marginBlockStart: "30px",
              marginBlockEnd: "13px",
              fontSize: "20px",
            },
            subtitle: {
              string_id: "about-private-browsing-spotlight-basics-subtitle",
              fontSize: "14px",
              letterSpacing: "revert",
              marginInline: "30px",
              textAlign: "left",
              fontWeight: "200",
              color: "var(--text-color)",
            },
            tiles: {
              type: "single-select",
              selected: "all",
              action: {
                picker: "<event>",
              },
              data: [
                {
                  inert: true,
                  type: "backup",
                  style: {
                    border: "none",
                    width: "100%",
                    marginBlockStart: "-80px",
                  },
                  id: "all",
                  body: {
                    items: [
                      {
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-what-to-know",
                          marginBlockStart: "38px",
                          fontSize: "15px",
                          fontWeight: "600",
                          color: "var(--text-color)",
                        },
                      },
                      {
                        style: {
                          display: "list-item",
                          listStyle: "disc",
                          marginInlineStart: "30px",
                        },
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-activity-seen",
                          fontSize: "14px",
                          fontWeight: "200",
                          color: "var(--text-color)",
                          marginBlockStart: "-10px",
                        },
                      },
                      {
                        style: {
                          display: "list-item",
                          listStyle: "disc",
                          marginInlineStart: "30px",
                        },
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-bookmarks-downloads",
                          fontSize: "14px",
                          fontWeight: "200",
                          color: "var(--text-color)",
                          marginBlockStart: "-10px",
                          marginBlockEnd: "18px",
                        },
                      },
                      {
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-more-privacy",
                          fontSize: "15px",
                          fontWeight: "600",
                          color: "var(--text-color)",
                        },
                      },
                      {
                        style: {
                          display: "list-item",
                          listStyle: "disc",
                          marginInlineStart: "30px",
                        },
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-malware-alerts",
                          fontSize: "14px",
                          fontWeight: "200",
                          color: "var(--text-color)",
                          marginBlockStart: "-10px",
                        },
                      },
                      {
                        style: {
                          display: "list-item",
                          listStyle: "disc",
                          marginInlineStart: "30px",
                        },
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-no-sell-data",
                          fontSize: "14px",
                          fontWeight: "200",
                          color: "var(--text-color)",
                          marginBlockStart: "-10px",
                        },
                      },
                      {
                        style: {
                          display: "list-item",
                          listStyle: "disc",
                          marginInlineStart: "30px",
                        },
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-vpn",
                          fontSize: "14px",
                          fontWeight: "200",
                          color: "var(--text-color)",
                          marginBlockStart: "-10px",
                        },
                      },
                      {
                        style: {
                          display: "list-item",
                          listStyle: "disc",
                          marginInlineStart: "30px",
                        },
                        text: {
                          string_id:
                            "about-private-browsing-spotlight-basics-strict-tracking",
                          fontSize: "14px",
                          fontWeight: "200",
                          color: "var(--text-color)",
                          marginBlockStart: "-10px",
                        },
                      },
                    ],
                  },
                },
              ],
            },
            additional_button: {
              style: "link",
              label: {
                string_id: "about-private-browsing-spotlight-basics-learn-more",
                fontSize: "15px",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
              },
              action: {
                type: "OPEN_URL",
                navigate: true,
                data: {
                  args: "https://support.mozilla.org/%LOCALE%/kb/private-browsing-use-firefox-without-history",
                  where: "tabshifted",
                },
              },
            },
          },
        },
      ],
    },
  },
  {
    id: "RESTORE_FROM_BACKUP",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "window",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_SCREEN",
          content: {
            position: "split",
            split_content_padding_block: "166px",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-backup-fox-outside-box.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-no-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && !doesAppNeedPin && (!'browser.shell.checkDefaultBrowser'|preferenceValue || isDefaultBrowser) && !willShowDefaultPrompt && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_NEED_PIN",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "window",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_NEED_PIN",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_NEED_PIN_SCREEN",
          content: {
            position: "split",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-backup-fox-outside-box.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            subtitle: {
              string_id: "restored-from-backup-success-with-checklist-subtitle",
              paddingInline: "0 48px",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
            secondary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-secondary-button",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
            tiles: {
              type: "multiselect",
              style: {
                gap: "10px",
              },
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                  label: {
                    string_id:
                      "mr2022-onboarding-easy-setup-set-default-checkbox-label",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
                {
                  id: "checkbox-2",
                  defaultValue: true,
                  label: {
                    string_id: isMSIX
                      ? "mr2022-onboarding-pin-primary-button-label-msix"
                      : "mr2022-onboarding-pin-primary-button-label",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "PIN_FIREFOX_TO_TASKBAR",
                        },
                        {
                          type: "PIN_FIREFOX_TO_START_MENU",
                        },
                      ],
                    },
                  },
                },
              ],
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && doesAppNeedPin && 'browser.shell.checkDefaultBrowser'|preferenceValue && !isDefaultBrowser && !willShowDefaultPrompt && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "RESTORE_FROM_BACKUP_NEED_DEFAULT",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "window",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP_NEED_DEFAULT",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_SCREEN",
          content: {
            position: "split",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-backup-fox-outside-box.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            subtitle: {
              string_id: "restored-from-backup-success-with-checklist-subtitle",
              paddingInline: "0 48px",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
            secondary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-secondary-button",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
            tiles: {
              type: "multiselect",
              style: {
                gap: "10px",
              },
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                  label: {
                    string_id:
                      "mr2022-onboarding-easy-setup-set-default-checkbox-label",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
              ],
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && !doesAppNeedPin && 'browser.shell.checkDefaultBrowser'|preferenceValue && !isDefaultBrowser && !willShowDefaultPrompt && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "RESTORE_FROM_BACKUP_NEED_PIN",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "window",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP_NEED_PIN",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_NEED_PIN_SCREEN",
          content: {
            position: "split",
            background:
              "url('chrome://activity-stream/content/data/content/assets/br-backup-fox-outside-box.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            subtitle: {
              string_id: "restored-from-backup-success-with-checklist-subtitle",
              paddingInline: "0 48px",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
            secondary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-secondary-button",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
            tiles: {
              type: "multiselect",
              style: {
                gap: "10px",
              },
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                  label: {
                    string_id: isMSIX
                      ? "mr2022-onboarding-pin-primary-button-label-msix"
                      : "mr2022-onboarding-pin-primary-button-label",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "PIN_FIREFOX_TO_TASKBAR",
                        },
                        {
                          type: "PIN_FIREFOX_TO_START_MENU",
                        },
                      ],
                    },
                  },
                },
              ],
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && doesAppNeedPin && !willShowDefaultPrompt &&(!'browser.shell.checkDefaultBrowser'|preferenceValue || isDefaultBrowser) && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    // Silently pins for users Windows will itself ask to consent to pin via
    // an OS-level prompt, in lieu of the AW_EASY_SETUP pin checkbox.
    id: "PIN_FIREFOX_TASKBAR_WIN_OS_PROMPT",
    template: "action_only",
    skip_in_tests: "it silently triggers a real OS-level pin request",
    content: {
      action: {
        type: "PIN_FIREFOX_TO_TASKBAR",
      },
    },
    // `profileGroupProfileCount == 0` used to ensure this only shows on the first profile, to be replaced with "profileScope: 'single' when bug 2074258 is fixed
    targeting: `source == 'startup' && !previousSessionEnd && profileGroupProfileCount == 0 && doesAppNeedPin && ${WIN_OS_PIN_PROMPT_ENABLED} && (unhandledCampaignAction != 'PIN_FIREFOX_TO_TASKBAR') && (unhandledCampaignAction != 'PIN_AND_DEFAULT')`,
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
    priority: 5,
  },
  {
    // Silently triggers set default for users on Mac, and on Windows when
    // one-click set default isn't available (so this falls back to Windows'
    // own "Choose default apps" settings UI), in lieu of the AW_EASY_SETUP
    // default checkbox. Never fired when one-click set default IS available,
    // since that would silently rewrite the UserChoice registry with no
    // consent surface at all.
    id: "SET_DEFAULT_MAC_AND_WINDOWS_OS_PROMPT",
    template: "action_only",
    skip_in_tests: "it silently triggers a real OS-level set default request",
    content: {
      action: {
        type: "SET_DEFAULT_BROWSER",
      },
    },
    // `profileGroupProfileCount == 0` used to ensure this only shows on the first profile, to be replaced with "profileScope: 'single' when bug 2074258 is fixed
    targeting: `source == 'newtab'  && profileGroupProfileCount == 0 && !previousSessionEnd && 'browser.shell.checkDefaultBrowser'|preferenceValue && !isDefaultBrowser && ${SET_DEFAULT_OS_PROMPT_ENABLED} && (unhandledCampaignAction != 'SET_DEFAULT_BROWSER') && (unhandledCampaignAction != 'PIN_AND_DEFAULT')`,
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
    priority: 5,
  },
  {
    id: "SET_DEFAULT_BROWSER_GUIDANCE_NOTIFICATION_WIN10",
    template: "toast_notification",
    content: {
      title: {
        string_id: "default-browser-guidance-notification-title",
      },
      body: {
        string_id:
          "default-browser-guidance-notification-body-instruction-win10",
      },
      launch_action: {
        type: "OPEN_URL",
        data: {
          args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/win-set-firefox-default-browser",
          where: "tabshifted",
        },
      },
      requireInteraction: true,
      actions: [
        {
          action: "info-page",
          title: {
            string_id: "default-browser-guidance-notification-info-page",
          },
          launch_action: {
            type: "OPEN_URL",
            data: {
              args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/win-set-firefox-default-browser",
              where: "tabshifted",
            },
          },
        },
        {
          action: "dismiss",
          title: {
            string_id: "default-browser-guidance-notification-dismiss",
          },
          windowsSystemActivationType: true,
        },
      ],
      tag: "set-default-guidance-notification",
    },
    // Both Windows 10 and 11 return `os.windowsVersion == 10.0`. We limit to
    // only Windows 10 with `os.windowsBuildNumber < 22000`. We need this due to
    // Windows 10 and 11 having substantively different UX for Windows Settings.
    targeting:
      "os.isWindows && os.windowsVersion >= 10.0 && os.windowsBuildNumber < 22000",
    trigger: { id: "deeplinkedToWindowsSettingsUI" },
  },
  {
    id: "SET_DEFAULT_BROWSER_GUIDANCE_NOTIFICATION_WIN11",
    template: "toast_notification",
    content: {
      title: {
        string_id: "default-browser-guidance-notification-auto-trigger-title",
      },
      body: {
        string_id: "default-browser-guidance-notification-auto-trigger-body",
      },
      image_url:
        "chrome://browser/content/asrouter/assets/windows-guidance.gif",
      reduced_motion_image_url:
        "chrome://browser/content/asrouter/assets/windows-guidance-static.svg",
      requireInteraction: true,
      tag: "set-default-guidance-notification",
    },
    // Both Windows 10 and 11 return `os.windowsVersion == 10.0`. We limit to
    // only Windows 11 with `os.windowsBuildNumber >= 22000`. We need this due to
    // Windows 10 and 11 having substantively different UX for Windows Settings.
    targeting:
      "os.isWindows && os.windowsVersion >= 10.0 && os.windowsBuildNumber >= 22000",
    trigger: { id: "deeplinkedToWindowsSettingsUI" },
  },
  {
    id: "INFOBAR_DEFAULT_AND_PIN_87",
    groups: ["cfr"],
    content: {
      text: {
        string_id: "default-browser-notification-message",
      },
      type: "global",
      buttons: [
        {
          label: {
            string_id: "default-browser-notification-button",
          },
          action: {
            type: "PIN_AND_DEFAULT",
          },
          primary: true,
          accessKey: "P",
        },
      ],
      category: "cfrFeatures",
      bucket_id: "INFOBAR_DEFAULT_AND_PIN_87",
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "infobar",
    frequency: {
      custom: [
        {
          cap: 1,
          period: 3024000000,
        },
      ],
      lifetime: 2,
    },
    targeting:
      "(firefoxVersion >= 138 && source == 'startup' && !isDefaultBrowser && !'browser.shell.checkDefaultBrowser'|preferenceValue && currentDate|date - 'browser.shell.userDisabledDefaultCheck'|preferenceValue * 1000 >= 604800000 && isMajorUpgrade != true && platformName != 'linux' && ((currentDate|date - profileAgeCreated) / 604800000) >= 5 && !activeNotifications && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features'|preferenceValue && ((currentDate|date - profileAgeCreated) / 604800000) < 15",
  },
  {
    id: "FINISH_SETUP_CHECKLIST",
    template: "feature_callout",
    content: {
      id: "FINISH_SETUP_CHECKLIST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "FINISH_SETUP_CHECKLIST",
          anchors: [
            {
              selector: "#fxms-bmb-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
                offset_y: 4,
              },
              no_open_on_anchor: true,
            },
            {
              selector: "#FINISH_SETUP_BUTTON",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
                offset_y: 4,
              },
              no_open_on_anchor: true,
            },
          ],
          content: {
            page_event_listeners: [
              {
                params: {
                  type: "tourend",
                },
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "messaging-system-action.easyChecklist.open",
                      value: "false",
                    },
                  },
                },
              },
            ],
            position: "callout",
            title: {
              string_id: "onboarding-checklist-title",
              marginInline: "3px 40px",
              fontWeight: "600",
              fontSize: "16px",
            },
            title_logo: {
              alignment: "top",
              imageURL: "chrome://branding/content/about-logo.png",
            },
            action_checklist_subtitle: {
              string_id: "onboarding-checklist-subtitle",
            },
            tiles: {
              type: "action_checklist",
              data: [
                {
                  id: "action-checklist-set-to-default",
                  targeting: "isDefaultBrowserUncached",
                  label: {
                    string_id: "onboarding-checklist-set-default",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
                {
                  id: "action-checklist-pin-to-taskbar",
                  targeting: "!doesAppNeedPinUncached",
                  label: {
                    string_id: "onboarding-checklist-pin",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "PIN_FIREFOX_TO_TASKBAR",
                        },
                        {
                          type: "PIN_FIREFOX_TO_START_MENU",
                        },
                      ],
                    },
                  },
                },
                {
                  id: "action-checklist-import-data",
                  targeting:
                    "hasMigratedBookmarks || hasMigratedCSVPasswords || hasMigratedHistory || hasMigratedPasswords",
                  label: {
                    string_id: "onboarding-checklist-import",
                  },
                  action: {
                    type: "SHOW_MIGRATION_WIZARD",
                  },
                },
                {
                  id: "action-checklist-explore-extensions",
                  targeting:
                    "'messaging-system-action.hasOpenedExtensions'|preferenceValue || addonsInfo.hasInstalledAddons",
                  label: {
                    string_id: "onboarding-checklist-extension",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "messaging-system-action.hasOpenedExtensions",
                              value: "true",
                            },
                          },
                        },
                        {
                          type: "OPEN_URL",
                          data: {
                            args: "https://addons.mozilla.org/en-US/firefox/collections/4757633/b4d5649fb087446aa05add5f0258c3/?page=1&collection_sort=-popularity",
                            where: "current",
                          },
                        },
                      ],
                    },
                  },
                },
              ],
            },
            more_button: {
              submenu: [
                {
                  type: "action",
                  label: { string_id: "onboarding-checklist-minimize" },
                  action: {
                    type: "MULTI_ACTION",
                    dismiss: true,
                    sendDismissTelemetry: true,
                    data: {
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "messaging-system-action.easyChecklist.open",
                              value: false,
                            },
                          },
                        },
                      ],
                    },
                  },
                  id: "minimize",
                },
                {
                  type: "action",
                  label: {
                    string_id: "onboarding-checklist-remove",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    dismiss: true,
                    data: {
                      actions: [
                        {
                          type: "BLOCK_MESSAGE",
                          data: {
                            id: "FINISH_SETUP_CHECKLIST",
                          },
                        },
                        {
                          type: "DESTROY_UIWIDGET",
                          data: {
                            widget_id: "fxms-bmb-button",
                          },
                        },
                      ],
                    },
                  },
                  id: "remove_checklist",
                },
              ],
            },
            remove_checklist_button: {
              label: { string_id: "onboarding-checklist-remove-2" },
              source_id: "remove_checklist_button",
              action: {
                type: "MULTI_ACTION",
                dismiss: true,
                data: {
                  actions: [
                    {
                      type: "BLOCK_MESSAGE",
                      data: {
                        id: "FINISH_SETUP_CHECKLIST",
                      },
                    },
                    {
                      type: "DESTROY_UIWIDGET",
                      data: {
                        widget_id: "fxms-bmb-button",
                      },
                    },
                  ],
                },
              },
            },
          },
        },
      ],
    },
    priority: 3,
    targeting:
      "'messaging-system-action.easyChecklist.open' | preferenceValue == true",
    triggers: [
      {
        id: "preferenceObserver",
        params: ["messaging-system-action.easyChecklist.open"],
      },
      { id: "messagesLoaded" },
    ],
  },
  {
    id: "MULTIPROFILE_DATA_COLLECTION_CHANGED_INFOBAR",
    template: "infobar",
    targeting: "true",
    content: {
      priority: 3,
      text: { string_id: "multiprofile-data-collection-message" },
      buttons: [
        {
          label: { string_id: "multiprofile-data-collection-view-settings" },
          action: {
            type: "OPEN_PREFERENCES_PAGE",
            data: { category: "privacy-reports" },
            dismiss: true,
          },
        },
        {
          label: { string_id: "multiprofile-data-collection-dismiss" },
          action: { type: "CANCEL", dismiss: true },
        },
      ],
    },
    trigger: {
      id: "selectableProfilesUpdated",
    },
  },
  {
    id: "REFRESH_UNUSED_PROFILE_INFOBAR",
    template: "infobar",
    content: {
      type: "global",
      priority: 1,
      text: { string_id: "refresh-unused-profile-infobar-message" },
      buttons: [
        {
          label: { string_id: "refresh-profile-infobar-button" },
          action: { type: "RESET_PROFILE" },
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    skip_in_tests: "fires on startup, may interfere with other tests",
    priority: 1,
    targeting:
      "source == 'startup' && canResetProfile && !'browser.disableResetPrompt'|preferenceValue && profileLastUse && currentDate|date - profileLastUse >= 5184000000 && !activeNotifications",
  },
  {
    id: "REFRESH_REINSTALLED_PROFILE_INFOBAR",
    template: "infobar",
    content: {
      type: "global",
      priority: 1,
      text: { string_id: "refresh-reinstalled-profile-infobar-message" },
      buttons: [
        {
          label: { string_id: "refresh-profile-infobar-button" },
          action: { type: "RESET_PROFILE" },
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    skip_in_tests: "fires on startup, may interfere with other tests",
    targeting:
      "source == 'startup' && isFirefoxReinstalled && canResetProfile && !'browser.disableResetPrompt'|preferenceValue && !activeNotifications",
  },
  {
    id: "RESTORE_SESSION_STARTUP_INFOBAR",
    template: "infobar",
    content: {
      type: "tab",
      priority: 2,
      text: { string_id: "restore-session-startup-suggestion-message" },
      inline_icons: {
        icon: { imageURL: "chrome://browser/skin/menu.svg" },
      },
      buttons: [
        {
          label: { string_id: "restore-session-startup-suggestion-button" },
          primary: true,
          action: {
            type: "SELECT_AND_MARK_MENU_ITEM",
            data: {
              itemIds: ["appMenu-history-button", "appMenu-restoreSession"],
            },
          },
        },
      ],
      impression_action: {
        type: "SET_PREF",
        data: {
          pref: {
            name: "browser.startup.couldRestoreSession.count",
            value: 2,
          },
        },
      },
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    skip_in_tests: "fires on startup, may interfere with other tests",
    targeting:
      "source == 'startup' && canRestoreLastSession && !activeNotifications && 'browser.startup.couldRestoreSession.count'|preferenceValue == 1",
  },
  {
    id: "updated-privacy-notice-notification-infobar",
    groups: ["cfr"],
    template: "infobar",
    content: {
      impression_action: {
        type: "MULTI_ACTION",
        orderedExecution: true,
        data: {
          actions: [
            {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "termsofuse.firstAcceptedDate",
                  value: {
                    copyFromPref: "termsofuse.acceptedDate",
                  },
                },
              },
            },
            {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "termsofuse.acceptedDate",
                  value: {
                    timestamp: true,
                  },
                },
              },
            },
          ],
        },
      },
      text: {
        string_id: "existing-user-privacy-notice-update-message",
        args: { where: "tabshifted" },
      },
      linkUrls: {
        "privacy-notice-link": "https://www.mozilla.org/privacy/firefox/next/",
      },
      type: "tab",
      buttons: [
        {
          label: {
            string_id: "existing-user-tou-learn-more",
          },
          action: {
            type: "OPEN_URL",
            data: {
              where: "tabshifted",
              args: "https://www.mozilla.org/privacy/firefox/update/",
            },
            dismiss: false,
          },
          accessKey: "L",
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
    priority: 2,
    targeting:
      "('termsofuse.acceptedDate'|preferenceValue != '0') && (('termsofuse.acceptedDate'|preferenceValue * 1) < 1765972800000)",
  },
];

const PREONBOARDING_MESSAGES = () => [
  {
    id: "NEW_USER_TOU_ONBOARDING",
    enabled: true,
    requireAction: true,
    currentVersion: 4,
    minimumVersion: 4,
    firstRunURL: "https://www.mozilla.org/privacy/firefox/",
    screens: [
      {
        id: "TOU_ONBOARDING_LOADING",
        targeting:
          "'browser.aboutwelcome.experimentsGate.enabled'|preferenceValue && (!'browser.aboutwelcome.experimentsGate.skipSplashIfLoaded'|preferenceValue || !experimentsLoaded)",
        advance_on_experiment_load: {
          minDisplayMs: 3000,
          maxDisplayMs: 10000,
        },
        force_hide_steps_indicator: true,
        content: {
          screen_style: {
            overflow: "auto",
            display: "block",
            padding: "0",
            width: "100vw",
            height: "100vh",
          },
          main_content_style: {
            display: "none",
          },
          logo: {
            imageURL:
              "chrome://activity-stream/content/data/content/assets/splash-logo.svg",
            height: "500px",
            width: "500px",
            style: {
              display: "flex",
              justifyContent: "center",
              alignItems: "center",
              flexGrow: "1",
            },
          },
        },
      },
      {
        id: "TOU_ONBOARDING",
        force_hide_steps_indicator: true,
        content: {
          action_buttons_above_content: true,
          screen_style: {
            overflow: "auto",
            display: "block",
            padding: "40px 0 0 0",
            width: "560px",
          },
          logo: {
            imageURL: "chrome://branding/content/about-logo.png",
            height: "40px",
            width: "40px",
          },
          title: {
            string_id: "preonboarding-title",
          },
          subtitle: {
            string_id: "preonboarding-subtitle",
            paddingInline: "24px",
          },
          tiles: [
            {
              type: "link",
              id: "terms_of_use",
              header: {
                title: {
                  string_id: "preonboarding-terms-of-use-header-button-title",
                },
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://mozilla.org/about/legal/terms/firefox/?v=product",
                  where: "chromeless",
                },
              },
            },
            {
              type: "link",
              id: "privacy_notice",
              header: {
                title: {
                  string_id: "preonboarding-privacy-notice-header-button-title",
                },
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://mozilla.org/privacy/firefox/?v=product",
                  where: "chromeless",
                },
              },
            },
            {
              type: "multiselect",
              header: {
                title: {
                  string_id: "preonboarding-manage-data-header-button-title",
                },
              },
              data: [
                {
                  id: "interaction-data",
                  type: "checkbox",
                  defaultValue: true,
                  label: {
                    string_id: "preonboarding-checklist-interaction-data-label",
                  },
                  description: {
                    string_id:
                      "preonboarding-checklist-interaction-data-description",
                  },
                  action: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "datareporting.healthreport.uploadEnabled",
                        value: true,
                      },
                    },
                  },
                  uncheckedAction: {
                    type: "MULTI_ACTION",
                    data: {
                      orderedExecution: true,
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "datareporting.healthreport.uploadEnabled",
                              value: false,
                            },
                          },
                        },
                        {
                          type: "SUBMIT_ONBOARDING_OPT_OUT_PING",
                        },
                      ],
                    },
                  },
                },
              ],
            },
          ],
          primary_button: {
            label: {
              string_id: "preonboarding-primary-cta-v2",
              marginBlock: "30px 0",
              paddingBlock: "4px",
              paddingInline: "16px",
            },
            action: {
              type: "MULTI_ACTION",
              collectSelect: true,
              data: {
                orderedExecution: true,
                actions: [
                  {
                    type: "SET_TERMS_OF_USE_INTERACTED",
                  },
                ],
              },
              dismiss: true,
            },
          },
        },
      },
    ],
  },
];

// Eventually, move Feature Callout messages to their own provider
const ONBOARDING_MESSAGES = () =>
  BASE_MESSAGES().concat(FeatureCalloutMessages.getMessages());

export const OnboardingMessageProvider = {
  async getExtraAttributes() {
    const [header, button_label] = await L10N.formatMessages([
      { id: "onboarding-welcome-header" },
      { id: "onboarding-start-browsing-button-label" },
    ]);
    return { header: header.value, button_label: button_label.value };
  },

  async getMessages() {
    const messages = await this.translateMessages(await ONBOARDING_MESSAGES());
    OnboardingMessageProvider.getRestoredFromBackupMessage(messages);
    return messages;
  },

  getPreonboardingMessages() {
    return PREONBOARDING_MESSAGES();
  },

  /**
   * Fill in Nimbus `preonboarding` feature variables from the default
   * preonboarding message when preonboarding is the unconfigured default
   * (`enabled === null`) or enabled without screens. Supplied values win,
   * except nulls and empty arrays, which fall back to the default message's
   * values. Explicitly disabled (`enabled === false`) variables are returned
   * unchanged.
   *
   * @param {object} variables Nimbus `preonboarding` feature variables.
   * @return {object} the variables, merged over the default message if needed.
   */
  getPreonboardingVariablesWithDefaults(variables) {
    if (
      variables.enabled !== null &&
      !(variables.enabled && !variables.screens?.length)
    ) {
      return variables;
    }

    const preonboardingMessage = this.getPreonboardingMessages().find(
      m => m.id === "NEW_USER_TOU_ONBOARDING"
    );
    return {
      ...preonboardingMessage,
      ...Object.fromEntries(
        Object.entries(variables).filter(
          ([_, value]) =>
            value !== null && !(Array.isArray(value) && !value.length)
        )
      ),
    };
  },

  // If the user has restored from a backup, mutate the restore from backup message to appear once per backup by using the restoration timestamp as the unique message id
  getRestoredFromBackupMessage(messages) {
    const backupRestorationTimestamp = Services.prefs.getIntPref(
      "browser.backup.profile-restoration-date",
      0
    );

    if (backupRestorationTimestamp) {
      for (const msg of messages) {
        if (msg.id.startsWith("RESTORE_FROM_BACKUP")) {
          msg.id += `_${backupRestorationTimestamp}`;
          msg.content.id += `_${backupRestorationTimestamp}`;
        }
      }
    }
  },

  async getUntranslatedMessages() {
    // This is helpful for jsonSchema testing - since we are localizing in the provider
    const messages = await ONBOARDING_MESSAGES();
    return messages;
  },

  async translateMessages(messages) {
    let translatedMessages = [];
    for (const msg of messages) {
      let translatedMessage = { ...msg };

      // If the message has no content, do not attempt to translate it
      if (!translatedMessage.content) {
        translatedMessages.push(translatedMessage);
        continue;
      }

      // Translate any secondary buttons separately
      if (msg.content.secondary_button) {
        const [secondary_button_string] = await L10N.formatMessages([
          { id: msg.content.secondary_button.label.string_id },
        ]);
        translatedMessage.content.secondary_button.label =
          secondary_button_string.value;
      }
      if (msg.content.header) {
        const [header_string] = await L10N.formatMessages([
          { id: msg.content.header.string_id },
        ]);
        translatedMessage.content.header = header_string.value;
      }
      translatedMessages.push(translatedMessage);
    }
    return translatedMessages;
  },

  async _doesAppNeedPin(privateBrowsing = false) {
    const needPin = await lazy.ShellService.doesAppNeedPin(privateBrowsing);
    return needPin;
  },

  async _doesAppNeedDefault() {
    let checkDefault = Services.prefs.getBoolPref(
      "browser.shell.checkDefaultBrowser",
      false
    );
    let isDefault = await lazy.ShellService.isDefaultBrowserAsync();
    return checkDefault && !isDefault;
  },

  _shouldShowPrivacySegmentationScreen() {
    return Services.prefs.getBoolPref(
      "browser.privacySegmentation.preferences.show"
    );
  },

  _doesHomepageNeedReset() {
    return (
      Services.prefs.prefHasUserValue(HOMEPAGE_PREF) ||
      Services.prefs.prefHasUserValue(NEWTAB_PREF)
    );
  },

  async getUpgradeMessage() {
    let message = (await OnboardingMessageProvider.getMessages()).find(
      ({ id }) => id === "FX_MR_106_UPGRADE"
    );

    let { content } = message;
    // Helper to find screens and remove them where applicable.
    function removeScreens(check) {
      const { screens } = content;
      for (let i = 0; i < screens?.length; i++) {
        if (check(screens[i])) {
          screens.splice(i--, 1);
        }
      }
    }

    // Helper to prepare mobile download screen content
    function prepareMobileDownload() {
      let mobileContent = content.screens.find(
        screen => screen.id === "UPGRADE_MOBILE_DOWNLOAD"
      )?.content;

      if (!mobileContent) {
        return;
      }
      if (!lazy.BrowserUtils.sendToDeviceEmailsSupported()) {
        // If send to device emails are not supported for a user's locale,
        // remove the send to device link and update the screen text
        delete mobileContent.cta_paragraph.action;
        mobileContent.cta_paragraph.text = {
          string_id: "mr2022-onboarding-no-mobile-download-cta-text",
        };
      }
    }

    let pinScreen = content.screens?.find(
      screen => screen.id === "UPGRADE_PIN_FIREFOX"
    );
    const needPin = await this._doesAppNeedPin();
    const needDefault = await this._doesAppNeedDefault();
    const needPrivatePin =
      !lazy.hidePrivatePin && (await this._doesAppNeedPin(true));
    const showSegmentation = this._shouldShowPrivacySegmentationScreen();

    //If a user has Firefox as default remove import screen
    if (!needDefault) {
      removeScreens(screen =>
        screen.id?.startsWith("UPGRADE_IMPORT_SETTINGS_EMBEDDED")
      );
    }

    // If already pinned, convert "pin" screen to "welcome" with desired action.
    let removeDefault = !needDefault;
    // If user doesn't need pin, update screen to set "default" or "get started" configuration
    if (!needPin && pinScreen) {
      // don't need to show the checkbox
      delete pinScreen.content.checkbox;

      removeDefault = true;
      let primary = pinScreen.content.primary_button;
      if (needDefault) {
        pinScreen.id = "UPGRADE_ONLY_DEFAULT";
        pinScreen.content.subtitle = {
          string_id: "mr2022-onboarding-existing-set-default-only-subtitle",
        };
        primary.label.string_id =
          "mr2022-onboarding-set-default-primary-button-label";

        // The "pin" screen will now handle "default" so remove other "default."
        primary.action.type = "SET_DEFAULT_BROWSER";
      } else {
        pinScreen.id = "UPGRADE_GET_STARTED";
        pinScreen.content.subtitle = {
          string_id: "mr2022-onboarding-get-started-primary-subtitle",
        };
        primary.label = {
          string_id: "mr2022-onboarding-get-started-primary-button-label",
        };
        delete primary.action.type;
      }
    }

    // If a user has Firefox private pinned remove pin private window screen
    // We also remove standalone pin private window screen if a user doesn't have
    // Firefox pinned in which case the option is shown as checkbox with UPGRADE_PIN_FIREFOX screen
    if (!needPrivatePin || needPin) {
      removeScreens(screen =>
        screen.id?.startsWith("UPGRADE_PIN_PRIVATE_WINDOW")
      );
    }

    if (!showSegmentation) {
      removeScreens(screen =>
        screen.id?.startsWith("UPGRADE_DATA_RECOMMENDATION")
      );
    }

    //If privatePin, remove checkbox from pinscreen
    if (!needPrivatePin) {
      delete content.screens?.find(
        screen => screen.id === "UPGRADE_PIN_FIREFOX"
      )?.content?.checkbox;
    }

    if (removeDefault) {
      removeScreens(screen => screen.id?.startsWith("UPGRADE_SET_DEFAULT"));
    }

    // Remove mobile download screen if user has sync enabled
    if (lazy.usesFirefoxSync && lazy.mobileDevices > 0) {
      removeScreens(screen => screen.id === "UPGRADE_MOBILE_DOWNLOAD");
    } else {
      prepareMobileDownload();
    }

    return message;
  },
};
