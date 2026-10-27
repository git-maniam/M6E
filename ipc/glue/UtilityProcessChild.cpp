/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UtilityProcessChild.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/JSOracleChild.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/Preferences.h"
#include "mozilla/RemoteMediaManagerParent.h"



#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#  include "mozilla/SandboxTestingChild.h"
#endif

#include "mozilla/Telemetry.h"


#include "nsDebugImpl.h"
#include "nsIXULRuntime.h"
#include "nsThreadManager.h"
#include "GeckoProfiler.h"

#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/glean/GleanTestsTestMetrics.h"

#include "mozilla/Services.h"

namespace TelemetryScalar {
void Set(mozilla::Telemetry::ScalarID aId, uint32_t aValue);
}

namespace mozilla::ipc {

using namespace layers;

static StaticMutex sUtilityProcessChildMutex;
static StaticRefPtr<UtilityProcessChild> sUtilityProcessChild
    MOZ_GUARDED_BY(sUtilityProcessChildMutex);

UtilityProcessChild::UtilityProcessChild() : mChildStartTime(TimeStamp::Now()) {
  nsDebugImpl::SetMultiprocessMode("Utility");
}

UtilityProcessChild::~UtilityProcessChild() = default;

/* static */
RefPtr<UtilityProcessChild> UtilityProcessChild::GetSingleton() {
  MOZ_ASSERT(XRE_IsUtilityProcess());
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownFinal)) {
    return nullptr;
  }
  StaticMutexAutoLock lock(sUtilityProcessChildMutex);
  if (!sUtilityProcessChild) {
    sUtilityProcessChild = new UtilityProcessChild();
  }
  return sUtilityProcessChild;
}

/* static */
RefPtr<UtilityProcessChild> UtilityProcessChild::Get() {
  StaticMutexAutoLock lock(sUtilityProcessChildMutex);
  return sUtilityProcessChild;
}

bool UtilityProcessChild::Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
                               const nsCString& aParentBuildID,
                               SandboxingKind aSandboxingKind) {
  MOZ_ASSERT(NS_IsMainThread());

  // Initialize the thread manager before starting IPC. Otherwise, messages
  // may be posted to the main thread and we won't be able to process them.
  if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
    return false;
  }

  // Now it's safe to start IPC.
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return false;
  }

  // This must be checked before any IPDL message, which may hit sentinel
  // errors due to parent and content processes having different
  // versions.
  MessageChannel* channel = GetIPCChannel();
  if (channel && !channel->SendBuildIDsMatchMessage(aParentBuildID.get())) {
    // We need to quit this process if the buildID doesn't match the parent's.
    // This can occur when an update occurred in the background.
    ipc::ProcessChild::QuickExit();
  }

  // Init crash reporter support.
  ipc::CrashReporterClient::InitSingleton(this);

  if (NS_FAILED(NS_InitMinimalXPCOM())) {
    return false;
  }

  mSandbox = aSandboxingKind;

  // At the moment, only ORB uses JSContext in the
  // Utility Process and ORB uses GENERIC_UTILITY
  if (mSandbox == SandboxingKind::GENERIC_UTILITY) {
    if (!JS_FrontendOnlyInit()) {
      return false;
    }
  }

  profiler_set_process_name(nsCString("Utility Process"));

  // Notify the parent process that we have finished our init and that it can
  // now resolve the pending promise of process startup
  SendInitCompleted();

  PROFILER_MARKER_UNTYPED(
      "UtilityProcessChild::SendInitCompleted", IPC,
      MarkerOptions(MarkerTiming::IntervalUntilNowFrom(mChildStartTime)));

  RunOnShutdown(
      [sandboxKind = mSandbox] {
        StaticMutexAutoLock lock(sUtilityProcessChildMutex);
        sUtilityProcessChild = nullptr;
        if (sandboxKind == SandboxingKind::GENERIC_UTILITY) {
          JS_FrontendOnlyShutDown();
        }
      },
      ShutdownPhase::XPCOMShutdownFinal);

  return true;
}

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
extern "C" {
void CGSShutdownServerConnections();
};
#endif

mozilla::ipc::IPCResult UtilityProcessChild::RecvInit(
    const Maybe<FileDescriptor>& aBrokerFd,
    const bool& aCanRecordReleaseTelemetry,
    const bool& aIsReadyForBackgroundProcessing) {
  // Do this now (before closing WindowServer on macOS) to avoid risking
  // blocking in GetCurrentProcess() called on that platform
  mozilla::ipc::SetThisProcessName("Utility Process");

#if defined(MOZ_SANDBOX)
  // Close all current connections to the WindowServer. This ensures that the
  // Activity Monitor will not label the content process as "Not responding"
  // because it's not running a native event loop. See bug 1384336.
  CGSShutdownServerConnections();

#endif    // MOZ_SANDBOX


  PROFILER_MARKER_UNTYPED(
      "UtilityProcessChild::RecvInit", IPC,
      MarkerOptions(MarkerTiming::IntervalUntilNowFrom(mChildStartTime)));
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvPreferenceUpdate(
    const Pref& aPref) {
  Preferences::SetPreference(aPref);
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvInitProfiler(
    Endpoint<PProfilerChild>&& aEndpoint) {
  mProfilerController = ChildProfilerController::Create(std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvRequestMemoryReport(
    const uint32_t& aGeneration, const bool& aAnonymize,
    const bool& aMinimizeMemoryUsage, const Maybe<FileDescriptor>& aDMDFile,
    const RequestMemoryReportResolver& aResolver) {
  nsPrintfCString processName("Utility (pid %" PRIPID
                              ", sandboxingKind %" PRIu64 ")",
                              base::GetCurrentProcId(), mSandbox);

  mozilla::dom::MemoryReportRequestClient::Start(
      aGeneration, aAnonymize, aMinimizeMemoryUsage, aDMDFile, processName,
      [&](const MemoryReport& aReport) {
        (void)GetSingleton()->SendAddMemoryReport(aReport);
      },
      aResolver);
  return IPC_OK();
}

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
IPCResult UtilityProcessChild::RecvStartPKCS11ModuleService(
    Endpoint<PPKCS11ModuleChild>&& aEndpoint, nsCString&& aProfilePath) {
  auto child = MakeRefPtr<psm::PKCS11ModuleChild>();
  if (!child ||
      NS_FAILED(child->Start(std::move(aEndpoint), std::move(aProfilePath)))) {
    return IPC_FAIL(this, "Failed to create and start PKCS11ModuleChild");
  }

  mPKCS11ModuleInstance = std::move(child);
  return IPC_OK();
}
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
mozilla::ipc::IPCResult UtilityProcessChild::RecvInitSandboxTesting(
    Endpoint<PSandboxTestingChild>&& aEndpoint) {
  if (!SandboxTestingChild::Initialize(std::move(aEndpoint))) {
    return IPC_FAIL(
        this, "InitSandboxTesting failed to initialise the child process.");
  }
  return IPC_OK();
}
#endif

mozilla::ipc::IPCResult UtilityProcessChild::RecvFlushFOGData(
    FlushFOGDataResolver&& aResolver) {
  glean::FlushFOGData(std::move(aResolver));
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvTestTriggerMetrics(
    TestTriggerMetricsResolver&& aResolve) {
  mozilla::glean::test_only_ipc::a_counter.Add(
      nsIXULRuntime::PROCESS_TYPE_UTILITY);
  aResolve(true);
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvTestTelemetryProbes() {
  const uint32_t kExpectedUintValue = 42;
  TelemetryScalar::Set(Telemetry::ScalarID::TELEMETRY_TEST_UTILITY_ONLY_UINT,
                       kExpectedUintValue);
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvStartUtilityMediaService(
    Endpoint<PUtilityMediaServiceParent>&& aEndpoint,
    nsTArray<gfx::GfxVarUpdate>&& aUpdates) {
  PROFILER_MARKER_UNTYPED(
      "UtilityProcessChild::RecvStartUtilityMediaService", MEDIA,
      MarkerOptions(MarkerTiming::IntervalUntilNowFrom(mChildStartTime)));
  mUtilityMediaServiceInstance =
      new UtilityMediaServiceParent(std::move(aUpdates));
  if (!mUtilityMediaServiceInstance) {
    return IPC_FAIL(this, "Failed to create UtilityMediaServiceParent");
  }

  mUtilityMediaServiceInstance->Start(std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult UtilityProcessChild::RecvStartJSOracleService(
    Endpoint<PJSOracleChild>&& aEndpoint) {
  PROFILER_MARKER_UNTYPED(
      "UtilityProcessChild::RecvStartJSOracleService", JS,
      MarkerOptions(MarkerTiming::IntervalUntilNowFrom(mChildStartTime)));
  mJSOracleInstance = new mozilla::dom::JSOracleChild();
  if (!mJSOracleInstance) {
    return IPC_FAIL(this, "Failed to create JSOracleParent");
  }

  mJSOracleInstance->Start(std::move(aEndpoint));
  return IPC_OK();
}


mozilla::ipc::IPCResult UtilityProcessChild::RecvShutdown() {
  // Send the last bits of Glean data over to the main process. Doing this in
  // ActorDestroy would be too late: our channel is already gone by then.
  glean::FlushFOGData(
      [](ByteBuf&& aBuf) { glean::SendFOGData(std::move(aBuf)); });

  if (mProfilerController) {
    ProfileAndAdditionalInformation shutdownProfileAndAdditionalInformation =
        mProfilerController->GrabShutdownProfileAndShutdown();
    mProfilerController = nullptr;

    if (const size_t len = shutdownProfileAndAdditionalInformation.SizeOf();
        len >= size_t(IPC::Channel::kMaximumMessageSize)) {
      shutdownProfileAndAdditionalInformation.mProfile = nsPrintfCString(
          "*Profile from pid %u bigger (%zu) than IPC max (%zu)",
          unsigned(profiler_current_process_id().ToNumber()), len,
          size_t(IPC::Channel::kMaximumMessageSize));
      shutdownProfileAndAdditionalInformation.mAdditionalInformation.reset();
    }

    // Send the shutdown profile to the parent process through our own
    // message channel, which we know will survive for long enough.
    (void)SendShutdownProfile(
        std::move(shutdownProfileAndAdditionalInformation));
  }

  // Now tell the parent to actually destroy our channel, which will end our
  // process. This is expected to be the last event the parent will ever
  // process for this UtilityProcessChild.
  //
  // Note that we deliberately don't call Close() ourselves here: that would
  // immediately trigger ActorDestroy(), which exits the process, potentially
  // killing it before the messages we sent above have been delivered.
  (void)SendFinishShutdown();
  return IPC_OK();
}

void UtilityProcessChild::ActorDestroy(ActorDestroyReason aWhy) {

  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Shutting down Utility process early due to a crash!");
    ipc::ProcessChild::QuickExit();
  }

#ifndef NS_FREE_PERMANENT_DATA
  ProcessChild::QuickExit();
#else

  // RecvShutdown normally did this already, but it doesn't run if the parent
  // could not ask us to shut down gracefully.
  if (mProfilerController) {
    mProfilerController->Shutdown();
    mProfilerController = nullptr;
  }

  uint32_t timeout = 0;
  if (mUtilityMediaServiceInstance) {
    mUtilityMediaServiceInstance = nullptr;
    timeout = 10 * 1000;
  }

  mJSOracleInstance = nullptr;

#  if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  mPKCS11ModuleInstance = nullptr;
#  endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

  // Wait until all RemoteMediaManagerParent have closed.
  // It is still possible some may not have clean up yet, and we might hit
  // timeout. Our xpcom-shutdown listener should take care of cleaning the
  // reference of our singleton.
  //
  // FIXME: Should move from using AsyncBlockers to proper
  // nsIAsyncShutdownService once it is not JS, see bug 1760855
  mShutdownBlockers.WaitUntilClear(timeout)->Then(
      GetCurrentSerialEventTarget(), __func__, [&]() {

        ipc::CrashReporterClient::DestroySingleton();
        XRE_ShutdownChildProcess();
      });
#endif    // NS_FREE_PERMANENT_DATA
}

}  // namespace mozilla::ipc
