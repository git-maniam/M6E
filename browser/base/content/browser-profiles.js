/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var gProfiles = {
  async init() {
    this.copyProfile = this.copyProfile.bind(this);
    this.createNewProfile = this.createNewProfile.bind(this);
    this.handleCommand = this.handleCommand.bind(this);
    this.launchProfile = this.launchProfile.bind(this);
    this.manageProfiles = this.manageProfiles.bind(this);
    this.onPopupShowing = this.onPopupShowing.bind(this);
    this.toggleProfileMenus = this.toggleProfileMenus.bind(this);
    this.updateView = this.updateView.bind(this);

    this.bundle = Services.strings.createBundle(
      "chrome://browser/locale/browser.properties"
    );

    this.profilesButton = PanelMultiView.getViewNode(
      document,
      "appMenu-profiles-button"
    );
    this.appMenuCreateProfileButton = PanelMultiView.getViewNode(
      document,
      "appMenu-create-profile-button"
    );
    this.subview = PanelMultiView.getViewNode(document, "PanelUI-profiles");
    this.subview.addEventListener("command", this.handleCommand);

    PanelUI.mainView.addEventListener("ViewShowing", () =>
      this._onPanelShowing(this.profilesButton)
    );

    this.profilesButton.addEventListener("command", this.handleCommand);
    this.appMenuCreateProfileButton.addEventListener(
      "command",
      this.handleCommand
    );

    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-create-profile"
    ).addEventListener("command", this.handleCommand);

    // moz-button emits "click" rather than "command".
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-create-profile-confirm-button"
    ).addEventListener("click", this.handleCommand);

    // M6E: without a support site, the "Learn more" button is hidden.
    if (!Services.prefs.getStringPref("app.support.baseURL", "")) {
      PanelMultiView.getViewNode(
        document,
        "PanelUI-fxa-menu-create-profile-learn-more-button"
      ).hidden = true;
    }

    this.toggleProfileMenus(SelectableProfileService?.isEnabled);

    if (SelectableProfileService) {
      let listener = (event, isEnabled) => this.toggleProfileMenus(isEnabled);

      SelectableProfileService.on("enableChanged", listener);
      window.addEventListener("unload", () =>
        SelectableProfileService.off("enableChanged", listener)
      );
    }
  },

  toggleProfileMenus(isEnabled) {
    let profilesMenu = document.getElementById("profiles-menu");
    profilesMenu.hidden = !isEnabled;
  },

  async _onPanelShowing(profilesButton) {
    if (!SelectableProfileService?.isEnabled) {
      profilesButton.hidden = true;
      this.appMenuCreateProfileButton.hidden = true;
      return;
    }

    // If the feature is preffed on, but we haven't created profiles yet, the
    // service will not be initialized.
    let profiles = SelectableProfileService.initialized
      ? await SelectableProfileService.getAllProfiles()
      : [];
    if (!profiles.length) {
      profilesButton.hidden = true;
      this.appMenuCreateProfileButton.hidden = false;
      return;
    }
    this.appMenuCreateProfileButton.hidden = true;

    profilesButton.hidden = false;

    let { themeBg, themeFg } = SelectableProfileService.currentProfile.theme;
    profilesButton.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
    profilesButton.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
    profilesButton.setAttribute(
      "label",
      SelectableProfileService.currentProfile.name
    );
    profilesButton.setAttribute(
      "image",
      await SelectableProfileService.currentProfile.getAvatarURL(16)
    );
  },

  /**
   * Draws the menubar panel contents.
   */
  async onPopupShowing() {
    let menuPopup = document.getElementById("menu_ProfilesPopup");
    let profiles = await SelectableProfileService.getAllProfiles();
    let currentProfile = SelectableProfileService.currentProfile;
    let insertionPoint = document.getElementById("menu_newProfile");
    let existingItems = [
      ...menuPopup.querySelectorAll(":scope > menuitem[profileid]"),
    ];
    for (let profile of profiles) {
      let menuitem = existingItems.shift();
      let isNewItem = !menuitem;
      if (isNewItem) {
        menuitem = document.createXULElement("menuitem");
        menuitem.classList.add("menuitem-iconic", "menuitem-iconic-profile");
        menuitem.setAttribute("command", "Profiles:LaunchProfile");
      }
      let { themeBg, themeFg } = profile.theme;
      menuitem.setAttribute("profileid", profile.id);
      menuitem.setAttribute("image", await profile.getAvatarURL(48));
      menuitem.style.setProperty("--menu-profiles-theme-bg", themeBg);
      menuitem.style.setProperty("--menu-profiles-theme-fg", themeFg);

      if (profile.id === currentProfile.id) {
        menuitem.classList.add("current");
        menuitem.setAttribute("data-l10n-id", "menu-profiles-current");
        menuitem.setAttribute(
          "data-l10n-args",
          JSON.stringify({ profileName: profile.name })
        );
      } else {
        menuitem.classList.remove("current");
        menuitem.removeAttribute("data-l10n-id");
        menuitem.removeAttribute("data-l10n-args");
        menuitem.setAttribute("label", profile.name);
      }

      if (isNewItem) {
        menuPopup.insertBefore(menuitem, insertionPoint);
      }
    }
    // If there's any old item to remove, do so now.
    for (let remaining of existingItems) {
      remaining.remove();
    }
  },

  manageProfiles() {
    return SelectableProfileService.maybeSetupDataStore().then(() => {
      toOpenWindowByType(
        "about:profilemanager",
        "about:profilemanager",
        "chrome,resizable,toolbar,centerscreen"
      );
    });
  },

  copyProfile() {
    const profile = SelectableProfileService.currentProfile;
    SelectableProfileService.maybeSetupDataStore().then(() => {
      profile.copyProfile();
    });
  },

  createNewProfile(source) {
    SelectableProfileService.createNewProfile(true, null, source);
  },

  updateView(target) {
    const closestPanelView = target.closest("panelview");
    const panelView = closestPanelView
      ? PanelView.forNode(closestPanelView)
      : null;
    const wasKeyboardActivation = panelView?._doingKeyboardActivation ?? false;
    this.populateSubView();
    if (panelView && wasKeyboardActivation) {
      panelView._doingKeyboardActivation = true;
    }
    PanelUI.showSubView("PanelUI-profiles", target);
  },

  launchProfile(aEvent) {
    SelectableProfileService.getProfile(
      aEvent.target.getAttribute("profileid")
    ).then(profile => {
      SelectableProfileService.launchInstance(profile);
    });
  },

  async openTabsInProfile(aEvent, tabsToOpen) {
    let profile = await SelectableProfileService.getProfile(
      aEvent.target.getAttribute("profileid")
    );
    SelectableProfileService.launchInstance(
      profile,
      tabsToOpen.map(tab => tab.linkedBrowser.currentURI.spec)
    );
  },

  async handleCommand(aEvent) {
    switch (aEvent.target.id) {
      /* App menu button events */
      case "appMenu-profiles-button": {
        this.updateView(aEvent.target);
        break;
      }
      case "appMenu-create-profile-button": {
        aEvent.stopPropagation();
        PanelUI.showSubView("PanelUI-fxa-menu-create-profile", aEvent.target);
        break;
      }
      case "PanelUI-fxa-menu-create-profile-confirm-button": {
        this.createNewProfile("profiles-panel");
        break;
      }
      case "PanelUI-fxa-menu-create-profile-learn-more-button": {
        if (!Services.prefs.getStringPref("app.support.baseURL", "")) {
          break;
        }
        openTrustedLinkIn(
          "https://support.mozilla.org/kb/profile-management",
          "tab"
        );
        break;
      }
      case "PanelUI-fxa-menu-create-profile-copy-button": {
        this.copyProfile();
        break;
      }
      case "PanelUI-fxa-menu-create-profile-manage-button": {
        this.manageProfiles();
        break;
      }
      /* Subpanel events */
      case "profiles-appmenu-back-button": {
        aEvent.target.closest("panelview").panelMultiView.goBack();
        aEvent.target.blur();
        break;
      }
      case "profiles-edit-this-profile-button": {
        openTrustedLinkIn("about:editprofile", "tab");
        break;
      }
      case "profiles-manage-profiles-button": {
        this.manageProfiles();
        break;
      }
      case "profiles-copy-profile-button": {
        this.copyProfile();
        break;
      }
      case "profiles-create-profile-button": {
        this.createNewProfile("profiles-panel");
        break;
      }

      /* Menubar events */
      case "Profiles:CreateProfile": {
        this.createNewProfile("main-menu");
        break;
      }
      case "Profiles:ManageProfiles": {
        this.manageProfiles();
        break;
      }
      case "Profiles:LaunchProfile": {
        this.launchProfile(aEvent.sourceEvent);
        break;
      }
      case "Profiles:MoveTabsToProfile": {
        let tabs;
        if (TabContextMenu.contextTab.multiselected) {
          tabs = gBrowser.selectedTabs;
        } else {
          tabs = [TabContextMenu.contextTab];
        }
        this.openTabsInProfile(aEvent.sourceEvent, tabs);
        break;
      }
    }
    /* Subpanel profile events */
    if (
      aEvent.target.classList.contains("profile-item") &&
      aEvent.target.hasAttribute("profileid") &&
      aEvent.target.getAttribute("profileid") !==
        String(SelectableProfileService.currentProfile?.id)
    ) {
      this.launchProfile(aEvent);
    }
  },

  /**
   * Inserts the subpanel contents for the PanelUI subpanel.
   *
   *   @param {SelectableProfile|null} [displayProfile]
   *   The profile to display in the subview. If null, falls back to the
   *   current profile.
   */
  async populateSubView(displayProfile = null) {
    let profiles = [];
    let currentProfile = null;

    if (SelectableProfileService.initialized) {
      profiles = await SelectableProfileService.getAllProfiles();
      currentProfile = SelectableProfileService.currentProfile;
    }

    const targetProfile = displayProfile ?? currentProfile;
    const showProfileInfo = displayProfile !== null || profiles.length >= 1;

    let subview = PanelMultiView.getViewNode(document, "PanelUI-profiles");

    let backButton = PanelMultiView.getViewNode(
      document,
      "profiles-appmenu-back-button"
    );
    backButton.setAttribute(
      "aria-label",
      this.bundle.GetStringFromName("panel.back")
    );
    backButton.style.fill = "var(--appmenu-profiles-theme-fg, currentColor)";

    const subviewBody = subview.querySelector(".panel-subview-body");
    subviewBody.hidden = !targetProfile;

    let currentProfileCard = PanelMultiView.getViewNode(
      document,
      "current-profile"
    );
    currentProfileCard.hidden = !targetProfile;

    let profilesHeader = PanelMultiView.getViewNode(
      document,
      "PanelUI-profiles-header"
    );

    let editThisProfileButton = PanelMultiView.getViewNode(
      document,
      "profiles-edit-this-profile-button"
    );
    if (!editThisProfileButton) {
      editThisProfileButton = document.createXULElement("toolbarbutton");
      editThisProfileButton.id = "profiles-edit-this-profile-button";
      editThisProfileButton.classList.add("subviewbutton");
      editThisProfileButton.setAttribute(
        "data-l10n-id",
        "appmenu-edit-this-profile"
      );
    }

    // Automatically created by PanelMultiView.
    const headerSeparator = profilesHeader.nextElementSibling;
    let footerSeparator = PanelMultiView.getViewNode(
      document,
      "footer-separator"
    );
    if (!footerSeparator) {
      footerSeparator = document.createXULElement("toolbarseparator");
      footerSeparator.id = "footer-separator";
    }

    let createProfileButton = PanelMultiView.getViewNode(
      document,
      "profiles-create-profile-button"
    );
    if (!createProfileButton) {
      createProfileButton = document.createXULElement("toolbarbutton");
      createProfileButton.id = "profiles-create-profile-button";
      createProfileButton.classList.add(
        "subviewbutton",
        "panel-subview-footer-button"
      );
      createProfileButton.setAttribute(
        "data-l10n-id",
        "appmenu-create-profile2"
      );
    }

    let copyProfileButton = PanelMultiView.getViewNode(
      document,
      "profiles-copy-profile-button"
    );

    if (!copyProfileButton) {
      copyProfileButton = document.createXULElement("toolbarbutton");
      copyProfileButton.id = "profiles-copy-profile-button";
      copyProfileButton.classList.add("subviewbutton");
      copyProfileButton.setAttribute("data-l10n-id", "appmenu-copy-profile");
    }

    let manageProfilesButton = PanelMultiView.getViewNode(
      document,
      "profiles-manage-profiles-button"
    );

    if (!manageProfilesButton) {
      manageProfilesButton = document.createXULElement("toolbarbutton");
      manageProfilesButton.id = "profiles-manage-profiles-button";
      manageProfilesButton.classList.add("subviewbutton");
      manageProfilesButton.setAttribute(
        "data-l10n-id",
        "appmenu-manage-profiles"
      );
    }

    if (targetProfile) {
      let { themeBg, themeFg } = targetProfile.theme;
      subview.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
      subview.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
      profilesHeader.style.backgroundColor = "var(--appmenu-profiles-theme-bg)";
      profilesHeader.style.color = "var(--appmenu-profiles-theme-fg)";

      let headerText = PanelMultiView.getViewNode(
        document,
        "profiles-header-content"
      );
      headerText.textContent = targetProfile.name;

      let profileIconEl = PanelMultiView.getViewNode(
        document,
        "profile-icon-image"
      );
      currentProfileCard.style.setProperty(
        "--appmenu-profiles-theme-bg",
        themeBg
      );
      currentProfileCard.style.setProperty(
        "--appmenu-profiles-theme-fg",
        themeFg
      );

      profileIconEl.style.listStyleImage = `url(${await targetProfile.getAvatarURL(80)})`;
    } else {
      profilesHeader.removeAttribute("style");
    }

    if (showProfileInfo) {
      headerSeparator.hidden = true;
      editThisProfileButton.hidden = false;
      subview.appendChild(editThisProfileButton);
      subview.appendChild(copyProfileButton);

      for (let item of subview.querySelectorAll(".profiles-subview-item")) {
        item.remove();
      }
      PanelMultiView.getViewNode(
        document,
        "profiles-subview-list-start-separator"
      )?.remove();
      PanelMultiView.getViewNode(
        document,
        "profiles-subview-list-header"
      )?.remove();
      PanelMultiView.getViewNode(document, "profiles-subview-list")?.remove();

      let hasOtherProfiles = false;
      if (displayProfile === null) {
        let profilesListStartSeparator =
          document.createXULElement("toolbarseparator");
        profilesListStartSeparator.id = "profiles-subview-list-start-separator";

        subview.appendChild(profilesListStartSeparator);

        let profilesListHeader = document.createXULElement("label");
        profilesListHeader.id = "profiles-subview-list-header";
        profilesListHeader.classList.add("subview-subheader");
        profilesListHeader.setAttribute("crop", "end");
        profilesListHeader.setAttribute(
          "data-l10n-id",
          "appmenu-other-profiles"
        );
        subview.appendChild(profilesListHeader);

        let profilesList = document.createXULElement("vbox");
        profilesList.id = "profiles-subview-list";
        subview.appendChild(profilesList);

        for (let profile of profiles) {
          if (profile.id === targetProfile?.id) {
            continue;
          }
          let btn = document.createXULElement("toolbarbutton");
          btn.classList.add(
            "subviewbutton",
            "subviewbutton-iconic",
            "profile-item",
            "profiles-subview-item"
          );
          btn.setAttribute("profileid", profile.id);
          btn.setAttribute("label", profile.name);
          let { themeBg, themeFg } = profile.theme;
          btn.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
          btn.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
          btn.setAttribute("image", await profile.getAvatarURL(16));
          profilesList.appendChild(btn);
          hasOtherProfiles = true;
        }

        // With no other profiles the list is empty, so hide it and the footer
        // separator to avoid rendering two adjacent separators.
        profilesList.hidden = !hasOtherProfiles;
        profilesListHeader.hidden = !hasOtherProfiles;
      }

      footerSeparator.hidden = displayProfile === null && !hasOtherProfiles;
      subview.appendChild(footerSeparator);

      subview.appendChild(manageProfilesButton);
      subview.appendChild(createProfileButton);
    } else {
      headerSeparator.hidden = false;
      footerSeparator.hidden = true;
      editThisProfileButton.hidden = true;
      subview.insertBefore(editThisProfileButton, subviewBody);
      subview.insertBefore(createProfileButton, subviewBody);
      subview.insertBefore(copyProfileButton, subviewBody);
      subview.insertBefore(manageProfilesButton, subviewBody);
    }
  },

  async populateMoveTabMenu(menuPopup) {
    if (!SelectableProfileService.initialized) {
      return;
    }

    const profiles = await SelectableProfileService.getAllProfiles();
    const currentProfile = SelectableProfileService.currentProfile;

    const separator = document.getElementById("moveTabSeparator");
    separator.hidden = profiles.length < 2;

    let existingItems = [
      ...menuPopup.querySelectorAll(":scope > menuitem[profileid]"),
    ];

    // Place the profile items immediately after the separator, repositioning
    // reused ones too. Anchoring keeps them in their declared slot regardless of
    // any later items in the popup (e.g. Select All Tabs in the alt layout).
    let anchor = separator;
    for (let profile of profiles) {
      if (profile.id === currentProfile.id) {
        continue;
      }

      let menuitem = existingItems.shift();
      if (!menuitem) {
        menuitem = document.createXULElement("menuitem");
        menuitem.setAttribute("tbattr", "tabbrowser-multiple-visible");
        menuitem.setAttribute("data-l10n-id", "move-to-new-profile");
        menuitem.setAttribute("command", "Profiles:MoveTabsToProfile");
      }

      menuitem.disabled = false;
      menuitem.setAttribute("profileid", profile.id);
      menuitem.setAttribute(
        "data-l10n-args",
        JSON.stringify({ profileName: profile.name })
      );

      anchor.after(menuitem);
      anchor = menuitem;
    }
    // If there's any old item to remove, do so now.
    for (let remaining of existingItems) {
      remaining.remove();
    }
  },
};
