/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UtilityProcessImpl.h"

#include "mozilla/GeckoArgs.h"




#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX)
#  include "mozilla/MFCDMParent.h"
#endif

namespace mozilla::ipc {

UtilityProcessImpl::~UtilityProcessImpl() = default;


bool UtilityProcessImpl::Init(int aArgc, char* aArgv[]) {
  Maybe<uint64_t> sandboxingKind = geckoargs::sSandboxingKind.Get(aArgc, aArgv);
  if (sandboxingKind.isNothing()) {
    return false;
  }

  if (*sandboxingKind >= SandboxingKind::COUNT) {
    return false;
  }


#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX)
  if (*sandboxingKind == MF_MEDIA_ENGINE_CDM) {
    Maybe<const char*> pluginPath = geckoargs::sPluginPath.Get(aArgc, aArgv);
    if (pluginPath) {
      MFCDMParent::SetWidevineL1Path(*pluginPath);
    } else {
      NS_WARNING("No Widevine L1 plugin for the utility process!");
    }
  }
#endif

  Maybe<const char*> parentBuildID =
      geckoargs::sParentBuildID.Get(aArgc, aArgv);
  if (parentBuildID.isNothing()) {
    return false;
  }

  if (!ProcessChild::InitPrefs(aArgc, aArgv)) {
    return false;
  }

#if defined(MOZ_MEMORY) && defined(DEBUG)
  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  MOZ_ASSERT(stats.opt_randomize_small,
             "Utility process should randomize small allocations");
#endif

  return mUtility->Init(TakeInitialEndpoint(), nsCString(*parentBuildID),
                        SandboxingKind(*sandboxingKind));
}

void UtilityProcessImpl::CleanUp() { NS_ShutdownXPCOM(nullptr); }

}  // namespace mozilla::ipc
