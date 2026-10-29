/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { NimbusFeatures } from "resource://nimbus/ExperimentAPI.sys.mjs";

/**
 * Makes a feature report the given variable values, as an enrollment would.
 *
 * @param {object} options
 * @param {string} options.featureId
 * @param {object} options.value
 *   Variable values. Variables not listed keep their fallback pref values.
 * @returns {Function}
 *   An async function that removes the override.
 */
export function overrideFeature({ featureId, value }) {
  const feature = NimbusFeatures[featureId];
  const proto = Object.getPrototypeOf(feature);

  feature.getAllVariables = function (options) {
    return { ...proto.getAllVariables.call(this, options), ...value };
  };
  feature.getVariable = function (variable) {
    return variable in value
      ? value[variable]
      : proto.getVariable.call(this, variable);
  };

  return async () => {
    delete feature.getAllVariables;
    delete feature.getVariable;
  };
}
