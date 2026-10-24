/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const lazy = XPCOMUtils.declareLazy({
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  NLP: "resource://gre/modules/NLP.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ResetProfile: "resource://gre/modules/ResetProfile.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  UrlbarProviderGlobalActions:
    "moz-src:///browser/components/urlbar/UrlbarProviderGlobalActions.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
});

const EN_LOCALE_MATCH = /^en(-.*)$/;

// The search "documents" corresponding to each tip type.
const DOCUMENTS = {
  clear: [
    "cache firefox",
    "clear cache firefox",
    "clear cache in firefox",
    "clear cookies firefox",
    "clear firefox cache",
    "clear history firefox",
    "cookies firefox",
    "delete cookies firefox",
    "delete history firefox",
    "firefox cache",
    "firefox clear cache",
    "firefox clear cookies",
    "firefox clear history",
    "firefox cookie",
    "firefox cookies",
    "firefox delete cookies",
    "firefox delete history",
    "firefox history",
    "firefox not loading pages",
    "history firefox",
    "how to clear cache",
    "how to clear history",
  ],
  refresh: [
    "firefox crashing",
    "firefox keeps crashing",
    "firefox not responding",
    "firefox not working",
    "firefox refresh",
    "firefox slow",
    "how to reset firefox",
    "refresh firefox",
    "reset firefox",
  ],
};

/**
 * A node in the QueryScorer's phrase tree.
 */
// eslint-disable-next-line no-shadow
class Node {
  constructor(word) {
    this.word = word;
    this.documents = new Set();
    this.childrenByWord = new Map();
  }
}

/**
 * This class scores a query string against sets of phrases.  To refer to a
 * single set of phrases, we borrow the term "document" from search engine
 * terminology.  To use this class, first add your documents with `addDocument`,
 * and then call `score` with a query string.  `score` returns a sorted array of
 * document-score pairs.
 *
 * The scoring method is fairly simple and is based on Levenshtein edit
 * distance.  Therefore, lower scores indicate a better match than higher
 * scores.  In summary, a query matches a phrase if the query starts with the
 * phrase.  So a query "firefox update foo bar" matches the phrase "firefox
 * update" for example.  A query matches a document if it matches any phrase in
 * the document.  The query and phrases are compared word for word, and we allow
 * fuzzy matching by computing the Levenshtein edit distance in each comparison.
 * The amount of fuzziness allowed is controlled with `distanceThreshold`.  If
 * the distance in a comparison is greater than this threshold, then the phrase
 * does not match the query.  The final score for a document is the minimum edit
 * distance between its phrases and the query.
 *
 * As mentioned, `score` returns a sorted array of document-score pairs.  It's
 * up to you to filter the array to exclude scores above a certain threshold, or
 * to take the top scorer, etc.
 */
export class QueryScorer {
  /**
   * @param {object} options
   *   Constructor options.
   * @param {number} [options.distanceThreshold]
   *   Edit distances no larger than this value are considered matches.
   * @param {Map} [options.variations]
   *   For convenience, the scorer can augment documents by replacing certain
   *   words with other words and phrases. This mechanism is called variations.
   *   This keys of this map are words that should be replaced, and the values
   *   are the replacement words or phrases.  For example, if you add a document
   *   whose only phrase is "firefox update", normally the scorer will register
   *   only this single phrase for the document.  However, if you pass the value
   *   `new Map(["firefox", ["fire fox", "fox fire", "foxfire"]])` for this
   *   parameter, it will register 4 total phrases for the document: "fire fox
   *   update", "fox fire update", "foxfire update", and the original "firefox
   *   update".
   */
  constructor({ distanceThreshold = 1, variations = new Map() } = {}) {
    this._distanceThreshold = distanceThreshold;
    this._variations = variations;
    this._documents = new Set();
    this._rootNode = new Node();
  }

  /**
   * Adds a document to the scorer.
   *
   * @param {object} doc
   *   The document.
   * @param {string} doc.id
   *   The document's ID.
   * @param {Array} doc.phrases
   *   The set of phrases in the document.  Each phrase should be a string.
   */
  addDocument(doc) {
    this._documents.add(doc);

    for (let phraseStr of doc.phrases) {
      // Split the phrase and lowercase the words.
      let phrase = phraseStr
        .trim()
        .split(/\s+/)
        .map(word => word.toLocaleLowerCase());

      // Build a phrase list that contains the original phrase plus its
      // variations, if any.
      let phrases = [phrase];
      for (let [triggerWord, variations] of this._variations) {
        let index = phrase.indexOf(triggerWord);
        if (index >= 0) {
          for (let variation of variations) {
            let variationPhrase = Array.from(phrase);
            variationPhrase.splice(index, 1, ...variation.split(/\s+/));
            phrases.push(variationPhrase);
          }
        }
      }

      // Finally, add the phrases to the phrase tree.
      for (let completedPhrase of phrases) {
        this._buildPhraseTree(this._rootNode, doc, completedPhrase, 0);
      }
    }
  }

  /**
   * Scores a query string against the documents in the scorer.
   *
   * @param {string} queryString
   *   The query string to score.
   * @returns {Array}
   *   An array of objects: { document, score }.  Each element in the array is a
   *   a document and its score against the query string.  The elements are
   *   ordered by score from low to high.  Scores represent edit distance, so
   *   lower scores are better.
   */
  score(queryString) {
    let queryWords = queryString
      .trim()
      .split(/\s+/)
      .map(word => word.toLocaleLowerCase());
    let minDistanceByDoc = this._traverse({ queryWords });
    let results = [];
    for (let doc of this._documents) {
      let distance = minDistanceByDoc.get(doc);
      results.push({
        document: doc,
        score: distance === undefined ? Infinity : distance,
      });
    }
    results.sort((a, b) => a.score - b.score);
    return results;
  }

  /**
   * Builds the phrase tree based on the current documents.
   *
   * The phrase tree lets us efficiently match queries against phrases.  Each
   * path through the tree starting from the root and ending at a leaf
   * represents a complete phrase in a document (or more than one document, if
   * the same phrase is present in multiple documents).  Each node in the path
   * represents a word in the phrase.  To match a query, we start at the root,
   * and in the root we look up the query's first word.  If the word matches the
   * first word of any phrase, then the root will have a child node representing
   * that word, and we move on to the child node.  Then we look up the query's
   * second word in the child node, and so on, until either a lookup fails or we
   * reach a leaf node.
   *
   * @param {Node} node
   *   The current node being visited.
   * @param {object} doc
   *   The document whose phrases are being added to the tree.
   * @param {Array} phrase
   *   The phrase to add to the tree.
   * @param {number} wordIndex
   *   The index in the phrase of the current word.
   */
  _buildPhraseTree(node, doc, phrase, wordIndex) {
    if (phrase.length == wordIndex) {
      // We're done with this phrase.
      return;
    }

    let word = phrase[wordIndex].toLocaleLowerCase();
    let child = node.childrenByWord.get(word);
    if (!child) {
      child = new Node(word);
      node.childrenByWord.set(word, child);
    }
    child.documents.add(doc);

    // Recurse with the next word in the phrase.
    this._buildPhraseTree(child, doc, phrase, wordIndex + 1);
  }

  /**
   * Traverses a path in the phrase tree in order to score a query.  See
   * `_buildPhraseTree` for a description of how this works.
   *
   * @param {object} options
   *   Options.
   * @param {Array} options.queryWords
   *   The query being scored, split into words.
   * @param {Node} [options.node]
   *   The node currently being visited.
   * @param {Map} [options.minDistanceByDoc]
   *   Keeps track of the minimum edit distance for each document as the
   *   traversal continues.
   * @param {number} [options.queryWordsIndex]
   *   The current index in the query words array.
   * @param {number} [options.phraseDistance]
   *   The total edit distance between the query and the path in the tree that's
   *   been traversed so far.
   * @returns {Map} minDistanceByDoc
   */
  _traverse({
    queryWords,
    node = this._rootNode,
    minDistanceByDoc = new Map(),
    queryWordsIndex = 0,
    phraseDistance = 0,
  }) {
    if (!node.childrenByWord.size) {
      // We reached a leaf node.  The query has matched a phrase.  If the query
      // and the phrase have the same number of words, then queryWordsIndex ==
      // queryWords.length also.  Otherwise the query contains more words than
      // the phrase.  We still count that as a match.
      for (let doc of node.documents) {
        minDistanceByDoc.set(
          doc,
          Math.min(
            phraseDistance,
            minDistanceByDoc.has(doc) ? minDistanceByDoc.get(doc) : Infinity
          )
        );
      }
      return minDistanceByDoc;
    }

    if (queryWordsIndex == queryWords.length) {
      // We exhausted all the words in the query but have not reached a leaf
      // node.  No match; the query has matched a phrase(s) up to this point,
      // but it doesn't have enough words.
      return minDistanceByDoc;
    }

    // Compare each word in the node to the current query word.
    let queryWord = queryWords[queryWordsIndex];
    for (let [childWord, child] of node.childrenByWord) {
      let distance = lazy.NLP.levenshtein(queryWord, childWord);
      if (distance <= this._distanceThreshold) {
        // The word represented by this child node matches the current query
        // word.  Recurse into the child node.
        this._traverse({
          node: child,
          queryWords,
          queryWordsIndex: queryWordsIndex + 1,
          phraseDistance: phraseDistance + distance,
          minDistanceByDoc,
        });
      }
      // Else, the path that continues at the child node can't possibly match
      // the query, so don't recurse into it.
    }

    return minDistanceByDoc;
  }
}

/**
 * Gets appropriate values for each tip's payload.
 *
 * @param {string} tip a value from UrlbarShared.INTERVENTION_TIP_TYPE
 * @returns {object} Properties to include in the payload
 */
function getPayloadForTip(tip) {
  const baseURL = Services.urlFormatter.formatURLPref("app.support.baseURL");
  switch (tip) {
    case UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR:
      return {
        titleL10n: { id: "intervention-clear-data" },
        buttons: [{ l10n: { id: "intervention-clear-data-confirm" } }],
        helpUrl: baseURL + "delete-browsing-search-download-history-firefox",
      };
    case UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH:
      return {
        titleL10n: { id: "intervention-refresh-profile" },
        buttons: [{ l10n: { id: "intervention-refresh-profile-confirm" } }],
        helpUrl: baseURL + "refresh-firefox-reset-add-ons-and-settings",
      };
    default:
      throw new Error("Unknown TIP type.");
  }
}

/**
 * A provider that returns actionable tip results when the user is performing
 * a search related to those actions.
 */
export class UrlbarProviderInterventions extends UrlbarProvider {
  static lazy = XPCOMUtils.declareLazy({
    // This object is used to match the user's queries to tips.
    queryScorer: () => {
      let queryScorer = new QueryScorer({
        variations: new Map([
          // Recognize "fire fox", "fox fire", and "foxfire" as "firefox".
          ["firefox", ["fire fox", "fox fire", "foxfire"]],
          // Recognize "mozila" as "mozilla".  This will catch common mispellings
          // "mozila", "mozzila", and "mozzilla" (among others) due to the edit
          // distance threshold of 1.
          ["mozilla", ["mozila"]],
        ]),
      });
      for (let [id, phrases] of Object.entries(DOCUMENTS)) {
        queryScorer.addDocument({ id, phrases });
      }
      return queryScorer;
    },
  });

  /**
   * The tip we should currently show.
   *
   * @type {Values<typeof UrlbarShared.INTERVENTION_TIP_TYPE>}
   */
  currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.NONE;

  /**
   * @returns {Values<typeof UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    if (
      !queryContext.searchString ||
      queryContext.searchString.length > UrlbarShared.MAX_TEXT_LENGTH ||
      lazy.UrlUtils.REGEXP_LIKE_PROTOCOL.test(queryContext.searchString) ||
      !EN_LOCALE_MATCH.test(Services.locale.appLocaleAsBCP47) ||
      !Services.policies.isAllowed("urlbarinterventions") ||
      (await this.queryInstance
        .getProvider(lazy.UrlbarProviderGlobalActions.name)
        ?.isActive(queryContext))
    ) {
      return false;
    }

    this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.NONE;

    // Get the scores and the top score.
    let docScores = UrlbarProviderInterventions.lazy.queryScorer.score(
      queryContext.searchString
    );
    let topDocScore = docScores[0];

    // Multiple docs may have the top score, so collect them all.
    let topDocIDs = new Set();
    if (topDocScore.score != Infinity) {
      for (let { score, document } of docScores) {
        if (score != topDocScore.score) {
          break;
        }
        topDocIDs.add(document.id);
      }
    }

    // Determine the tip to show, if any. If there are multiple top-score docs,
    // prefer them in the following order.
    if (topDocIDs.has("clear")) {
      let window = lazy.BrowserWindowTracker.getTopWindow();
      if (!lazy.PrivateBrowsingUtils.isWindowPrivate(window)) {
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR;
      }
    } else if (topDocIDs.has("refresh")) {
      this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH;
    }

    return (
      this.currentTip != UrlbarShared.INTERVENTION_TIP_TYPE.NONE &&
      (this.currentTip != UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH ||
        Services.policies.isAllowed("profileRefresh"))
    );
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    let result = new lazy.UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.TIP,
      source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
      suggestedIndex: 1,
      payload: {
        ...getPayloadForTip(this.currentTip),
        type: this.currentTip,
        icon: UrlbarShared.ICON.TIP,
        helpL10n: {
          id: "urlbar-result-menu-tip-get-help2",
        },
      },
    });
    addCallback(this, result);
  }

  #pickResult(result, window) {
    let tip = result.payload.type;

    // Do the tip action.
    switch (tip) {
      case UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR:
        openClearHistoryDialog(window);
        break;
      case UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH:
        resetBrowser(window);
        break;
    }
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   */
  onEngagement(queryContext, controller, details) {
    // `selType` is "tip" when the tip's main button is picked. Ignore clicks on
    // the help command ("help"), which is handled by UrlbarInput since we
    // set `helpUrl` on the result payload. Currently there aren't any other
    // buttons or commands but this will ignore clicks on them too.
    if (details.selType == "tip") {
      this.#pickResult(details.result, controller.browserWindow);
    }
  }
}

/**
 * Tip callbacks follow.
 */

function openClearHistoryDialog(window) {
  // The behaviour of the Clear Recent History dialog in PBM does
  // not have the expected effect (bug 463607).
  if (lazy.PrivateBrowsingUtils.isWindowPrivate(window)) {
    return;
  }
  lazy.Sanitizer.showUI(window, undefined, {
    entryPoint: lazy.Sanitizer.DIALOG_ENTRY_POINTS.ADDRESS_BAR_SUGGESTION,
  });
}

function resetBrowser(window) {
  if (!lazy.ResetProfile.resetSupported()) {
    return;
  }
  lazy.ResetProfile.openConfirmationDialog(window);
}
