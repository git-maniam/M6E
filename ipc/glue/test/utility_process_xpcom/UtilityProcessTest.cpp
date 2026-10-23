/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if defined(ENABLE_TESTS)
#  include "mozilla/ipc/UtilityProcessManager.h"
#  include "mozilla/ipc/UtilityProcessTest.h"
#  include "mozilla/dom/ChromeUtilsBinding.h"
#  include "mozilla/dom/Promise.h"
#  include "mozilla/ProcInfo.h"
#  include "mozilla/IntentionalCrash.h"


namespace mozilla::ipc {

static UtilityActorName UtilityActorNameFromString(
    const nsACString& aStringName) {
  using namespace mozilla::dom;
  auto idlName = StringToEnum<UtilityActorName>(aStringName);
  if (idlName.isSome()) {
    return idlName.value();
  }
  MOZ_CRASH("Unknown utility actor name");
}

// Find the utility process with the given actor or any utility process if
// aActorName is Nothing().
static SandboxingKind FindUtilityProcessWithActor(
    const Maybe<UtilityActorName>& aActorName) {
  RefPtr<UtilityProcessManager> utilityProc =
      UtilityProcessManager::GetSingleton();
  MOZ_ASSERT(utilityProc, "No UtilityprocessManager?");

  for (size_t i = 0; i < SandboxingKind::COUNT; ++i) {
    auto sbKind = static_cast<SandboxingKind>(i);
    RefPtr<UtilityProcessKeepAlive> keepAlive =
        utilityProc->GetSharedKeepAlive(sbKind);
    if (!keepAlive) {
      continue;
    }
    if (aActorName.isNothing()) {
      return sbKind;
    }
    for (auto actor : utilityProc->GetActors(keepAlive->GetProcessParent())) {
      if (actor == aActorName.ref()) {
        return sbKind;
      }
    }
  }

  return SandboxingKind::COUNT;
}


NS_IMETHODIMP
UtilityProcessTest::StartProcess(const nsTArray<nsCString>& aActorsToRegister,
                                 JSContext* aCx,
                                 mozilla::dom::Promise** aOutPromise) {
  NS_ENSURE_ARG(aOutPromise);
  *aOutPromise = nullptr;
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  RefPtr<UtilityProcessManager> utilityProc =
      UtilityProcessManager::GetSingleton();
  MOZ_ASSERT(utilityProc, "No UtilityprocessManager?");

  auto actors = aActorsToRegister.Clone();

  utilityProc->LaunchProcess(SandboxingKind::GENERIC_UTILITY)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise, utilityProc, actors = std::move(actors)] {
            RefPtr<UtilityProcessKeepAlive> keepAlive =
                utilityProc->GetSharedKeepAlive(
                    SandboxingKind::GENERIC_UTILITY);
            RefPtr<UtilityProcessParent> utilityParent =
                keepAlive ? keepAlive->GetProcessParent() : nullptr;
            Maybe<int32_t> utilityPid =
                utilityProc->ProcessPid(SandboxingKind::GENERIC_UTILITY);
            for (size_t i = 0; i < actors.Length(); ++i) {
              auto uan = UtilityActorNameFromString(actors[i]);
              utilityProc->RegisterActor(utilityParent, uan);
            }
            if (utilityPid.isSome()) {
              promise->MaybeResolve(*utilityPid);
            } else {
              promise->MaybeReject(NS_ERROR_NOT_AVAILABLE);
            }
          },
          [promise](LaunchError aError) {
            MOZ_ASSERT_UNREACHABLE(
                "UtilityProcessTest; failure to get Utility process");
            promise->MaybeReject(NS_ERROR_FAILURE);
          });

  promise.forget(aOutPromise);
  return NS_OK;
}

NS_IMETHODIMP
UtilityProcessTest::NoteIntentionalCrash(uint32_t aPid) {
  mozilla::NoteIntentionalCrash("utility", aPid);
  return NS_OK;
}

NS_IMETHODIMP
UtilityProcessTest::UntilChildProcessDead(
    uint32_t pid, JSContext* cx, ::mozilla::dom::Promise** aOutPromise) {
  NS_ENSURE_ARG(aOutPromise);
  *aOutPromise = nullptr;

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
UtilityProcessTest::StopProcess(const char* aActorName) {
  using namespace mozilla::dom;

  SandboxingKind sbKind;
  if (aActorName) {
    const nsDependentCString actorStringName(aActorName);
    UtilityActorName actorName = UtilityActorNameFromString(actorStringName);
    sbKind = FindUtilityProcessWithActor(Some(actorName));
  } else {
    sbKind = FindUtilityProcessWithActor(Nothing());
  }

  if (sbKind == SandboxingKind::COUNT) {
    MOZ_ASSERT_UNREACHABLE(
        "Attempted to stop process for actor when no "
        "such process exists");
    return NS_ERROR_FAILURE;
  }

  RefPtr<UtilityProcessManager> utilityProc =
      UtilityProcessManager::GetSingleton();
  MOZ_ASSERT(utilityProc, "No UtilityprocessManager?");

  utilityProc->CleanShutdown(sbKind);
  Maybe<int32_t> utilityPid = utilityProc->ProcessPid(sbKind);
  MOZ_RELEASE_ASSERT(utilityPid.isNothing(),
                     "Should not have a utility process PID anymore");

  return NS_OK;
}

NS_IMETHODIMP
UtilityProcessTest::TestTelemetryProbes() {
  RefPtr<UtilityProcessManager> utilityProc =
      UtilityProcessManager::GetSingleton();
  MOZ_ASSERT(utilityProc, "No UtilityprocessManager?");

  for (RefPtr<UtilityProcessParent>& parent :
       utilityProc->GetAllProcessesProcessParent()) {
    (void)parent->SendTestTelemetryProbes();
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS(UtilityProcessTest, nsIUtilityProcessTest)

}  // namespace mozilla::ipc
#endif  // defined(ENABLE_TESTS)
