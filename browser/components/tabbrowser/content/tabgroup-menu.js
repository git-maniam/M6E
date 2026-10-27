/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
{
  const { TabMetrics } = ChromeUtils.importESModule(
    "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs"
  );
  const { TabStateFlusher } = ChromeUtils.importESModule(
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
  );
  const { ContentSharingUtils } = ChromeUtils.importESModule(
    "moz-src:///browser/components/sharing/ContentSharingUtils.sys.mjs"
  );
  class MozTabbrowserTabGroupMenu extends MozXULElement {
    static COLORS = [
      "blue",
      "purple",
      "cyan",
      "orange",
      "yellow",
      "pink",
      "green",
      "gray",
      "red",
    ];

    static MESSAGE_IDS = {
      blue: "tab-group-editor-color-selector2-blue",
      purple: "tab-group-editor-color-selector2-purple",
      cyan: "tab-group-editor-color-selector2-cyan",
      orange: "tab-group-editor-color-selector2-orange",
      yellow: "tab-group-editor-color-selector2-yellow",
      pink: "tab-group-editor-color-selector2-pink",
      green: "tab-group-editor-color-selector2-green",
      gray: "tab-group-editor-color-selector2-gray",
      red: "tab-group-editor-color-selector2-red",
    };

    static headerSection = /*html*/ `
      <html:div class="panel-header" >
        <html:h1
          id="tab-group-editor-title-create"
          class="tab-group-create-mode-only"
          data-l10n-id="tab-group-editor-title-create">
        </html:h1>
        <html:h1
          id="tab-group-editor-title-edit"
          class="tab-group-edit-mode-only"
          data-l10n-id="tab-group-editor-title-edit">
        </html:h1>
      </html:div>
    `;

    static editActions = /*html*/ `
      <html:div
        class="tab-group-edit-actions tab-group-edit-mode-only">
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_addNewTabInGroup"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-new-tab">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_moveGroupToNewWindow"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-new-window">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_shareTabGroup"
          class="subviewbutton"
          badged="true"
          data-l10n-id="tab-group-editor-action-share-group"
          hidden="">
          <html:moz-badge type="new" move-after-stack="true"></html:moz-badge>
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_copyAllLinks"
          class="subviewbutton">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_saveAndCloseGroup"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-save">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_ungroupTabs"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-ungroup">
        </toolbarbutton>
        <toolbarseparator class="tab-group-edit-mode-only" />
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_deleteGroup"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-delete">
        </toolbarbutton>
      </html:div>
    `;

    static defaultActions = /*html*/ `
      <html:moz-button-group
        class="tab-group-create-actions tab-group-create-mode-only panel-footer"
        id="tab-group-default-actions">
        <html:moz-button
          id="tab-group-editor-button-cancel"
          data-l10n-id="tab-group-editor-cancel">
        </html:moz-button>
        <html:moz-button
          type="primary"
          id="tab-group-editor-button-create"
          data-l10n-id="tab-group-editor-done">
        </html:moz-button>
      </html:moz-button-group>
    `;

    static markup = /*html*/ `
      <panel
        type="arrow"
        class="tab-group-editor-panel"
        orient="vertical"
        role="dialog"
        ignorekeys="true"
        norolluponanchor="true">

        ${this.headerSection}

        <toolbarseparator />

        <html:div
          class="tab-group-editor-name">
          <html:label
            for="tab-group-name"
            data-l10n-id="tab-group-editor-name-label">
          </html:label>
          <html:input
            id="tab-group-name"
            type="text"
            name="tab-group-name"
            value=""
            data-l10n-id="tab-group-editor-name-field"
          />
        </html:div>

        <html:div id="tab-group-main" class="panel-subview-body">
          <html:div id="tab-group-properties-and-actions">
            <html:div
              class="tab-group-editor-swatches"
              role="radiogroup"
              data-l10n-id="tab-group-editor-color-selector"
            />

            <toolbarseparator class="tab-group-edit-mode-only"/>

            ${this.editActions}

            ${this.defaultActions}

          </html:div>
        </html:div>
      </panel>
    `;

    #tabGroupPropertiesActions;
    #activeGroup;
    #cancelButton;
    #commandButtons;
    #createButton;
    #createMode;
    #keepNewlyCreatedGroup;
    #nameContainer;
    #nameField;
    #panel;
    #swatches;
    #swatchesContainer;
    #defaultActions;
    /** @type {string} */
    #initialTabGroupName;

    connectedCallback() {
      if (this._initialized) {
        return;
      }

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();

      this._initialized = true;

      this.#cancelButton = this.querySelector(
        "#tab-group-editor-button-cancel"
      );
      this.#createButton = this.querySelector(
        "#tab-group-editor-button-create"
      );
      this.#panel = this.querySelector("panel");
      this.#nameField = this.querySelector("#tab-group-name");
      this.#nameContainer = this.querySelector(".tab-group-editor-name");
      this.#panel.addEventListener("click", e => {
        if (e.target !== this.#nameField) {
          this.#nameField.blur();
        }
      });
      this.#swatchesContainer = this.querySelector(
        ".tab-group-editor-swatches"
      );
      this.#defaultActions = this.querySelector("#tab-group-default-actions");
      this.#tabGroupPropertiesActions = this.querySelector(
        "#tab-group-properties-and-actions"
      );
      this.#populateSwatches();

      this.#cancelButton.addEventListener("click", () => {
        this.close(false);
      });

      this.#createButton.addEventListener("click", () => {
        this.close();
      });

      this.#nameField.addEventListener("input", () => {
        if (this.activeGroup) {
          this.activeGroup.label = this.#nameField.value;
        }
      });

      this.#commandButtons = {
        addNewTabInGroup: document.getElementById(
          "tabGroupEditor_addNewTabInGroup"
        ),
        moveGroupToNewWindow: document.getElementById(
          "tabGroupEditor_moveGroupToNewWindow"
        ),
        copyAllLinks: document.getElementById("tabGroupEditor_copyAllLinks"),
        ungroupTabs: document.getElementById("tabGroupEditor_ungroupTabs"),
        saveAndCloseGroup: document.getElementById(
          "tabGroupEditor_saveAndCloseGroup"
        ),
        deleteGroup: document.getElementById("tabGroupEditor_deleteGroup"),
        shareTabGroup: document.getElementById("tabGroupEditor_shareTabGroup"),
      };

      this.#commandButtons.addNewTabInGroup.addEventListener("command", () => {
        this.#handleNewTabInGroup();
      });

      this.#commandButtons.moveGroupToNewWindow.addEventListener(
        "command",
        () => {
          gBrowser.replaceGroupWithWindow(this.activeGroup, {
            metricsContext: gBrowser.TabMetrics.userTriggeredContext(
              gBrowser.TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU
            ),
          });
        }
      );

      this.#commandButtons.copyAllLinks.addEventListener("command", () => {
        let links = this.#getGroupLinks(this.activeGroup);
        if (links.length) {
          BrowserUtils.copyLinks(links);
        }
        Glean.tabgroup.groupInteractions.copy_all_links.add(1);
        this.close();
      });

      this.#commandButtons.ungroupTabs.addEventListener("command", () => {
        this.activeGroup.ungroupTabs(
          TabMetrics.userTriggeredContext(
            TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU
          )
        );
      });

      this.#commandButtons.saveAndCloseGroup.addEventListener("command", () => {
        this.activeGroup.saveAndClose(
          TabMetrics.userTriggeredContext(
            TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU
          )
        );
      });

      this.#commandButtons.deleteGroup.addEventListener("command", () => {
        gBrowser.removeTabGroup(this.activeGroup, {
          metricsContext: TabMetrics.userTriggeredContext(
            TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU
          ),
        });
      });

      this.#commandButtons.shareTabGroup.addEventListener("command", () => {
        ContentSharingUtils.handleShareTabGroup(this.activeGroup);
        this.close();
      });

      this.panel.addEventListener("popupshown", this);
      this.panel.addEventListener("popuphidden", this);
      this.panel.addEventListener("keypress", this);
      this.#swatchesContainer.addEventListener("change", this);
    }

    #populateSwatches() {
      this.#clearSwatches();
      for (let colorCode of MozTabbrowserTabGroupMenu.COLORS) {
        let input = document.createElement("input");
        input.id = `tab-group-editor-swatch-${colorCode}`;
        input.type = "radio";
        input.name = "tab-group-color";
        input.value = colorCode;
        let label = document.createElement("label");
        label.classList.add("tab-group-editor-swatch");
        label.setAttribute(
          "data-l10n-id",
          MozTabbrowserTabGroupMenu.MESSAGE_IDS[colorCode]
        );
        label.htmlFor = input.id;
        label.style.setProperty(
          "--tabgroup-swatch-color",
          `var(--tab-group-${colorCode})`
        );
        label.style.setProperty(
          "--tabgroup-swatch-color-invert",
          `var(--tab-group-${colorCode}-invert)`
        );
        this.#swatchesContainer.append(input, label);
        this.#swatches.push(input);
      }
    }

    #clearSwatches() {
      this.#swatchesContainer.innerHTML = "";
      this.#swatches = [];
    }

    get createMode() {
      return this.#createMode;
    }

    set createMode(enableCreateMode) {
      this.#panel.classList.toggle(
        "tab-group-editor-mode-create",
        enableCreateMode
      );
      this.#panel.setAttribute(
        "aria-labelledby",
        enableCreateMode
          ? "tab-group-editor-title-create"
          : "tab-group-editor-title-edit"
      );
      this.#commandButtons.copyAllLinks.hidden = enableCreateMode;
      this.#createMode = enableCreateMode;
    }

    get activeGroup() {
      return this.#activeGroup;
    }

    set activeGroup(group = null) {
      this.#activeGroup = group;
      this.#nameField.value = group ? group.label : "";
      this.#swatches.forEach(node => {
        if (group && node.value == group.color) {
          node.checked = true;
        } else {
          node.checked = false;
        }
      });
    }

    get nextUnusedColor() {
      let usedColors = [];
      gBrowser.getAllTabGroups().forEach(group => {
        usedColors.push(group.color);
      });
      let color = MozTabbrowserTabGroupMenu.COLORS.find(
        colorCode => !usedColors.includes(colorCode)
      );
      if (!color) {
        // if all colors are used, pick one randomly
        let randomIndex = Math.floor(
          Math.random() * MozTabbrowserTabGroupMenu.COLORS.length
        );
        color = MozTabbrowserTabGroupMenu.COLORS[randomIndex];
      }
      return color;
    }

    get panel() {
      return this.children[0];
    }

    get #panelPosition() {
      if (gBrowser.tabContainer.verticalMode) {
        return SidebarController._positionStart
          ? "topleft topright"
          : "topright topleft";
      }
      return "bottomleft topleft";
    }

    openCreateModal(group) {
      this.activeGroup = group;
      this.createMode = true;
      this.#defaultActions.hidden = false;

      this.#maybeUpdateLayoutForNova();

      this.#panel.openPopup(group.firstChild, {
        position: this.#panelPosition,
      });
    }

    openEditModal(group) {
      this.activeGroup = group;
      this.createMode = false;
      this.#defaultActions.hidden = true;

      this.#maybeUpdateLayoutForNova();

      this.#panel.openPopup(group.firstChild, {
        position: this.#panelPosition,
      });
      document.getElementById("tabGroupEditor_moveGroupToNewWindow").disabled =
        gBrowser.openTabs.length == this.activeGroup?.tabs.length;
      let linkCount = this.#getGroupLinks(this.activeGroup).length;
      document.l10n.setAttributes(
        this.#commandButtons.copyAllLinks,
        "tab-group-editor-action-copy-links",
        { linkCount }
      );
      this.#commandButtons.copyAllLinks.disabled = !linkCount;
      this.#maybeDisableOrHideSaveButton();
    }

    #maybeUpdateLayoutForNova() {
      const isNovaEnabled = Services.prefs.getBoolPref(
        "browser.nova.enabled",
        false
      );
      if (isNovaEnabled) {
        this.#nameContainer.before(this.#swatchesContainer);
      } else {
        this.#tabGroupPropertiesActions.prepend(this.#swatchesContainer);
      }
    }

    #maybeDisableOrHideSaveButton() {
      const saveAndCloseGroup = document.getElementById(
        "tabGroupEditor_saveAndCloseGroup"
      );
      if (PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)) {
        saveAndCloseGroup.hidden = true;
        return;
      }

      let flushes = [];
      this.activeGroup.tabs.forEach(tab => {
        flushes.push(TabStateFlusher.flush(tab.linkedBrowser));
      });
      Promise.allSettled(flushes).then(() => {
        // `this.activeGroup` could be no longer available if the menu was closed
        // since starting the tab state flushes.
        if (this.activeGroup?.tabs) {
          saveAndCloseGroup.disabled = !SessionStore.shouldSaveTabsToGroup(
            this.activeGroup.tabs
          );
        }
      });
    }

    close(keepNewlyCreatedGroup = true) {
      if (this.createMode) {
        this.#keepNewlyCreatedGroup = keepNewlyCreatedGroup;
      }
      this.#panel.hidePopup();
    }

    on_popupshown() {
      if (this.createMode) {
        this.#keepNewlyCreatedGroup = true;
      }
      this.#initialTabGroupName = this.activeGroup?.label;
      this.#nameField.focus();

      for (const button of Object.values(this.#commandButtons)) {
        button.tooltipText = button.label;
      }

      this.#commandButtons.shareTabGroup.hidden =
        !ContentSharingUtils.isEnabled;
    }

    on_popuphidden() {
      if (this.createMode) {
        if (this.#keepNewlyCreatedGroup) {
          this.dispatchEvent(
            new CustomEvent("TabGroupCreateDone", { bubbles: true })
          );
        } else {
          this.activeGroup.ungroupTabs(
            TabMetrics.userTriggeredContext(
              TabMetrics.METRIC_SOURCE.CANCEL_TAB_GROUP_CREATION
            )
          );
        }
      }
      if (this.activeGroup?.label != this.#initialTabGroupName) {
        Glean.tabgroup.groupInteractions.rename.add(1);
      }
      this.activeGroup = null;
    }

    on_keypress(event) {
      if (event.defaultPrevented) {
        // The event has already been consumed inside of the panel.
        return;
      }

      switch (event.keyCode) {
        case KeyEvent.DOM_VK_ESCAPE:
          this.close(false);
          break;
        case KeyEvent.DOM_VK_RETURN:
          // When focus is on a button, we need to let that handle the Enter key,
          // which should ultimately close the panel as well.
          if (
            event.target.localName != "toolbarbutton" &&
            event.target.localName != "moz-button"
          ) {
            this.close();
          }
          break;
      }
    }

    /**
     * change handler for color input
     */
    on_change(aEvent) {
      if (aEvent.target.name != "tab-group-color") {
        return;
      }
      if (this.activeGroup) {
        this.activeGroup.color = aEvent.target.value;
        Glean.tabgroup.groupInteractions.change_color.add(1);
      }
    }

    async #handleNewTabInGroup() {
      let lastTab = this.activeGroup?.tabs.at(-1);
      let onTabOpened = async aEvent => {
        this.activeGroup?.addTabs([aEvent.target]);
        this.close();
        window.removeEventListener("TabOpen", onTabOpened);
      };
      window.addEventListener("TabOpen", onTabOpened);
      // The tab group menu can be invoked on a window that isn't the OS-level
      // frontmost window (most reproducibly on macOS in a multi-monitor
      // setup). Raise the window so the new tab's focusUrlBar request can
      // actually land OS keyboard focus on the address bar. Bug 2039674
      // tracks routing this through URILoadingHelper instead.
      window.focus();
      gBrowser.addAdjacentNewTab(lastTab);
    }

    /**
     * @param {MozTabbrowserTabGroup} group
     * @returns {Array<{url: string, title: string}>}
     */
    #getGroupLinks(group) {
      let links = [];
      for (let tab of group.tabs) {
        let browser = tab.linkedBrowser;
        let shareableURL = BrowserUtils.getShareableURL(browser.currentURI);
        if (shareableURL) {
          links.push({
            url: gURLBar.makeURIReadable(shareableURL).displaySpec,
            title: browser.contentTitle,
          });
        }
      }
      return links;
    }
  }

  customElements.define("tabgroup-menu", MozTabbrowserTabGroupMenu);
}
