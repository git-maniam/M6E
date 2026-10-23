/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxSettings_h
#define mozilla_SandboxSettings_h

#if defined(MOZ_PROFILE_GENERATE)
#  include <string>
#endif

#include "nsIXULRuntime.h"


namespace mozilla {

// Return the current sandbox level. This is the
// "security.sandbox.content.level" preference, but rounded up to the current
// minimum allowed level. Returns 0 (disabled) if the env var
// MOZ_DISABLE_CONTENT_SANDBOX is set.
int GetEffectiveContentSandboxLevel();
int GetEffectiveSocketProcessSandboxLevel();
// Returns the "security.sandbox.gpu.level" preference, or 0 (disabled) if the
// env var MOZ_DISABLE_GPU_SANDBOX is set.
int GetEffectiveGpuSandboxLevel();

#if defined(MOZ_PROFILE_GENERATE)
bool GetLlvmProfileDir(std::string& parentPath);
#endif

// Checks whether the effective content sandbox level is > 0.
bool IsContentSandboxEnabled();

bool IsGPUSandboxEnabled();

const char* ContentWin32kLockdownStateToString(
    nsIXULRuntime::ContentWin32kLockdownState aValue);

bool GetContentWin32kLockdownEnabled();

nsIXULRuntime::ContentWin32kLockdownState GetContentWin32kLockdownState();

int ClampFlashSandboxLevel(const int aLevel);


}  // namespace mozilla
#endif  // mozilla_SandboxPolicies_h
