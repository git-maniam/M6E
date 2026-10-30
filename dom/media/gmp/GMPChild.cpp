/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPChild.h"

#include <algorithm>

#include "ChildProfilerController.h"
#include "ChromiumCDMAdapter.h"
#include "GeckoProfiler.h"
#include "base/command_line.h"
#include "base/task.h"
#include "GMPContentChild.h"
#include "GMPLoader.h"
#include "GMPLog.h"
#include "GMPPlatform.h"
#include "GMPProcessChild.h"
#include "GMPProcessParent.h"
#include "GMPUtils.h"
#include "GMPVideoDecoderChild.h"
#include "GMPVideoEncoderChild.h"
#include "GMPVideoHost.h"
#include "gmp-video-decode.h"
#include "gmp-video-encode.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/TextUtils.h"
#include "mozilla/glean/GleanTestsTestMetrics.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"
#include "nsDebugImpl.h"
#include "nsExceptionHandler.h"
#include "nsIFile.h"
#include "nsIXULRuntime.h"
#include "nsReadableUtils.h"
#include "nsThreadManager.h"
#include "nsXPCOM.h"
#include "nsXPCOMPrivate.h"  // for XUL_DLL
#include "nsXULAppAPI.h"
#include "prio.h"
#  include <unistd.h>  // for _exit()

using namespace mozilla::ipc;

namespace mozilla {
namespace gmp {

#define GMP_CHILD_LOG(loglevel, x, ...)                       \
  MOZ_LOG_FMT(GetGMPLog(), (loglevel), "GMPChild[pid={}] " x, \
              (int)base::GetCurrentProcId(), ##__VA_ARGS__)

#define GMP_CHILD_LOG_DEBUG(x, ...) \
  GMP_CHILD_LOG(LogLevel::Debug, x, ##__VA_ARGS__)

GMPChild::GMPChild()
    : mGMPMessageLoop(MessageLoop::current()), mGMPLoader(nullptr) {
  MOZ_ASSERT(NS_IsMainThread());
  GMP_CHILD_LOG_DEBUG("GMPChild ctor");
  nsDebugImpl::SetMultiprocessMode("GMP");
}

GMPChild::~GMPChild() {
  GMP_CHILD_LOG_DEBUG("GMPChild dtor");
}

bool GMPChild::Init(const nsAString& aPluginPath, const char* aParentBuildID,
                    mozilla::ipc::UntypedEndpoint&& aEndpoint) {
  GMP_CHILD_LOG_DEBUG("{} pluginPath={} useXpcom={}, useNativeEvent={}",
                      __FUNCTION__, NS_ConvertUTF16toUTF8(aPluginPath).get(),
                      GMPProcessChild::UseXPCOM(),
                      GMPProcessChild::UseNativeEventProcessing());

  // GMPChild needs nsThreadManager to create the ProfilerChild thread.
  // It is also used on debug builds for the sandbox tests.
  if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
    return false;
  }

  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return false;
  }

  // This must be checked before any IPDL message, which may hit sentinel
  // errors due to parent and content processes having different
  // versions.
  MessageChannel* channel = GetIPCChannel();
  if (channel && !channel->SendBuildIDsMatchMessage(aParentBuildID)) {
    // We need to quit this process if the buildID doesn't match the parent's.
    // This can occur when an update occurred in the background.
    ipc::ProcessChild::QuickExit();
  }

  CrashReporterClient::InitSingleton();

  if (GMPProcessChild::UseXPCOM()) {
    if (NS_WARN_IF(NS_FAILED(NS_InitMinimalXPCOM()))) {
      return false;
    }
  } else {
    BackgroundHangMonitor::Startup();
  }

  mPluginPath = aPluginPath;

  nsAutoCString processName("GMPlugin Process");

  nsAutoCString pluginName;
  if (GetPluginName(pluginName)) {
    processName.AppendLiteral(" (");
    processName.Append(pluginName);
    processName.AppendLiteral(")");
  }

  profiler_set_process_name(processName);

  return true;
}

void GMPChild::Shutdown() {
  if (GMPProcessChild::UseXPCOM()) {
    NS_ShutdownXPCOM(nullptr);
  } else {
    BackgroundHangMonitor::Shutdown();
  }
}

mozilla::ipc::IPCResult GMPChild::RecvProvideStorageId(
    const nsCString& aStorageId) {
  GMP_CHILD_LOG_DEBUG("{}", __FUNCTION__);
  mStorageId = aStorageId;
  return IPC_OK();
}

GMPErr GMPChild::GetAPI(const char* aAPIName, void* aHostAPI, void** aPluginAPI,
                        const nsACString& aKeySystem) {
  if (!mGMPLoader) {
    return GMPGenericErr;
  }
  return mGMPLoader->GetAPI(aAPIName, aHostAPI, aPluginAPI, aKeySystem);
}

mozilla::ipc::IPCResult GMPChild::RecvPreloadLibs(const nsCString& aLibs) {
  // Pre-load libraries that may need to be used by the EME plugin but that
  // can't be loaded after the sandbox has started.
  return IPC_OK();
}

bool GMPChild::GetUTF8LibPath(nsACString& aOutLibPath) {
  nsCOMPtr<nsIFile> libFile;

#  define GMP_PATH_CRASH(explain)                           \
    do {                                                    \
      nsAutoString path;                                    \
      if (!libFile || NS_FAILED(libFile->GetPath(path))) {  \
        path = mPluginPath;                                 \
      }                                                     \
      CrashReporter::RecordAnnotationNSString(              \
          CrashReporter::Annotation::GMPLibraryPath, path); \
      MOZ_CRASH(explain);                                   \
    } while (false)

  nsresult rv = NS_NewLocalFile(mPluginPath, getter_AddRefs(libFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to create file for plugin path");
    return false;
  }

  nsCOMPtr<nsIFile> parent;
  rv = libFile->GetParent(getter_AddRefs(parent));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to get parent file for plugin file");
    return false;
  }

  nsAutoString parentLeafName;
  rv = parent->GetLeafName(parentLeafName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to get leaf for plugin file");
    return false;
  }

  nsAutoString baseName;
  baseName = Substring(parentLeafName, 4, parentLeafName.Length() - 1);

  nsAutoString binaryName = u"lib"_ns + baseName + u".dylib"_ns;
  rv = libFile->AppendRelativePath(binaryName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to append lib to plugin file");
    return false;
  }

  if (NS_WARN_IF(!FileExists(libFile))) {
    GMP_PATH_CRASH("Plugin file does not exist");
    return false;
  }

  nsAutoString path;
  rv = libFile->GetPath(path);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to get path for plugin file");
    return false;
  }

  CopyUTF16toUTF8(path, aOutLibPath);
  return true;
}

bool GMPChild::GetPluginName(nsACString& aPluginName) const {
  // Extract the plugin directory name if possible.
  nsCOMPtr<nsIFile> libFile;
  nsresult rv = NS_NewLocalFile(mPluginPath, getter_AddRefs(libFile));
  NS_ENSURE_SUCCESS(rv, false);

  nsCOMPtr<nsIFile> parent;
  rv = libFile->GetParent(getter_AddRefs(parent));
  NS_ENSURE_SUCCESS(rv, false);

  nsAutoString parentLeafName;
  rv = parent->GetLeafName(parentLeafName);
  NS_ENSURE_SUCCESS(rv, false);

  aPluginName.Assign(NS_ConvertUTF16toUTF8(parentLeafName));
  return true;
}

static nsCOMPtr<nsIFile> AppendFile(nsCOMPtr<nsIFile>&& aFile,
                                    const nsString& aStr) {
  return (aFile && NS_SUCCEEDED(aFile->Append(aStr))) ? aFile : nullptr;
}

static nsCOMPtr<nsIFile> CloneFile(const nsCOMPtr<nsIFile>& aFile) {
  nsCOMPtr<nsIFile> clone;
  return (aFile && NS_SUCCEEDED(aFile->Clone(getter_AddRefs(clone)))) ? clone
                                                                      : nullptr;
}

static nsCOMPtr<nsIFile> GetParentFile(const nsCOMPtr<nsIFile>& aFile) {
  nsCOMPtr<nsIFile> parent;
  return (aFile && NS_SUCCEEDED(aFile->GetParent(getter_AddRefs(parent))))
             ? parent
             : nullptr;
}


#  define FIREFOX_FILE MOZ_APP_NAME u""_ns
#define XUL_LIB_FILE XUL_DLL u""_ns

static nsCOMPtr<nsIFile> GetFirefoxAppPath(
    nsCOMPtr<nsIFile> aPluginContainerPath) {
  MOZ_ASSERT(aPluginContainerPath);
  // On MacOS the firefox binary is a few parent directories up from
  // plugin-container.
  // aPluginContainerPath will end with something like:
  // xxxx/NightlyDebug.app/Contents/MacOS/plugin-container.app/Contents/MacOS/plugin-container
  nsCOMPtr<nsIFile> path = aPluginContainerPath;
  for (int i = 0; i < 4; i++) {
    path = GetParentFile(path);
  }
  return path;
}

static bool GetSigPath(const int aRelativeLayers,
                       const nsString& aTargetSigFileName,
                       nsCOMPtr<nsIFile> aExecutablePath,
                       nsCOMPtr<nsIFile>& aOutSigPath) {
  // The sig file will be located in
  // xxxx/NightlyDebug.app/Contents/Resources/XUL.sig
  // xxxx/NightlyDebug.app/Contents/Resources/firefox.sig
  // xxxx/NightlyDebug.app/Contents/MacOS/plugin-container.app/Contents/Resources/plugin-container.sig
  // On MacOS the sig file is a few parent directories up from
  // its executable file.
  // Start to search the path from the path of the executable file we provided.
  MOZ_ASSERT(aExecutablePath);
  nsCOMPtr<nsIFile> path = aExecutablePath;
  for (int i = 0; i < aRelativeLayers; i++) {
    nsCOMPtr<nsIFile> parent;
    if (NS_WARN_IF(NS_FAILED(path->GetParent(getter_AddRefs(parent))))) {
      return false;
    }
    path = parent;
  }
  MOZ_ASSERT(path);
  aOutSigPath = path;
  return NS_SUCCEEDED(path->Append(u"Resources"_ns)) &&
         NS_SUCCEEDED(path->Append(aTargetSigFileName));
}

static bool AppendHostPath(nsCOMPtr<nsIFile>& aFile,
                           nsTArray<std::pair<nsCString, nsCString>>& aPaths) {
  nsString str;
  if (!FileExists(aFile) || NS_FAILED(aFile->GetPath(str))) {
    return false;
  }

  nsCString filePath = NS_ConvertUTF16toUTF8(str);
  nsCString sigFilePath;
  nsAutoString binary;
  if (NS_FAILED(aFile->GetLeafName(binary))) {
    return false;
  }
  binary.Append(u".sig"_ns);
  nsCOMPtr<nsIFile> sigFile;
  if (GetSigPath(2, binary, aFile, sigFile) &&
      NS_SUCCEEDED(sigFile->GetPath(str))) {
    CopyUTF16toUTF8(str, sigFilePath);
  } else {
    // Cannot successfully get the sig file path.
    // Assume it is located at the same place as plugin-container
    // alternatively.
    sigFilePath = nsCString(NS_ConvertUTF16toUTF8(str) + ".sig"_ns);
  }
  aPaths.AppendElement(
      std::make_pair(std::move(filePath), std::move(sigFilePath)));
  return true;
}

nsTArray<std::pair<nsCString, nsCString>>
GMPChild::MakeCDMHostVerificationPaths(const nsACString& aPluginLibPath) {
  // Record the file path and its sig file path.
  nsTArray<std::pair<nsCString, nsCString>> paths;
  // Plugin binary path.
  paths.AppendElement(
      std::make_pair(nsCString(aPluginLibPath), aPluginLibPath + ".sig"_ns));

  // Current process binary path.
  // Note: clang won't let us initialize an nsString from a wstring, so we
  // need to go through UTF8 to get to an nsString.
  const std::string currentProcessBinary =
      WideToUTF8(CommandLine::ForCurrentProcess()->program());
  nsString str;

  CopyUTF8toUTF16(nsDependentCString(currentProcessBinary.c_str()), str);
  nsCOMPtr<nsIFile> path;
  if (NS_FAILED(NS_NewLocalFile(str, getter_AddRefs(path))) ||
      !AppendHostPath(path, paths)) {
    // Without successfully determining our path, we can't determine libxul's or
    // Firefox's. So give up.
    return paths;
  }

  bool addFirefoxBinaryPath = true;

  // Firefox application binary path.
  nsCOMPtr<nsIFile> appDir = GetFirefoxAppPath(path);
  if (addFirefoxBinaryPath) {
    path = AppendFile(CloneFile(appDir), FIREFOX_FILE);
    if (!AppendHostPath(path, paths)) {
      return paths;
    }
  }

  // Libxul path. Note: re-using 'appDir' var here, as we assume libxul is in
  // the same directory as Firefox executable.
  path = AppendFile(CloneFile(appDir), XUL_LIB_FILE);
  if (!AppendHostPath(path, paths)) {
    return paths;
  }

  return paths;
}

static auto ToCString(const nsTArray<std::pair<nsCString, nsCString>>& aPairs) {
  return StringJoin(","_ns, aPairs, [](nsACString& dest, const auto& p) {
    dest.AppendPrintf("(%s,%s)", p.first.get(), p.second.get());
  });
}

mozilla::ipc::IPCResult GMPChild::RecvStartPlugin(const nsString& aAdapter) {
  GMP_CHILD_LOG_DEBUG("{}", __FUNCTION__);

  nsAutoCString libPath;
  if (!GetUTF8LibPath(libPath)) {
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::GMPLibraryPath,
        NS_ConvertUTF16toUTF8(mPluginPath));

    return IPC_FAIL(this, "Failed to get lib path.");
  }

  auto platformAPI = new GMPPlatformAPI();
  InitPlatformAPI(*platformAPI, this);

  mGMPLoader = MakeUnique<GMPLoader>();

  GMPAdapter* adapter = nullptr;
  if (aAdapter.EqualsLiteral("chromium")) {
    auto&& paths = MakeCDMHostVerificationPaths(libPath);
    GMP_CHILD_LOG_DEBUG("{} CDM host paths={}", __func__,
                        ToCString(paths).get());
    adapter = new ChromiumCDMAdapter(std::move(paths));
  }

  if (!mGMPLoader->Load(libPath.get(), libPath.Length(), platformAPI,
                        adapter)) {
    NS_WARNING("Failed to load GMP");
    delete platformAPI;
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::GMPLibraryPath,
        NS_ConvertUTF16toUTF8(mPluginPath));

    return IPC_FAIL(this, "Failed to load GMP.");
  }

  return IPC_OK();
}

MessageLoop* GMPChild::GMPMessageLoop() { return mGMPMessageLoop; }

void GMPChild::ActorDestroy(ActorDestroyReason aWhy) {
  GMP_CHILD_LOG_DEBUG("{} reason={}", __FUNCTION__, static_cast<int>(aWhy));


  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    MOZ_ASSERT_IF(aWhy == NormalShutdown,
                  !mGMPContentChildren[i - 1]->IsUsed());
    mGMPContentChildren[i - 1]->Close();
  }

  if (ShutdownPlatformAPI() ==
      PlatformShutdownResult::PluginExecutionOutstanding) {
    ProcessChild::QuickExit();
    return;
  }

  if (mGMPLoader) {
    mGMPLoader->Shutdown();
  }

  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Abnormal shutdown of GMP process!");
    ProcessChild::QuickExit();
  }

  // Send the last bits of Glean data over to the main process.
  glean::FlushFOGData(
      [](ByteBuf&& aBuf) { glean::SendFOGData(std::move(aBuf)); });

  if (mProfilerController) {
    mProfilerController->Shutdown();
    mProfilerController = nullptr;
  }

  CrashReporterClient::DestroySingleton();

  XRE_ShutdownChildProcess();
}

void GMPChild::ProcessingError(Result aCode, const char* aReason) {
  switch (aCode) {
    case MsgDropped:
      NS_WARNING("MsgDropped in GMPChild");
      return;
    case MsgNotKnown:
      MOZ_CRASH("aborting because of MsgNotKnown");
    case MsgNotAllowed:
      MOZ_CRASH("aborting because of MsgNotAllowed");
    case MsgPayloadError:
      MOZ_CRASH("aborting because of MsgPayloadError");
    case MsgProcessingError:
      MOZ_CRASH("aborting because of MsgProcessingError");
    case MsgValueError:
      MOZ_CRASH("aborting because of MsgValueError");
    default:
      MOZ_CRASH("not reached");
  }
}

GMPTimerChild* GMPChild::GetGMPTimers() {
  if (auto* timer = SingleManagedOrNull(ManagedPGMPTimerChild())) {
    return static_cast<GMPTimerChild*>(timer);
  }
  auto timerChild = MakeRefPtr<GMPTimerChild>(this);
  if (!SendPGMPTimerConstructor(timerChild)) {
    return nullptr;
  }
  return timerChild.get();
}

GMPStorageChild* GMPChild::GetGMPStorage() {
  if (auto* storage = SingleManagedOrNull(ManagedPGMPStorageChild())) {
    return static_cast<GMPStorageChild*>(storage);
  }
  auto storageChild = MakeRefPtr<GMPStorageChild>(this);
  if (!SendPGMPStorageConstructor(storageChild)) {
    return nullptr;
  }
  return storageChild.get();
}

mozilla::ipc::IPCResult GMPChild::RecvCrashPluginNow() {
  MOZ_CRASH();
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvCloseActive() {
  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    mGMPContentChildren[i - 1]->CloseActive();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvInitGMPContentChild(
    Endpoint<PGMPContentChild>&& aEndpoint) {
  GMPContentChild* child =
      mGMPContentChildren.AppendElement(new GMPContentChild(this))->get();
  aEndpoint.Bind(child);
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvFlushFOGData(
    FlushFOGDataResolver&& aResolver) {
  GMP_CHILD_LOG_DEBUG("GMPChild RecvFlushFOGData");
  glean::FlushFOGData(std::move(aResolver));
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvTestTriggerMetrics(
    TestTriggerMetricsResolver&& aResolve) {
  GMP_CHILD_LOG_DEBUG("GMPChild RecvTestTriggerMetrics");
  mozilla::glean::test_only_ipc::a_counter.Add(
      nsIXULRuntime::PROCESS_TYPE_GMPLUGIN);
  aResolve(true);
  return IPC_OK();
}

void GMPChild::GMPContentChildActorDestroy(GMPContentChild* aGMPContentChild) {
  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    RefPtr<GMPContentChild>& destroyedActor = mGMPContentChildren[i - 1];
    if (destroyedActor.get() == aGMPContentChild) {
      SendPGMPContentChildDestroyed();
      mGMPContentChildren.RemoveElementAt(i - 1);
      break;
    }
  }
}

mozilla::ipc::IPCResult GMPChild::RecvInitProfiler(
    Endpoint<PProfilerChild>&& aEndpoint) {
  mProfilerController =
      mozilla::ChildProfilerController::Create(std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvPreferenceUpdate(const Pref& aPref) {
  Preferences::SetPreference(aPref);
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvShutdown(ShutdownResolver&& aResolver) {
  if (!mProfilerController) {
    aResolver(ProfileAndAdditionalInformation{});
    return IPC_OK();
  }

  const bool isProfiling = profiler_is_active();
  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::ProfilerChildShutdownPhase,
      isProfiling ? "Profiling - GrabShutdownProfileAndShutdown"
                  : "Not profiling - GrabShutdownProfileAndShutdown");
  ProfileAndAdditionalInformation shutdownProfileAndAdditionalInformation =
      mProfilerController->GrabShutdownProfileAndShutdown();
  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::ProfilerChildShutdownPhase,
      isProfiling ? "Profiling - Destroying ChildProfilerController"
                  : "Not profiling - Destroying ChildProfilerController");
  mProfilerController = nullptr;
  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::ProfilerChildShutdownPhase,
      isProfiling ? "Profiling - SendShutdownProfile (resovling)"
                  : "Not profiling - SendShutdownProfile (resolving)");
  if (const size_t len = shutdownProfileAndAdditionalInformation.SizeOf();
      len >= size_t(IPC::Channel::kMaximumMessageSize)) {
    shutdownProfileAndAdditionalInformation.mProfile =
        nsPrintfCString("*Profile from pid %u bigger (%zu) than IPC max (%zu)",
                        unsigned(profiler_current_process_id().ToNumber()), len,
                        size_t(IPC::Channel::kMaximumMessageSize));
    shutdownProfileAndAdditionalInformation.mAdditionalInformation.reset();
  }
  // Send the shutdown profile to the parent process through our own
  // message channel, which we know will survive for long enough.
  aResolver(std::move(shutdownProfileAndAdditionalInformation));
  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::ProfilerChildShutdownPhase,
      isProfiling ? "Profiling - SendShutdownProfile (resolved)"
                  : "Not profiling - SendShutdownProfile (resolved)");
  return IPC_OK();
}


}  // namespace gmp
}  // namespace mozilla

#undef GMP_CHILD_LOG_DEBUG
#undef __CLASS__
