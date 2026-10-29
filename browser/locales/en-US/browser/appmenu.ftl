# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## App Menu

appmenu-nova-switch-device-promo =
    .message = Getting a new device soon? Take { -brand-short-name } with you!

appmenu-nova-switch-device-link = How to migrate your data

appmenuitem-new-tab =
    .label = New Tab
appmenuitem-new-window =
    .label = New Window
appmenuitem-new-private-window =
    .label = New Private Window
appmenuitem-history =
  .label = History
appmenuitem-tab-groups =
  .label = Tab groups
appmenuitem-downloads =
  .label = Downloads
appmenuitem-passwords =
    .label = Passwords
appmenuitem-extensions-and-themes =
    .label = Extensions and Themes
appmenuitem-extensions =
    .label = Extensions
appmenuitem-print =
  .label = Print…
appmenuitem-find-in-page =
    .label = Find in Page…
appmenuitem-zoom =
    .value = Zoom

# This menu item opens the referral page, where the user can invite others to
# use the browser. "Share" here means recommending or referring the browser
appmenuitem-referrals =
    .label = Share { -brand-product-name }
appmenuitem-more-tools =
    .label = More Tools
# Menu combining the previous "Help" menu and report broken site
appmenuitem-help-and-report =
    .label = Help and Report
appmenuitem-exit2 =
    .label =
        { PLATFORM() ->
            [linux] Quit
           *[other] Exit
        }
appmenu-menu-button-closed2 =
    .tooltiptext = Open application menu
    .label = { -brand-short-name }
appmenu-menu-button-opened2 =
    .tooltiptext = Close application menu
    .label = { -brand-short-name }

# Settings is now used to access the browser settings across all platforms,
# instead of Options or Preferences.
appmenuitem-settings =
    .label = Settings

## Zoom and Fullscreen Controls

appmenuitem-zoom-enlarge =
  .label = Zoom In
appmenuitem-zoom-reduce =
  .label = Zoom Out
appmenuitem-fullscreen =
  .label = Full screen

## Firefox Account toolbar button and Sync panel in App menu.

appmenuitem-save-page =
    .label = Save Page As…

## The Firefox Profiler – The popup is the UI to turn on the profiler, and record
## performance profiles. To enable it go to profiler.firefox.com and click
## "Enable Profiler Menu Button".

profiler-button-dropmarker =
    .label = Open the profiler panel
    .tooltiptext = Open the profiler panel

profiler-popup-button-idle =
  .label = Profiler
  .tooltiptext = Record a performance profile

profiler-popup-button-recording =
  .label = Profiler
  .tooltiptext = The profiler is recording a profile

profiler-popup-button-capturing =
  .label = Profiler
  .tooltiptext = The profiler is capturing a profile

profiler-popup-header-text = { -profiler-brand-name }

profiler-popup-reveal-description-button =
  .aria-label = Reveal more information

profiler-popup-description-title =
  .value = Record, analyze, share

profiler-popup-description =
  Collaborate on performance issues by publishing profiles to share with your team.

profiler-popup-learn-more-button =
  .label = Learn more

profiler-popup-settings =
  .value = Settings

# This link takes the user to about:profiling, and is only visible with the Custom preset.
profiler-popup-edit-settings-button =
  .label = Edit Settings…

profiler-popup-recording-screen = Recording…

profiler-popup-start-recording-button =
  .label = Start Recording

profiler-popup-discard-button =
  .label = Discard

profiler-popup-capture-button =
  .label = Capture

profiler-popup-start-shortcut =
  { PLATFORM() ->
      [macos] ⌃⇧1
     *[other] Ctrl+Shift+1
  }

profiler-popup-capture-shortcut =
  { PLATFORM() ->
      [macos] ⌃⇧2
     *[other] Ctrl+Shift+2
  }

## Profiler presets
## They are shown in the popup's select box.

# Presets and their l10n IDs are defined in the file
# devtools/client/performance-new/shared/background.sys.mjs
# Please take care that the same values are also defined in devtools' perftools.ftl.

profiler-popup-presets-web-developer-description = Recommended preset for most web app debugging, with low overhead.
profiler-popup-presets-web-developer-label =
  .label = Web Developer

profiler-popup-presets-firefox-description = Recommended preset for profiling { -brand-shorter-name }.
profiler-popup-presets-firefox-label =
  .label = { -brand-shorter-name }

profiler-popup-presets-graphics-description = Preset for investigating graphics bugs in { -brand-shorter-name }.
profiler-popup-presets-graphics-label =
  .label = Graphics

profiler-popup-presets-media-description2 = Preset for investigating audio and video bugs in { -brand-shorter-name }.
profiler-popup-presets-media-label =
  .label = Media

profiler-popup-presets-networking-description = Preset for investigating networking bugs in { -brand-shorter-name }.
profiler-popup-presets-networking-label =
  .label = Networking

profiler-popup-presets-networking-with-logs-description = Preset for investigating networking bugs in { -brand-shorter-name }, including networking logs. These logs may contain sensitive information such as the URLs you visit.
profiler-popup-presets-networking-with-logs-label =
  .label = Networking with Logs

profiler-popup-presets-power-description = Preset for investigating power use bugs in { -brand-shorter-name }, with low overhead.
# "Power" is used in the sense of energy (electricity used by the computer).
profiler-popup-presets-power-label =
  .label = Power

profiler-popup-presets-debug-description = Preset for debugging in { -brand-shorter-name }. High overhead, do not use for performance work but use for focusing on understanding browser behavior.
profiler-popup-presets-debug-label =
  .label = Debug

profiler-popup-presets-web-compat-description = Recommended preset for debugging web compatibility issues in websites, rather than tracking performance.
profiler-popup-presets-web-compat-label =
  .label = Web Compat

profiler-popup-presets-custom-label =
  .label = Custom

## History panel

appmenu-manage-history =
    .label = Manage History

appmenu-restore-session =
    .label = Restore previous session
appmenu-clear-history =
    .label = Clear Recent History…
appmenu-recent-history-subheader = Recent history
appmenu-recently-closed-tabs =
    .label = Recently Closed Tabs
appmenu-recently-closed-windows =
    .label = Recently Closed Windows
# This allows to search through the browser's history.
appmenu-search-history =
    .label = Search History

## Help panel

# Menu header for combination of "Help" menu and report broken site
appmenu-help-and-report-header =
    .title = Help and Report
appmenu-about =
    .label = About { -brand-shorter-name }
    .accesskey = A
# This menu item opens the referral page, where the user can invite others to
# use the browser. "Share" here means recommending or referring the browser
appmenu-referrals2 =
    .label = Share { -brand-product-name }
    .accesskey = r
appmenu-get-help =
    .label = Get Help
    .accesskey = h
appmenu-help-more-troubleshooting-info =
    .label = More Troubleshooting Information
    .accesskey = t
appmenu-help-share-ideas =
    .label = Share Ideas and Feedback…
    .accesskey = S
appmenu-help-switch-device =
    .label = Switching to a new device

## appmenu-help-enter-troubleshoot-mode and appmenu-help-exit-troubleshoot-mode
## are mutually exclusive, so it's possible to use the same accesskey for both.

appmenu-help-enter-troubleshoot-mode2 =
    .label = Troubleshoot Mode…
    .accesskey = M
appmenu-help-exit-troubleshoot-mode =
    .label = Turn Troubleshoot Mode off
    .accesskey = M

## appmenu-help-report-deceptive-site and appmenu-help-not-deceptive
## are mutually exclusive, so it's possible to use the same accesskey for both.

appmenu-help-report-deceptive-site =
    .label = Report Deceptive Site…
    .accesskey = d
appmenu-help-not-deceptive =
    .label = This isn’t a deceptive site…
    .accesskey = d

## More Tools

appmenu-customizetoolbar =
    .label = Customize Toolbar…
appmenu-edit-pdf =
    .label = Edit PDF…

appmenu-developer-tools-subheader = Browser tools
appmenu-developer-tools-extensions =
    .label = Extensions for Developers
appmenuitem-report-broken-site =
  .label = Report Broken Site

## Profiles panel

appmenu-profiles-header = Profiles
appmenu-all-profiles =
    .label = All Profiles

appmenu-profiles-2 =
    .label = Profiles
appmenu-other-profiles = Other profiles
appmenu-manage-profiles =
    .label = Manage Profiles
appmenu-copy-profile =
    .label = Copy This Profile
appmenu-create-profile2 =
    .label = Create a New Profile
appmenu-edit-profile =
    .aria-label = Edit profile
appmenu-edit-this-profile =
    .label = Edit This Profile
appmenu-profile-current-in-use = Current profile in use

fxa-menu-create-profile-subpanel =
    .title = Create a new profile

fxa-menu-create-profile-heading = Level up your browsing with a new profile

fxa-menu-create-profile-description = Keep your bookmarks, passwords, and history separate for work and personal browsing.

fxa-menu-create-profile-confirm =
    .label = Create new profile

fxa-menu-create-profile-learn-more =
    .label = What Are Profiles?
