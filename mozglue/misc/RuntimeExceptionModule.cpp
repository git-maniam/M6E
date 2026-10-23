/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RuntimeExceptionModule.h"

#include "mozilla/ProcessType.h"
#include <stdlib.h>


namespace CrashReporter {


void RegisterRuntimeExceptionModule() {
}

void UnregisterRuntimeExceptionModule() {
}

bool CrashReporterIsEnabled(bool force) {
  if (force) {
    return true;
  }
#if defined(DEBUG)
  // In debug builds, disable the crash reporter by default, and allow to
  // enable it with the MOZ_CRASHREPORTER environment variable.
  const char* envvar = getenv("MOZ_CRASHREPORTER");
  return envvar && *envvar;
#else
  // In other builds, enable the crash reporter by default, and allow
  // disabling it with the MOZ_CRASHREPORTER_DISABLE environment variable.
  const char* envvar = getenv("MOZ_CRASHREPORTER_DISABLE");
  return !envvar || !*envvar;
#endif
}

}  // namespace CrashReporter
