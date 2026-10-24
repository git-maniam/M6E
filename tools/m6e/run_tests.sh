#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# M6E's regression suites: M6E's own tests and the areas M6E changes.
# Usage: tools/m6e/run_tests.sh <log directory>
# Headless suites run first; the panel suites refuse --headless, so they run
# afterwards with a window, alone.

set -u
out="${1:?log directory}"
mkdir -p "$out"

run() {
  name="$1"
  shift
  ./mach test "$@" > "$out/$name.log" 2>&1
  echo "$? $name"
}

run m6e-own --headless \
  browser/components/preferences/tests/etp \
  dom/security/test/https-first \
  services/settings/test/unit

run security --headless \
  dom/security/test/https-only \
  toolkit/components/antitracking/test/xpcshell

run reader --headless toolkit/components/reader/tests/browser

run nimbus toolkit/components/nimbus/test/unit

run modules toolkit/modules/tests/xpcshell toolkit/components/utils/test/unit

run policies --headless browser/components/enterprisepolicies/tests/xpcshell

run pagetelemetry browser/components/pagetelemetry

run trustpanel browser/components/urlbar/tests/browser-trustPanel
