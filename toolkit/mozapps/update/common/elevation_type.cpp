/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "elevation_type.h"
#include "updatecommon.h"

const char* BoolResultToString(BoolResult boolResult) {
  switch (boolResult) {
    case BoolResult::True:
      return "True";
    case BoolResult::False:
      return "False";
    case BoolResult::Error:
      return "Error";
  }
  MOZ_CRASH("impossible value for BoolResult");
}

// Indicates that we are running with elevated privileges.
// This is only ever true on macOS and Windows. We don't currently have a
// way of elevating on other platforms.
// Note that this should not be used to determine whether this is the first or
// second invocation of the updater, even though the first invocation will
// _usually_ be unelevated and the second invocation should always be
// elevated. `gInvocation` can be used for that purpose.
BoolResult isProcessElevated(const NS_tchar* cmd) {
  if (!cmd) {
    return BoolResult::False;
  }
  return strstr(cmd, "/Library/PrivilegedHelperTools/org.mozilla.updater") !=
                 nullptr
             ? BoolResult::True
             : BoolResult::False;
}

const char* elevationTypeToString(ElevationType elevationType) {
  switch (elevationType) {
    case ElevationType::Unknown:
      return "ElevationType::Unknown";
    case ElevationType::None:
      return "ElevationType::None";
    case ElevationType::ElevatedByMMS:
      return "ElevationType::ElevatedByMMS";
    case ElevationType::ElevatedWithoutMMS:
      return "ElevationType::ElevatedWithoutMMS";
    case ElevationType::Error:
      return "ElevationType::Error";
  }
  MOZ_CRASH("impossible value for ElevationType");
}

ElevationType getElevationType(int argc, NS_tchar** argv) {
  // First, are we elevated at all?
  const NS_tchar* cmd = nullptr;
  if (argc > 0) {
    cmd = argv[0];
  }
  BoolResult isElevatedResult = isProcessElevated(cmd);
  if (isElevatedResult == BoolResult::Error) {
    fprintf(stderr, "Check for elevation failed\n");
    return ElevationType::Error;
  } else if (isElevatedResult == BoolResult::False) {
    return ElevationType::None;
  }

  // If we are elevated, is it through MMS or some other way?
#ifdef MOZ_MAINTENANCE_SERVICE
  if (EnvHasValue("MOZ_USING_SERVICE")) {
    putenv(const_cast<char*>("MOZ_USING_SERVICE="));
    return ElevationType::ElevatedByMMS;
  }
#endif
  return ElevationType::ElevatedWithoutMMS;
}

bool isElevationTypeElevated(ElevationType elevationType) {
  return elevationType == ElevationType::ElevatedByMMS ||
         elevationType == ElevationType::ElevatedWithoutMMS;
}

bool isValidInvocationForElevationType(UpdaterInvocation updaterInvocation,
                                       ElevationType elevationType) {
  switch (updaterInvocation) {
    case UpdaterInvocation::First:
      return elevationType == ElevationType::None ||
             elevationType == ElevationType::ElevatedWithoutMMS;
    case UpdaterInvocation::Second:
      return elevationType != ElevationType::Error &&
             elevationType != ElevationType::Unknown;
    default:
      return false;
  }
}
