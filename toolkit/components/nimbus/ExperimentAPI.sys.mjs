/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

export const EnrollmentType = Object.freeze({
  EXPERIMENT: "experiment",
  ROLLOUT: "rollout",
});

// featureId -> variable -> [type, fallbackPref]
const FALLBACK_PREFS = {
  urlbar: {
    actionsOnboardingTimesToShow: [
      "int",
      "browser.urlbar.quickactions.timesToShowOnboardingLabel",
    ],
    addonsFeatureGate: ["boolean", "browser.urlbar.addons.featureGate"],
    allowSearchSuggestionsForSimpleOrigins: [
      "boolean",
      "browser.urlbar.allowSearchSuggestionsForSimpleOrigins",
    ],
    ampFeatureGate: ["boolean", "browser.urlbar.amp.featureGate"],
    ampMatchingStrategy: [
      "int",
      "browser.urlbar.quicksuggest.ampMatchingStrategy",
    ],
    fakespotFeatureGate: ["boolean", "browser.urlbar.fakespot.featureGate"],
    flightStatusFeatureGate: [
      "boolean",
      "browser.urlbar.flightStatus.featureGate",
    ],
    importantDatesFeatureGate: [
      "boolean",
      "browser.urlbar.importantDates.featureGate",
    ],
    marketFeatureGate: ["boolean", "browser.urlbar.market.featureGate"],
    merinoClientVariants: ["string", "browser.urlbar.merino.clientVariants"],
    merinoEndpointURL: ["string", "browser.urlbar.merino.endpointURL"],
    merinoOhttpConfigURL: ["string", "browser.urlbar.merino.ohttpConfigURL"],
    merinoOhttpRelayURL: ["string", "browser.urlbar.merino.ohttpRelayURL"],
    merinoProviders: ["string", "browser.urlbar.merino.providers"],
    merinoTimeoutMs: ["int", "browser.urlbar.merino.timeoutMs"],
    newtabFeatureGate: ["boolean", "browser.urlbar.newtab.featureGate"],
    newtabVariantA: ["boolean", "browser.urlbar.newtab.variantA"],
    newtabVariantB: ["boolean", "browser.urlbar.newtab.variantB"],
    quickSuggestAmpTopPickCharThreshold: [
      "int",
      "browser.urlbar.quicksuggest.ampTopPickCharThreshold",
    ],
    quickSuggestEnabled: ["boolean", "browser.urlbar.quicksuggest.enabled"],
    quickSuggestImpressionCapsSponsoredEnabled: [
      "boolean",
      "browser.urlbar.quicksuggest.impressionCaps.sponsoredEnabled",
    ],
    quickSuggestImpressionCapsNonSponsoredEnabled: [
      "boolean",
      "browser.urlbar.quicksuggest.impressionCaps.nonSponsoredEnabled",
    ],
    quickSuggestMlInitDelaySeconds: [
      "int",
      "browser.urlbar.quicksuggest.mlInitDelaySeconds",
    ],
    quickSuggestMlEnabled: ["boolean", "browser.urlbar.quicksuggest.mlEnabled"],
    quickSuggestNonSponsoredIndex: [
      "int",
      "browser.urlbar.quicksuggest.nonSponsoredIndex",
    ],
    quickSuggestOnlineAvailable: [
      "boolean",
      "browser.urlbar.quicksuggest.online.available",
    ],
    quickSuggestRankingMode: [
      "string",
      "browser.urlbar.quicksuggest.rankingMode",
    ],
    quickSuggestSettingsUi: ["int", "browser.urlbar.quicksuggest.settingsUi"],
    quickSuggestSponsoredIndex: [
      "int",
      "browser.urlbar.quicksuggest.sponsoredIndex",
    ],
    quickSuggestSponsoredPriority: [
      "boolean",
      "browser.urlbar.quicksuggest.sponsoredPriority",
    ],
    resultExplanationsFeatureGate: [
      "boolean",
      "browser.urlbar.resultExplanations.featureGate",
    ],
    sportsFeatureGate: ["boolean", "browser.urlbar.sports.featureGate"],
    trackerCountFeatureGate: [
      "boolean",
      "browser.urlbar.trackerCount.featureGate",
    ],
    weatherFeatureGate: ["boolean", "browser.urlbar.weather.featureGate"],
    weatherUiTreatment: ["int", "browser.urlbar.weather.uiTreatment"],
    wikipediaFeatureGate: ["boolean", "browser.urlbar.wikipedia.featureGate"],
    yelpMlEnabled: ["boolean", "browser.urlbar.yelp.mlEnabled"],
    yelpFeatureGate: ["boolean", "browser.urlbar.yelp.featureGate"],
    yelpServiceResultDistinction: [
      "boolean",
      "browser.urlbar.yelp.serviceResultDistinction",
    ],
    yelpShowLessFrequentlyCap: [
      "int",
      "browser.urlbar.yelp.showLessFrequentlyCap",
    ],
    yelpSuggestPriority: ["boolean", "browser.urlbar.yelp.priority"],
    yelpRealtimeFeatureGate: [
      "boolean",
      "browser.urlbar.yelpRealtime.featureGate",
    ],
  },
  aboutwelcome: {
    enabled: ["boolean", "browser.aboutwelcome.enabled"],
    screens: ["json", "browser.aboutwelcome.screens"],
    languageMismatchEnabled: [
      "boolean",
      "intl.multilingual.aboutWelcome.languageMismatchEnabled",
    ],
    backdrop: ["string", "browser.aboutwelcome.backdrop"],
  },
  preonboarding: {
    enabled: ["boolean", "browser.preonboarding.enabled"],
    screens: ["json", "browser.preonboarding.screens"],
    requireAction: ["boolean", "browser.preonboarding.requireAction"],
    currentVersion: ["int", "termsofuse.currentVersion"],
    minimumVersion: ["int", "termsofuse.minimumVersion"],
    firstRunURL: ["string", "datareporting.policy.firstRunURL"],
  },
  moreFromMozilla: {
    enabled: ["boolean", "browser.preferences.moreFromMozilla"],
    template: ["string", "browser.preferences.moreFromMozilla.template"],
  },
  newtab: {
    customizationMenuEnabled: [
      "boolean",
      "browser.newtabpage.activity-stream.customizationMenu.enabled",
    ],
    topSitesContileEnabled: ["boolean", "browser.topsites.contile.enabled"],
  },
  pocketNewtab: {
    contileTopsitesPositions: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.contile-topsites-positions",
    ],
    spocAdTypes: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.spocAdTypes",
    ],
    spocZoneIds: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.spocZoneIds",
    ],
    spocSiteId: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.spocSiteId",
    ],
    widgetPositions: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.widget-positions",
    ],
    hybridLayout: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.hybridLayout.enabled",
    ],
    hideCardBackground: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.hideCardBackground.enabled",
    ],
    fourCardLayout: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.fourCardLayout.enabled",
    ],
    newFooterSection: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.newFooterSection.enabled",
    ],
    hideDescriptions: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.hideDescriptions.enabled",
    ],
    hideDescriptionsRegions: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.hideDescriptionsRegions",
    ],
    compactGrid: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.compactGrid.enabled",
    ],
    compactImages: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.compactImages.enabled",
    ],
    imageGradient: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.imageGradient.enabled",
    ],
    titleLines: [
      "int",
      "browser.newtabpage.activity-stream.discoverystream.titleLines",
    ],
    descLines: [
      "int",
      "browser.newtabpage.activity-stream.discoverystream.descLines",
    ],
    readTime: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.readTime.enabled",
    ],
    newSponsoredLabel: [
      "boolean",
      "browser.newtabpage.activity-stream.discoverystream.newSponsoredLabel.enabled",
    ],
    ctaButtonSponsors: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.ctaButtonSponsors",
    ],
    ctaButtonVariant: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.ctaButtonVariant",
    ],
    regionStoriesConfig: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.region-stories-config",
    ],
    merinoProviderEndpoint: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.merino-provider.endpoint",
    ],
    regionStoriesBlock: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.region-stories-block",
    ],
    regionSpocsConfig: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.region-spocs-config",
    ],
    pocketFeedParameters: [
      "string",
      "browser.newtabpage.activity-stream.discoverystream.pocket-feed-parameters",
    ],
  },
  shellService: {
    setDefaultPDFHandler: ["boolean", "browser.shell.setDefaultPDFHandler"],
    setDefaultPDFHandlerOnlyReplaceBrowsers: [
      "boolean",
      "browser.shell.setDefaultPDFHandler.onlyReplaceBrowsers",
    ],
    setDefaultGuidanceNotifications: [
      "boolean",
      "browser.shell.setDefaultGuidanceNotifications",
    ],
  },
  upgradeDialog: {
    enabled: ["boolean", "browser.startup.upgradeDialog.enabled"],
  },
  pingsender: {
    backgroundTaskEnabled: [
      "boolean",
      "toolkit.telemetry.shutdownPingSender.backgroundtask.enabled",
    ],
  },
  mailto: {
    dualPrompt: ["boolean", "browser.mailto.dualPrompt"],
    "dualPrompt.onLocationChange": [
      "boolean",
      "browser.mailto.dualPrompt.onLocationChange",
    ],
    "dualPrompt.dismissXClickMinutes": [
      "int",
      "browser.mailto.dualPrompt.dismissXClickMinutes",
    ],
    "dualPrompt.dismissNotNowMinutes": [
      "int",
      "browser.mailto.dualPrompt.dismissNotNowMinutes",
    ],
  },
  contentRelevancy: {
    enabled: ["boolean", "toolkit.contentRelevancy.enabled"],
    ingestEnabled: ["boolean", "toolkit.contentRelevancy.ingestEnabled"],
  },
  externalLinkHandling: {
    openBehavior: ["int", "browser.link.open_newwindow.override.external"],
  },
  fireButton: {
    flow: ["string", "browser.fireButton.flow"],
  },
};

function parseJSON(value) {
  if (value) {
    try {
      return JSON.parse(value);
    } catch (e) {
      console.error(e);
    }
  }
  return null;
}

export const ExperimentAPI = {
  enabled: false,
  labsEnabled: false,
  rolloutsEnabled: false,
  studiesEnabled: false,

  init() {
    return Promise.resolve(false);
  },

  ready() {
    return Promise.resolve();
  },

  updateRecipesForFeatures() {
    return Promise.resolve();
  },

  getAllBranches() {
    return Promise.resolve(null);
  },
};

export class _ExperimentFeature {
  #callbacks = new Set();

  constructor(featureId) {
    this.featureId = featureId;
    this.prefGetters = {};
    this.fallbackPrefs = {};

    for (let [variable, [type, pref]] of Object.entries(
      FALLBACK_PREFS[featureId] ?? {}
    )) {
      this.fallbackPrefs[variable] = pref;
      XPCOMUtils.defineLazyPreferenceGetter(
        this.prefGetters,
        variable,
        pref,
        null,
        () => this.#notify(),
        type === "json" ? parseJSON : val => val
      );
    }
  }

  #notify() {
    for (let callback of [...this.#callbacks]) {
      try {
        callback(`featureUpdate:${this.featureId}`, "pref-updated");
      } catch (e) {
        console.error(e);
      }
    }
  }

  ready() {
    return Promise.resolve();
  }

  getAllVariables({ defaultValues = null } = {}) {
    return { ...this.prefGetters, ...defaultValues };
  }

  getVariable(variable) {
    return variable in this.fallbackPrefs
      ? this.prefGetters[variable]
      : undefined;
  }

  getEnrollmentMetadata() {
    return null;
  }

  getAllEnrollments() {
    return [];
  }

  getAllEnrollmentMetadata() {
    return [];
  }

  recordExposureEvent() {}

  onUpdate(callback) {
    this.#callbacks.add(callback);
  }

  offUpdate(callback) {
    this.#callbacks.delete(callback);
  }
}

const features = new Map();

export const NimbusFeatures = new Proxy(
  {},
  {
    get(target, featureId) {
      if (typeof featureId !== "string") {
        return undefined;
      }
      let feature = features.get(featureId);
      if (!feature) {
        feature = new _ExperimentFeature(featureId);
        features.set(featureId, feature);
      }
      return feature;
    },
  }
);
