/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains the shared types for the translations component. The intended use
 * is for defining types to be used in JSDoc. They are used in a form that the TypeScript
 * language server can read them, and provide code hints.
 */

/**
 * A larger web document can be composed of multiple languages. This object details the
 * breakdown of what languages are present in the document, and at what percentages.
 * For instance a document could be 70% English and 30% French:
 *
 *   [
 *      { language: "en", percentage: 70 },
 *      { language: "fr", percentage: 30 },
 *   ]
 */
export interface MultilingualSection {
  // BCP 47 language tag, or "un" for unknown.
  language: string;
  // The integral percentage ranged 0-100.
  percent: number;
}

export interface DetectionResult {
  // The language code.
  language: string;
  // Whether the detector is confident of the result.
  confident: boolean;
  // The list of languages detected in multilingual content. This is between 0 and 3
  // languages.
  languages: MultilingualSection[];
}
