/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SafeMode_h
#define mozilla_SafeMode_h

// NB: This code must be able to run apart from XPCOM

#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/Maybe.h"


// Undo X11/X.h's definition of None
#undef None

namespace mozilla {

enum class SafeModeFlag : uint32_t {
  None = 0,
  Unset = (1 << 0),
  NoKeyPressCheck = (1 << 1),
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(SafeModeFlag)

template <typename CharT>
inline Maybe<bool> IsSafeModeRequested(
    int& aArgc, CharT* aArgv[],
    const SafeModeFlag aFlags = SafeModeFlag::Unset) {
  CheckArgFlag checkArgFlags = CheckArgFlag::None;
  if (aFlags & SafeModeFlag::Unset) {
    checkArgFlags |= CheckArgFlag::RemoveArg;
  }

  ArgResult ar = CheckArg(aArgc, aArgv, "safe-mode", nullptr, checkArgFlags);
  if (ar == ARG_BAD) {
    return Nothing();
  }

  bool result = ar == ARG_FOUND;


  if (!(aFlags & SafeModeFlag::NoKeyPressCheck) &&
      (GetCurrentEventKeyModifiers() & optionKey) &&
      !EnvHasValue("MOZ_DISABLE_SAFE_MODE_KEY")) {
    result = true;
  }

  // The Safe Mode Policy should not be enforced for the env var case
  // (used by updater and crash-recovery).
  if (EnvHasValue("MOZ_SAFE_MODE_RESTART")) {
    result = true;
    if (aFlags & SafeModeFlag::Unset) {
      // unset the env variable
      SaveToEnv("MOZ_SAFE_MODE_RESTART=");
    }
  }

  return Some(result);
}

}  // namespace mozilla

#endif  // mozilla_SafeMode_h
