/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoChildProcessHost.h"

#include "base/command_line.h"
#include "base/process.h"
#include "base/process_util.h"
#include "base/string_util.h"
#include "base/task.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/process_watcher.h"
#  include <mach/mach_traps.h>
#  include "base/rand_util.h"
#  include "chrome/common/mach_ipc_mac.h"
#  include "mozilla/StaticPrefs_dom.h"
#  include "mozilla/StaticPrefs_layers.h"
#  include "mozilla/StaticPrefs_media.h"
#  include <bsm/libbsm.h>
#  include <servers/bootstrap.h>
#  include "mozilla/EnumeratedArray.h"
#  include "nsILocalFileMac.h"

#include "GeckoProfiler.h"
#include "MainThreadUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/Sprintf.h"
#include "nsXPCOMPrivate.h"
#include "prenv.h"
#include "prerror.h"

#if defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#  include "nsAppDirectoryServiceDefs.h"
#endif

#include "ProtocolUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/GeckoArgs.h"
#include "mozilla/Omnijar.h"
#include "mozilla/RDDProcessHost.h"
#include "mozilla/Services.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/glean/IpcMetrics.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/ipc/IOThread.h"
#include "mozilla/ipc/EnvironmentMap.h"
#include "mozilla/ipc/NodeController.h"
#include "mozilla/net/SocketProcessHost.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsExceptionHandler.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsPrintfCString.h"



#  include "nsMacUtilsImpl.h"

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "GMPProcessParent.h"
#  include "mozilla/gfx/GPUProcessHost.h"
#endif

#include "mozilla/ipc/UtilityProcessHost.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"

#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsNativeCharsetUtils.h"
#include "nsTArray.h"
#include "nscore.h"  // for NS_FREE_PERMANENT_DATA
#include "nsIThread.h"

using mozilla::MonitorAutoLock;
using mozilla::Preferences;
using mozilla::StaticMutexAutoLock;


#ifdef MOZ_ENABLE_FORKSERVER
#  include "mozilla/ipc/ForkServiceChild.h"
#endif

static bool ShouldHaveDirectoryService() {
  return GeckoProcessType_Default == XRE_GetProcessType();
}


namespace mozilla {
namespace ipc {

LazyLogModule gChildProcessLifecycleLog("ChildProcessLifecycle");

struct LaunchResults {
  base::ProcessHandle mHandle = 0;
  task_t mChildTask = MACH_PORT_NULL;
};
typedef mozilla::MozPromise<LaunchResults, LaunchError, true>
    ProcessLaunchPromise;

// Monotonic counter used to generate a unique ChildID for each process as it is
// created. The parent process is given the ChildID of `0`, and each child
// process is given a non-zero ID.
static Atomic<int32_t> gChildCounter;

class BaseProcessLauncher {
 public:
  BaseProcessLauncher(GeckoChildProcessHost* aHost,
                      geckoargs::ChildProcessArgs&& aExtraOpts)
      : mProcessType(aHost->mProcessType),
        mLaunchOptions(std::move(aHost->mLaunchOptions)),
        mChildArgs(std::move(aExtraOpts)),
        mUtilitySandbox(aHost->mUtilitySandbox)
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
        ,
        mDisableOSActivityMode(aHost->mDisableOSActivityMode)
#endif
  {
    aHost->mInitialChannelId.ToProvidedString(mInitialChannelIdString);
    mChildID = aHost->mChildID;
    SprintfLiteral(mChildIDString, "%d", aHost->mChildID);

    // Compute the serial event target we'll use for launching.
    mLaunchThread = GetIPCLauncher();

    if (ShouldHaveDirectoryService()) {
      // "Current process directory" means the app dir, not the current
      // working dir or similar.
      (void)nsDirectoryService::gService->GetCurrentProcessDirectory(
          getter_AddRefs(mAppDir));
    }
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(BaseProcessLauncher);

#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  void SetLaunchArchitecture(uint32_t aLaunchArch) {
    mLaunchArch = aLaunchArch;
  }
#endif

  RefPtr<ProcessLaunchPromise> Launch(GeckoChildProcessHost*);

 protected:
  virtual ~BaseProcessLauncher() = default;

  RefPtr<ProcessLaunchPromise> PerformAsyncLaunch();

  // Overrideable hooks. If superclass behavior is invoked, it's always at the
  // top of the override.
  virtual Result<Ok, LaunchError> DoSetup();
  virtual RefPtr<ProcessLaunchPromise> DoLaunch() = 0;

  void MapChildLogging();

  static BinPathType GetPathToBinary(FilePath&, GeckoProcessType,
                                     SandboxingKind sandboxKind);

  void GetChildLogName(const char* origLogName, nsACString& buffer);

  const char* ChildProcessType() {
    return XRE_GeckoProcessTypeToString(mProcessType);
  }

  nsCOMPtr<nsISerialEventTarget> mLaunchThread;
  GeckoProcessType mProcessType;
  UniquePtr<base::LaunchOptions> mLaunchOptions;
#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  uint32_t mLaunchArch = base::PROCESS_ARCH_INVALID;
#endif
  geckoargs::ChildProcessArgs mChildArgs;
  SandboxingKind mUtilitySandbox;
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Controls whether or not the process will be launched with
  // environment variable OS_ACTIVITY_MODE set to "disabled".
  bool mDisableOSActivityMode;
#endif
  LaunchResults mResults = LaunchResults();
  char mInitialChannelIdString[NSID_LENGTH];
  GeckoChildID mChildID;
  char mChildIDString[32];

  // Set during launch.
  nsCOMPtr<nsIFile> mAppDir;
};


class PosixProcessLauncher : public BaseProcessLauncher {
 public:
  PosixProcessLauncher(GeckoChildProcessHost* aHost,
                       geckoargs::ChildProcessArgs&& aExtraOpts)
      : BaseProcessLauncher(aHost, std::move(aExtraOpts)),
        mProfileDir(aHost->mProfileDir) {}

 protected:
  virtual Result<Ok, LaunchError> DoSetup() override;
  virtual RefPtr<ProcessLaunchPromise> DoLaunch() override;

  nsCOMPtr<nsIFile> mProfileDir;
};

class MacProcessLauncher : public PosixProcessLauncher {
 public:
  MacProcessLauncher(GeckoChildProcessHost* aHost,
                     geckoargs::ChildProcessArgs&& aExtraOpts)
      : PosixProcessLauncher(aHost, std::move(aExtraOpts)),
        // Put a random number into the channel name, so that
        // a compromised renderer can't pretend being the child
        // that's forked off.
        mMachConnectionName(
            StringPrintf("org.mozilla.machname.%d",
                         base::RandInt(0, std::numeric_limits<int>::max()))) {
    MOZ_ASSERT(mMachConnectionName.size() < BOOTSTRAP_MAX_NAME_LEN);
  }

 protected:
  virtual RefPtr<ProcessLaunchPromise> DoLaunch() override;

  std::string mMachConnectionName;
  // We add a mach port to the command line so the child can communicate its
  // 'task_t' back to the parent.
  mozilla::UniqueMachReceiveRight mParentRecvPort;

  friend class PosixProcessLauncher;
};
typedef MacProcessLauncher ProcessLauncher;

using base::ProcessHandle;
using mozilla::ipc::BaseProcessLauncher;
using mozilla::ipc::ProcessLauncher;

static StaticMutex sGeckoChildProcessHostsMutex;
static StaticAutoPtr<nsTArray<ThreadSafeWeakPtr<GeckoChildProcessHost>>>
    sGeckoChildProcessHosts MOZ_GUARDED_BY(sGeckoChildProcessHostsMutex);

GeckoChildProcessHost::GeckoChildProcessHost(GeckoProcessType aProcessType,
                                             bool aIsFileContent)
    : mProcessType(aProcessType),
      mChildID(++gChildCounter),
      mIsFileContent(aIsFileContent),
      mMonitor("mozilla.ipc.GeckoChildProcessHost.mMonitor"),
      mLaunchOptions(MakeUnique<base::LaunchOptions>()),
      mInitialChannelId(nsID::GenerateUUID()),
      mProcessState(CREATING_CHANNEL),
      mHandleLock("mozilla.ipc.GeckoChildProcessHost.mHandleLock"),
      mChildProcessHandle(0)
      ,
      mChildTask(MACH_PORT_NULL)
#  if defined(MOZ_SANDBOX)
      ,
      mDisableOSActivityMode(false)
#  endif
{
  MOZ_COUNT_CTOR(GeckoChildProcessHost);
  MOZ_RELEASE_ASSERT(mChildID > 0, "gChildCounter overflowed");
}

GeckoChildProcessHost::~GeckoChildProcessHost() {
  MOZ_COUNT_DTOR(GeckoChildProcessHost);

  // Clean up the weak pointer from the global GeckoChildProcessHost array.
  {
    StaticMutexAutoLock lock(sGeckoChildProcessHostsMutex);
    if (sGeckoChildProcessHosts) {
      sGeckoChildProcessHosts->RemoveElement(this);
      if (sGeckoChildProcessHosts->IsEmpty()) {
        sGeckoChildProcessHosts = nullptr;
      }
    }
  }

  {
    mozilla::AutoWriteLock hLock(mHandleLock);
    if (mChildTask != MACH_PORT_NULL) {
      mach_port_deallocate(mach_task_self(), mChildTask);
    }

    if (mChildProcessHandle != 0) {
      MOZ_LOG(
          gChildProcessLifecycleLog, LogLevel::Info,
          ("--PROCESS [pid = %" PRIPID "] [childID = %" PRIi32 "] [type = %s]",
           base::GetProcId(mChildProcessHandle), mChildID,
           XRE_GeckoProcessTypeToString(mProcessType)));

      // If we still have a child process handle, asynchronously dispatch a task
      // to ensure that the child process is reaped to the background thread.
      nsCOMPtr<nsIRunnable> ensureProcessTerminated = NS_NewRunnableFunction(
          "GeckoChildProcessHost EnsureProcessTerminated",
          [handle = mChildProcessHandle] {
            ProcessWatcher::EnsureProcessTerminated(handle);
          });
      if (XRE_GetAsyncIOEventTarget()->IsOnCurrentThread()) {
        ensureProcessTerminated->Run();
      } else {
        MOZ_ALWAYS_SUCCEEDS(XRE_GetAsyncIOEventTarget()->Dispatch(
            ensureProcessTerminated.forget()));
      }
      mChildProcessHandle = 0;
    }
  }
}

base::ProcessHandle GeckoChildProcessHost::GetChildProcessHandle() {
  mozilla::AutoReadLock handleLock(mHandleLock);
  return mChildProcessHandle;
}

base::ProcessId GeckoChildProcessHost::GetChildProcessId() {
  mozilla::AutoReadLock handleLock(mHandleLock);
  if (!mChildProcessHandle) {
    return 0;
  }
  return base::GetProcId(mChildProcessHandle);
}

task_t GeckoChildProcessHost::GetChildTask() {
  mozilla::AutoReadLock handleLock(mHandleLock);
  return mChildTask;
}


static mozilla::StaticMutex sCachedExecutablePathMutex;
static mozilla::EnumeratedArray<MacOSChildProcessBundleType, const char*,
                                size_t(MacOSChildProcessBundleType::Count)>
    sCachedExecutablePaths MOZ_GUARDED_BY(sCachedExecutablePathMutex);

// To support different child process types having different codesigning
// entitlements, different executables are used to run some child process
// types.
static MacOSChildProcessBundleType GetMacOSChildProcessBundleType(
    GeckoProcessType processType, SandboxingKind utilitySandbox) {
  if (processType == GeckoProcessType_GMPlugin &&
      mozilla::StaticPrefs::media_plugin_helper_process_enabled()) {
    return MacOSChildProcessBundleType::EME;
  }
  if (processType == GeckoProcessType_GPU &&
      mozilla::StaticPrefs::layers_gpu_process_executable_enabled()) {
    return MacOSChildProcessBundleType::GPU;
  }
#  if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  if (processType == GeckoProcessType_Utility &&
      utilitySandbox == PKCS11_MODULE) {
    return MacOSChildProcessBundleType::PKCS11;
  }
#  endif
  return MacOSChildProcessBundleType::Default;
}

static nsLiteralCString GetBundleNameForType(
    MacOSChildProcessBundleType bundleType) {
  switch (bundleType) {
    case MacOSChildProcessBundleType::EME:
      return nsLiteralCString(MOZ_EME_PROCESS_BUNDLENAME);
    case MacOSChildProcessBundleType::GPU:
      return nsLiteralCString(MOZ_GPU_PROCESS_BUNDLENAME);
#  if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
    case MacOSChildProcessBundleType::PKCS11:
      return nsLiteralCString(MOZ_PKCS11_PROCESS_BUNDLENAME);
#  else
    case MacOSChildProcessBundleType::PKCS11:
#  endif
    case MacOSChildProcessBundleType::Default:
      return nsLiteralCString(MOZ_CHILD_PROCESS_BUNDLENAME);
    case MacOSChildProcessBundleType::Count:
      break;
  }
  MOZ_CRASH("Unexpected MacOSChildProcessBundleType");
}

#  define BUNDLE_EXEC_PATH(name)    \
    MOZ_##name##_PROCESS_BUNDLENAME \
        "/Contents/MacOS/" MOZ_##name##_PROCESS_NAME_BRANDED

// The relative path from the GRE directory to the child process executable.
static const char* GetBundleExecutablePathForType(
    MacOSChildProcessBundleType bundleType) {
  switch (bundleType) {
    case MacOSChildProcessBundleType::EME:
      return BUNDLE_EXEC_PATH(EME);
    case MacOSChildProcessBundleType::GPU:
      return BUNDLE_EXEC_PATH(GPU);
#  if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
    case MacOSChildProcessBundleType::PKCS11:
      return BUNDLE_EXEC_PATH(PKCS11);
#  else
    case MacOSChildProcessBundleType::PKCS11:
#  endif
    // plugin-container is unbranded, so it has no MOZ_*_PROCESS_NAME_BRANDED.
    case MacOSChildProcessBundleType::Default:
      return MOZ_CHILD_PROCESS_BUNDLENAME
          "/Contents/MacOS/" MOZ_CHILD_PROCESS_NAME;
    case MacOSChildProcessBundleType::Count:
      break;
  }
  MOZ_CRASH("Unexpected MacOSChildProcessBundleType");
}

#  undef BUNDLE_EXEC_PATH

// Build the executable path from a single macro containing the
// full relative path from the GRE directory to the executable.
static void GetExecutablePathFromBranding(
    FilePath& aExecutablePath, MacOSChildProcessBundleType aBundleType) {
  const char* executablePath = GetBundleExecutablePathForType(aBundleType);

  if (ShouldHaveDirectoryService()) {
    MOZ_ASSERT(gGREBinPath);
    nsCOMPtr<nsIFile> childProcPath;
    if (NS_SUCCEEDED(NS_NewLocalFile(nsDependentString(gGREBinPath),
                                     getter_AddRefs(childProcPath)))) {
      if (NS_SUCCEEDED(childProcPath->AppendRelativeNativePath(
              nsDependentCString(executablePath)))) {
        nsCString tempCPath;
        if (NS_SUCCEEDED(childProcPath->GetNativePath(tempCPath))) {
          aExecutablePath = FilePath(tempCPath.get());
        }
      }
    }
  }

  if (aExecutablePath.empty()) {
    aExecutablePath = FilePath(CommandLine::ForCurrentProcess()->argv()[0]);
    aExecutablePath = aExecutablePath.DirName();
    aExecutablePath = aExecutablePath.Append(executablePath);
  }
}

// Try to resolve the executable path from the bundle's Info.plist.
static bool GetExecutablePathFromPlist(
    FilePath& aExecutablePath, MacOSChildProcessBundleType aBundleType) {
  if (!ShouldHaveDirectoryService()) {
    return false;
  }

  MOZ_ASSERT(gGREBinPath);

  nsCOMPtr<nsIFile> bundlePath;
  if (NS_FAILED(NS_NewLocalFile(nsDependentString(gGREBinPath),
                                getter_AddRefs(bundlePath)))) {
    return false;
  }

  const nsLiteralCString bundleName = GetBundleNameForType(aBundleType);
  if (NS_FAILED(bundlePath->AppendNative(bundleName))) {
    return false;
  }

  nsCString bundlePathStr = bundlePath->NativePath();

  nsAutoCString executablePath;
  if (NS_FAILED(nsMacUtilsImpl::GetExecutablePathFromBundle(bundlePathStr,
                                                            executablePath))) {
    return false;
  }

  aExecutablePath = FilePath(executablePath.get());
  return true;
}

static void GetPathToBinaryMacOS(FilePath& aExecutablePath,
                                 GeckoProcessType aProcessType,
                                 SandboxingKind aUtilitySandbox) {
  MacOSChildProcessBundleType bundleType =
      GetMacOSChildProcessBundleType(aProcessType, aUtilitySandbox);

  {
    StaticMutexAutoLock lock(sCachedExecutablePathMutex);
    if (sCachedExecutablePaths[bundleType]) {
      aExecutablePath = FilePath(sCachedExecutablePaths[bundleType]);
      return;
    }
  }

  bool resolved = false;
  if (mozilla::StaticPrefs::dom_ipc_processLaunch_useMacPlists_AtStartup()) {
    resolved = GetExecutablePathFromPlist(aExecutablePath, bundleType);
    if (!resolved) {
      MOZ_LOG(gChildProcessLifecycleLog, LogLevel::Warning,
              ("Could not read the executable name from %s; falling back to "
               "compile-time branding, which may not match the files on disk",
               GetBundleNameForType(bundleType).get()));
    }
  }
  if (!resolved) {
    GetExecutablePathFromBranding(aExecutablePath, bundleType);
  }

  StaticMutexAutoLock lock(sCachedExecutablePathMutex);
  if (!sCachedExecutablePaths[bundleType]) {
    sCachedExecutablePaths[bundleType] =
        moz_xstrdup(aExecutablePath.value().c_str());
  }
}

nsLiteralCString GetChildProcessBundleNameForTesting(
    MacOSChildProcessBundleType aBundleType) {
  return GetBundleNameForType(aBundleType);
}

bool ResolveChildProcessPathFromPlistForTesting(
    MacOSChildProcessBundleType aBundleType, nsACString& aExecutablePath) {
  FilePath executablePath;
  if (!GetExecutablePathFromPlist(executablePath, aBundleType)) {
    return false;
  }

  aExecutablePath.Assign(executablePath.value().c_str());
  return true;
}


// static
mozilla::BinPathType BaseProcessLauncher::GetPathToBinary(
    FilePath& exePath, GeckoProcessType processType,
    SandboxingKind utilitySandbox) {
  exePath = {};
  BinPathType pathType = XRE_GetChildProcBinPathType(processType);

  if (pathType == BinPathType::Self) {
    exePath = FilePath(CommandLine::ForCurrentProcess()->argv()[0]);
    return pathType;
  }

  GetPathToBinaryMacOS(exePath, processType, utilitySandbox);

  return pathType;
}

class AutoCFTypeObject {
 public:
  explicit AutoCFTypeObject(CFTypeRef object) { mObject = object; }
  ~AutoCFTypeObject() { ::CFRelease(mObject); }

 private:
  CFTypeRef mObject;
};

// We start the unique IDs at 1 so that 0 can be used to mean that
// a component has no unique ID assigned to it.
std::atomic<uint32_t> GeckoChildProcessHost::sNextUniqueID = 1;

/* static */
uint32_t GeckoChildProcessHost::GetUniqueID() { return sNextUniqueID++; }

/* static */
void GeckoChildProcessHost::SetEnv(const char* aKey, const char* aValue) {
  MOZ_ASSERT(mLaunchOptions);
  mLaunchOptions->env_map[ENVIRONMENT_STRING(aKey)] =
      ENVIRONMENT_STRING(aValue);
}

bool GeckoChildProcessHost::PrepareLaunch(
    geckoargs::ChildProcessArgs& aExtraOpts) {

#  if defined(MOZ_SANDBOX)
  if (ShouldHaveDirectoryService() &&
      mProcessType != GeckoProcessType_GMPlugin) {
    (void)NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                 getter_AddRefs(mProfileDir));
  }
#  endif

  return true;
}


bool GeckoChildProcessHost::SyncLaunch(geckoargs::ChildProcessArgs aExtraOpts,
                                       int aTimeoutMs) {
  if (!AsyncLaunch(std::move(aExtraOpts))) {
    return false;
  }
  return WaitUntilConnected(aTimeoutMs);
}

// Note: for most process types, we currently call AsyncLaunch, and therefore
// the *ProcessLauncher constructor, on the main thread, while the
// ProcessLauncher methods to actually execute the launch are called on the IO
// or IPC launcher thread. GMP processes are an exception - the GMP code
// invokes GeckoChildProcessHost from non-main-threads, and therefore we cannot
// rely on having access to mainthread-only services (like the directory
// service) from this code if we're launching that type of process.
bool GeckoChildProcessHost::AsyncLaunch(
    geckoargs::ChildProcessArgs aExtraOpts) {
  if (!PrepareLaunch(aExtraOpts)) {
    return false;
  }

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  if (IsMacSandboxLaunchEnabled() &&
      !AppendMacSandboxParams(aExtraOpts.mArgs)) {
    return false;
  }
#endif

  RefPtr<BaseProcessLauncher> launcher =
      MakeRefPtr<ProcessLauncher>(this, std::move(aExtraOpts));
  TimeStamp startTimeStamp = TimeStamp::Now();
#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  launcher->SetLaunchArchitecture(mLaunchArch);
#endif

  if (!mHandlePromise) {
    StaticMutexAutoLock lock(sGeckoChildProcessHostsMutex);
    if (!sGeckoChildProcessHosts) {
      sGeckoChildProcessHosts =
          new nsTArray<ThreadSafeWeakPtr<GeckoChildProcessHost>>();
    }
    sGeckoChildProcessHosts->AppendElement(this);
  }

  MOZ_ASSERT(mHandlePromise == nullptr);
  mHandlePromise =
      mozilla::InvokeAsync<RefPtr<GeckoChildProcessHost>>(
          XRE_GetAsyncIOEventTarget(), launcher.get(), __func__,
          &BaseProcessLauncher::Launch, this)
          ->Then(
              XRE_GetAsyncIOEventTarget(), __func__,
              [self = RefPtr{this}, this,
               startTimeStamp](LaunchResults&& aResults) {
                {
                  {
                    mozilla::AutoWriteLock handleLock(mHandleLock);
                    if (!OpenPrivilegedHandle(base::GetProcId(aResults.mHandle))
                    ) {
                      MOZ_CRASH("cannot open handle to child process");
                    }
                    // The original handle is no longer needed; it must
                    // be closed to prevent a resource leak.
                    base::CloseProcessHandle(aResults.mHandle);
                    // FIXME (bug 1720523): define a cross-platform
                    // "safe" invalid value to use in places like this.
                    aResults.mHandle = 0;

                    this->mChildTask = aResults.mChildTask;

                    if (mNodeChannel) {
                      mNodeChannel->SetOtherPid(
                          base::GetProcId(this->mChildProcessHandle));
                      mNodeChannel->SetMachTaskPort(this->mChildTask);
                    }
                  }

                  MOZ_LOG(gChildProcessLifecycleLog, LogLevel::Info,
                          ("++PROCESS [pid = %" PRIPID "] [childID = %" PRIi32
                           "] [type = %s]",
                           GetChildProcessId(), mChildID,
                           XRE_GeckoProcessTypeToString(mProcessType)));

                  glean::process::child_launch.AccumulateRawDuration(
                      TimeStamp::Now() - startTimeStamp);

                  MonitorAutoLock lock(mMonitor);
                  // The OnChannel{Connected,Error} may have already advanced
                  // the state.
                  if (mProcessState < PROCESS_CREATED) {
                    mProcessState = PROCESS_CREATED;
                  }
                  lock.Notify();
                }
                return ProcessHandlePromise::CreateAndResolve(
                    GetChildProcessHandle(), __func__);
              },
              [self = RefPtr{this}, this](const LaunchError aError) {
                // WaitUntilConnected might be waiting for us to signal.
                // If something failed let's set the error state and notify.
                CHROMIUM_LOG(ERROR)
                    << "Failed to launch "
                    << XRE_GeckoProcessTypeToString(mProcessType)
                    << " subprocess @" << aError.FunctionName()
                    << " (Error:" << aError.ErrorCode() << ")";
                glean::subprocess::launch_failure
                    .Get(nsDependentCString(
                        XRE_GeckoProcessTypeToString(mProcessType)))
                    .Add(1);
                nsCString telemetryKey = nsPrintfCString(
                    "%s,%ld,%s",
                    aError.FunctionName().get(), aError.ErrorCode(),
                    XRE_GeckoProcessTypeToString(mProcessType));
                // Max telemetry key is 72 chars
                // https://searchfox.org/mozilla-central/rev/c244b16815d1fc827d141472b9faac5610f250e7/toolkit/components/telemetry/core/TelemetryScalar.cpp#105
                if (telemetryKey.Length() > 72) {
                  NS_WARNING(nsPrintfCString("Truncating telemetry key: %s",
                                             telemetryKey.get())
                                 .get());
                  telemetryKey.Truncate(72);
                }
                glean::dom_parentprocess::process_launch_errors
                    .Get(telemetryKey)
                    .Add(1);
                OnProcessLaunchError(aError);
                return ProcessHandlePromise::CreateAndReject(aError, __func__);
              });
  return true;
}

void GeckoChildProcessHost::OnProcessLaunchError(const LaunchError aError) {
  MonitorAutoLock lock(mMonitor);
  mProcessState = PROCESS_ERROR;
  lock.Notify();
}

bool GeckoChildProcessHost::WaitUntilConnected(int32_t aTimeoutMs) {
  AUTO_PROFILER_LABEL("GeckoChildProcessHost::WaitUntilConnected", OTHER);

  // NB: this uses a different mechanism than the chromium parent
  // class.
  TimeDuration timeout = (aTimeoutMs > 0)
                             ? TimeDuration::FromMilliseconds(aTimeoutMs)
                             : TimeDuration::Forever();

  MonitorAutoLock lock(mMonitor);
  TimeStamp waitStart = TimeStamp::Now();
  TimeStamp current;

  // We'll receive several notifications, we need to exit when we
  // have either successfully launched or have timed out.
  while (mProcessState != PROCESS_CONNECTED) {
    // If there was an error then return it, don't wait out the timeout.
    if (mProcessState == PROCESS_ERROR) {
      break;
    }

    CVStatus status = lock.Wait(timeout);
    if (status == CVStatus::Timeout) {
      break;
    }

    if (timeout != TimeDuration::Forever()) {
      current = TimeStamp::Now();
      timeout -= current - waitStart;
      waitStart = current;
    }
  }

  return mProcessState == PROCESS_CONNECTED;
}

bool GeckoChildProcessHost::WaitForProcessHandle() {
  MonitorAutoLock lock(mMonitor);
  while (mProcessState < PROCESS_CREATED) {
    lock.Wait();
  }
  MOZ_ASSERT(mProcessState == PROCESS_ERROR || GetChildProcessHandle());

  return mProcessState < PROCESS_ERROR;
}

bool GeckoChildProcessHost::LaunchAndWaitForProcessHandle(
    geckoargs::ChildProcessArgs aExtraOpts) {
  if (!AsyncLaunch(std::move(aExtraOpts))) {
    return false;
  }
  return WaitForProcessHandle();
}

bool GeckoChildProcessHost::InitializeChannel(
    IPC::Channel::ChannelHandle* aClientHandle) {
  mNodeController = NodeController::GetSingleton();
  if (!mNodeController->InviteChildProcess(this, aClientHandle, &mInitialPort,
                                           getter_AddRefs(mNodeChannel))) {
    return false;
  }

  MOZ_ASSERT(mInitialPort.IsValid());
  MOZ_ASSERT(mNodeChannel);

  MonitorAutoLock lock(mMonitor);
  mProcessState = CHANNEL_INITIALIZED;
  lock.Notify();
  return true;
}

void GeckoChildProcessHost::SetAlreadyDead() {
  mozilla::AutoWriteLock handleLock(mHandleLock);
  if (mChildProcessHandle &&
      mChildProcessHandle != base::kInvalidProcessHandle) {
    // The destructor logs this too, but only one of the two runs, as both are
    // guarded on still holding the handle.
    MOZ_LOG(
        gChildProcessLifecycleLog, LogLevel::Info,
        ("--PROCESS [pid = %" PRIPID "] [childID = %" PRIi32 "] [type = %s]",
         base::GetProcId(mChildProcessHandle), mChildID,
         XRE_GeckoProcessTypeToString(mProcessType)));

    base::CloseProcessHandle(mChildProcessHandle);
  }

  mChildProcessHandle = 0;
}

void BaseProcessLauncher::GetChildLogName(const char* origLogName,
                                          nsACString& buffer) {
  {
    buffer.Append(origLogName);
  }

  // Remove .moz_log extension to avoid its duplication, it will be added
  // automatically by the logging backend
  static constexpr auto kMozLogExt = nsLiteralCString{MOZ_LOG_FILE_EXTENSION};
  if (StringEndsWith(buffer, kMozLogExt)) {
    buffer.Truncate(buffer.Length() - kMozLogExt.Length());
  }

  // Append child-specific postfix to name
  buffer.AppendLiteral(".child-");
  buffer.AppendASCII(mChildIDString);
}

// We use a single dedicated thread for process launching. This is required on
// Windows (due to sandboxing code), Android (due to the java code) and Linux
// (due to ForkServerChild). Using a single thread is not required on macOS or
// iOS, but we use one to keep the implementation consistent.
//
// The only tier-1 platform where we do not need a dedicated thread is macOS,
// however we use a dedicated thread there as well for consistency and
// simplicity of implementation.

static mozilla::StaticMutex gIPCLaunchThreadMutex;
static mozilla::StaticRefPtr<nsIThread> gIPCLaunchThread
    MOZ_GUARDED_BY(gIPCLaunchThreadMutex);

class IPCLaunchThreadObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
 protected:
  virtual ~IPCLaunchThreadObserver() = default;
};

NS_IMPL_ISUPPORTS(IPCLaunchThreadObserver, nsIObserver, nsISupports)

NS_IMETHODIMP
IPCLaunchThreadObserver::Observe(nsISupports* aSubject, const char* aTopic,
                                 const char16_t* aData) {
  MOZ_RELEASE_ASSERT(strcmp(aTopic, "xpcom-shutdown-threads") == 0);

  nsCOMPtr<nsIThread> thread;
  {
    StaticMutexAutoLock lock(gIPCLaunchThreadMutex);
    thread = gIPCLaunchThread.forget();
  }

  nsresult rv = thread ? thread->Shutdown() : NS_OK;
  (void)NS_WARN_IF(NS_FAILED(rv));
  return rv;
}

nsCOMPtr<nsISerialEventTarget> GetIPCLauncher() {
  StaticMutexAutoLock lock(gIPCLaunchThreadMutex);
  if (!gIPCLaunchThread) {
    nsCOMPtr<nsIThread> thread;
    nsresult rv = NS_NewNamedThread("IPC Launch"_ns, getter_AddRefs(thread));
    if (!NS_WARN_IF(NS_FAILED(rv))) {
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("GeckoChildProcessHost::GetIPCLauncher", [] {
            nsCOMPtr<nsIObserverService> obsService =
                mozilla::services::GetObserverService();
            nsCOMPtr<nsIObserver> obs = new IPCLaunchThreadObserver();
            obsService->AddObserver(obs, "xpcom-shutdown-threads", false);
          }));
      gIPCLaunchThread = thread.forget();
    }
  }

  nsCOMPtr<nsISerialEventTarget> thread = gIPCLaunchThread.get();
  MOZ_DIAGNOSTIC_ASSERT(thread);
  return thread;
}

void
AddAppDirToCommandLine(geckoargs::ChildProcessArgs& aCmdLine,
                       nsIFile* aAppDir, nsIFile* aProfileDir)
{
  // Content processes need access to application resources, so pass
  // the full application directory path to the child process.
  if (aAppDir) {
    nsAutoCString path;
    MOZ_ALWAYS_SUCCEEDS(aAppDir->GetNativePath(path));
    geckoargs::sAppDir.Put(path.get(), aCmdLine);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
    // Full path to the profile dir
    if (aProfileDir) {
      // If the profile doesn't exist, normalization will
      // fail. But we don't return an error here because some
      // tests require startup with a missing profile dir.
      // For users, almost universally, the profile will be in
      // the home directory and normalization isn't required.
      (void)aProfileDir->Normalize();
      nsAutoCString path;
      MOZ_ALWAYS_SUCCEEDS(aProfileDir->GetNativePath(path));
      geckoargs::sProfile.Put(path.get(), aCmdLine);
    }
#endif
  }
}


RefPtr<ProcessLaunchPromise> BaseProcessLauncher::PerformAsyncLaunch() {
  Result<Ok, LaunchError> aError = DoSetup();
  if (aError.isErr()) {
    return ProcessLaunchPromise::CreateAndReject(aError.unwrapErr(), __func__);
  }

  // NOTE: We run this task on the IO event target as it should be cheap &
  // non-blocking, and the next step in our caller will be ->Then-ed onto this
  // event target, meaning this avoids unnecessary thread hops back to the
  // launch target on platforms like macOS.
  return DoLaunch()->Then(
      XRE_GetAsyncIOEventTarget(), __func__,
      [self =
           RefPtr{this}](ProcessLaunchPromise::ResolveOrRejectValue&& aResult) {
        // Explicitly destroy any outstanding references to HANDLEs which may be
        // held by mChildArgs before resolving.
        //
        // NOTE: This is technically redundant, as it will happen naturally as
        // the `BaseProcessLauncher` is destroyed, but this step makes it
        // explicit. If we leaked one of these HANDLEs, we could fail to detect
        // a child process shutting down or crashing.
        self->mChildArgs = {};
        return ProcessLaunchPromise::CreateAndResolveOrReject(
            std::move(aResult), __func__);
      });
}

Result<Ok, LaunchError> BaseProcessLauncher::DoSetup() {
  RefPtr<BaseProcessLauncher> self = this;
  GetProfilerEnvVarsForChildProcess([self](const char* key, const char* value) {
    self->mLaunchOptions->env_map[ENVIRONMENT_STRING(key)] =
        ENVIRONMENT_STRING(value);
  });
#ifdef MOZ_MEMORY
  if (mProcessType == GeckoProcessType_Content) {
    nsAutoCString mallocOpts(PR_GetEnv("MALLOC_OPTIONS"));
    // Disable randomization of small arenas in content.
    mallocOpts.Append("r");
    self->mLaunchOptions->env_map[ENVIRONMENT_LITERAL("MALLOC_OPTIONS")] =
        ENVIRONMENT_STRING(mallocOpts.get());
  }
#endif

  MapChildLogging();

  geckoargs::sInitialChannelID.Put(mInitialChannelIdString, mChildArgs);

  geckoargs::sParentPid.Put(static_cast<uint64_t>(base::GetCurrentProcId()),
                            mChildArgs);

  if (!CrashReporter::IsDummy() && CrashReporter::GetEnabled() &&
      mProcessType != GeckoProcessType_ForkServer) {
    if (!CrashReporter::RegisterChildIPCChannel(mChildArgs, mChildID)) {
      NS_WARNING("Could not create an IPC channel to the crash helper");
    }
  }

  return Ok();
}

void BaseProcessLauncher::MapChildLogging() {
  const char* origNSPRLogName = PR_GetEnv("NSPR_LOG_FILE");
  const char* origMozLogName = PR_GetEnv("MOZ_LOG_FILE");

  if (origNSPRLogName) {
    nsAutoCString nsprLogName;
    GetChildLogName(origNSPRLogName, nsprLogName);
    mLaunchOptions->env_map[ENVIRONMENT_LITERAL("NSPR_LOG_FILE")] =
        ENVIRONMENT_STRING(nsprLogName.get());
  }
  if (origMozLogName) {
    nsAutoCString mozLogName;
    GetChildLogName(origMozLogName, mozLogName);
    mLaunchOptions->env_map[ENVIRONMENT_LITERAL("MOZ_LOG_FILE")] =
        ENVIRONMENT_STRING(mozLogName.get());
  }

  // `RUST_LOG_CHILD` is meant for logging child processes only.
  nsAutoCString childRustLog(PR_GetEnv("RUST_LOG_CHILD"));
  if (!childRustLog.IsEmpty()) {
    mLaunchOptions->env_map[ENVIRONMENT_LITERAL("RUST_LOG")] =
        ENVIRONMENT_STRING(childRustLog.get());
  }
}


Result<Ok, LaunchError> PosixProcessLauncher::DoSetup() {
  Result<Ok, LaunchError> aError = BaseProcessLauncher::DoSetup();
  if (aError.isErr()) {
    return aError;
  }

  // XPCOM may not be initialized in some subprocesses.  We don't want
  // to initialize XPCOM just for the directory service, especially
  // since LD_LIBRARY_PATH is already set correctly in subprocesses
  // (meaning that we don't need to set that up in the environment).
  if (ShouldHaveDirectoryService()) {
    MOZ_ASSERT(gGREBinPath);
    nsCString path;
    NS_CopyUnicodeToNative(nsDependentString(gGREBinPath), path);
#  if defined(XP_LINUX) || defined(__DragonFly__) || defined(XP_FREEBSD) || \
      defined(XP_NETBSD) || defined(XP_OPENBSD)
    const char* ld_library_path = PR_GetEnv("LD_LIBRARY_PATH");
    nsCString new_ld_lib_path(path.get());

    if (ld_library_path && *ld_library_path) {
      new_ld_lib_path.Append(':');
      new_ld_lib_path.Append(ld_library_path);
    }
    mLaunchOptions->env_map["LD_LIBRARY_PATH"] = new_ld_lib_path.get();

#  else
    // With signed production Mac builds, the dynamic linker (dyld) will
    // ignore dyld environment variables preventing the use of variables
    // such as DYLD_LIBRARY_PATH and DYLD_INSERT_LIBRARIES.

    // If we're running with gtests, add the gtest XUL ahead of normal XUL on
    // the DYLD_LIBRARY_PATH so that plugin-container.app loads it instead.
    nsCString new_dyld_lib_path(path.get());
    if (PR_GetEnv("MOZ_RUN_GTEST")) {
      new_dyld_lib_path = path + "/gtest:"_ns + new_dyld_lib_path;
      mLaunchOptions->env_map["DYLD_LIBRARY_PATH"] = new_dyld_lib_path.get();
    }

    // DYLD_INSERT_LIBRARIES is currently unused by default but we allow
    // it to be set by the external environment.
    const char* interpose = PR_GetEnv("DYLD_INSERT_LIBRARIES");
    if (interpose && strlen(interpose) > 0) {
      mLaunchOptions->env_map["DYLD_INSERT_LIBRARIES"] = interpose;
    }

    // Prevent connection attempts to diagnosticd(8) to save cycles. Log
    // messages can trigger these connection attempts, but access to
    // diagnosticd is blocked in sandboxed child processes.
#    if defined(MOZ_SANDBOX) && defined(XP_MACOSX)
    if (mDisableOSActivityMode) {
      mLaunchOptions->env_map["OS_ACTIVITY_MODE"] = "disable";
    }
#    endif  // defined(MOZ_SANDBOX)
#  endif
  }

  FilePath exePath;
  BinPathType pathType =
      GetPathToBinary(exePath, mProcessType, mUtilitySandbox);

  // Make sure the executable path is present at the start of our argument list.
  // If we're using BinPathType::Self, also add the `-contentproc` argument.
  if (pathType == BinPathType::Self) {
    std::string args[]{exePath.value(), "-contentproc"};
    mChildArgs.mArgs.insert(mChildArgs.mArgs.begin(), std::begin(args),
                            std::end(args));
  } else {
    mChildArgs.mArgs.insert(mChildArgs.mArgs.begin(), exePath.value());
  }

  if ((mProcessType == GeckoProcessType_Content ||
       mProcessType == GeckoProcessType_ForkServer) &&
      Omnijar::IsInitialized()) {
    // Make sure that child processes can find the omnijar, if they
    // use full XPCOM.  See Omnijar::ChildProcessInit and its callers.
    nsAutoCString path;
    nsCOMPtr<nsIFile> greFile = Omnijar::GetPath(Omnijar::GRE);
    if (greFile && NS_SUCCEEDED(greFile->GetNativePath(path))) {
      geckoargs::sGREOmni.Put(path.get(), mChildArgs);
    }
    nsCOMPtr<nsIFile> appFile = Omnijar::GetPath(Omnijar::APP);
    if (appFile && NS_SUCCEEDED(appFile->GetNativePath(path))) {
      geckoargs::sAppOmni.Put(path.get(), mChildArgs);
    }
  }

  if (mProcessType != GeckoProcessType_GMPlugin) {
    // Add the application directory path (-appdir path)
    AddAppDirToCommandLine(mChildArgs, mAppDir, mProfileDir);
  }

  // XXX Command line params past this point are expected to be at
  // the end of the command line string, and in a specific order.
  // See XRE_InitChildProcess in nsEmbedFunction.

  {
    auto* thisMac = static_cast<MacProcessLauncher*>(this);
    kern_return_t kr =
        bootstrap_check_in(bootstrap_port, thisMac->mMachConnectionName.c_str(),
                           getter_Transfers(thisMac->mParentRecvPort));
    if (kr != KERN_SUCCESS) {
      CHROMIUM_LOG(ERROR) << "parent bootstrap_check_in failed: "
                          << mach_error_string(kr);
      return Err(LaunchError("bootstrap_check_in", kr));
    }
    mChildArgs.mArgs.push_back(thisMac->mMachConnectionName.c_str());
  }

  mChildArgs.mArgs.push_back(mChildIDString);

  mChildArgs.mArgs.push_back(ChildProcessType());

#  ifdef MOZ_ENABLE_FORKSERVER
  // Any unexpected fds given to the fork server could be leaked into
  // child processes if they aren't closed properly; this assertion is
  // a reminder to deal with that.
  MOZ_ASSERT(mProcessType != GeckoProcessType_ForkServer ||
                 mChildArgs.mFiles.size() == 2,
             "wrong number of FDs for the fork server");
#  endif

  // Add any files which need to be transferred to fds_to_remap.
  // NOTE: This doesn't transfer ownership of the files out of `mChildArgs`.
  geckoargs::AddToFdsToRemap(mChildArgs, mLaunchOptions->fds_to_remap);

  return Ok();
}


RefPtr<ProcessLaunchPromise> PosixProcessLauncher::DoLaunch() {
  Result<Ok, LaunchError> result = Err(LaunchError{"Launch not attempted"});
#  ifdef MOZ_ENABLE_FORKSERVER
  if (mProcessType != GeckoProcessType_ForkServer && ForkServiceChild::Get()) {
    result = ForkServiceChild::Get()->SendForkNewSubprocess(
        std::move(mChildArgs), std::move(*mLaunchOptions), &mResults.mHandle);
  } else
#  endif
  {
    result = base::LaunchApp(mChildArgs.mArgs, std::move(*mLaunchOptions),
                             &mResults.mHandle);
  }

  if (result.isErr()) {
    return ProcessLaunchPromise::CreateAndReject(result.unwrapErr(), __func__);
  }
  return ProcessLaunchPromise::CreateAndResolve(std::move(mResults), __func__);
}


RefPtr<ProcessLaunchPromise> MacProcessLauncher::DoLaunch() {
  return PosixProcessLauncher::DoLaunch()->Then(
      XRE_GetAsyncIOEventTarget(), __func__,
      [self = RefPtr{this}](
          LaunchResults&& aResults) -> RefPtr<ProcessLaunchPromise> {
        // Wait for the child process to send us its 'task_t' data, then
        // send it the mach send/receive rights which are being passed on
        // the commandline.
        return MachHandleProcessCheckIn(
                   std::move(self->mParentRecvPort),
                   base::GetProcId(aResults.mHandle),
                   mozilla::TimeDuration::FromSeconds(10),
                   std::move(self->mChildArgs.mSendRights),
                   std::move(self->mChildArgs.mReceiveRights))
            ->Then(
                XRE_GetAsyncIOEventTarget(), __func__,
                [self, results = std::move(aResults)](task_t aTask) mutable {
                  results.mChildTask = aTask;
                  return ProcessLaunchPromise::CreateAndResolve(
                      std::move(results), __func__);
                },
                [](LaunchError aError) {
                  return ProcessLaunchPromise::CreateAndReject(aError,
                                                               __func__);
                });
      },
      [](LaunchError aError) {
        return ProcessLaunchPromise::CreateAndReject(aError, __func__);
      });
}


bool GeckoChildProcessHost::OpenPrivilegedHandle(base::ProcessId aPid) {
  if (mChildProcessHandle) {
    MOZ_ASSERT(aPid == base::GetProcId(mChildProcessHandle));
    return true;
  }

  return base::OpenPrivilegedProcessHandle(aPid, &mChildProcessHandle);
}

void GeckoChildProcessHost::OnChannelConnected(base::ProcessId peer_pid) {
  {
    mozilla::AutoWriteLock hLock(mHandleLock);
    if (!OpenPrivilegedHandle(peer_pid)) {
      MOZ_CRASH("can't open handle to child process");
    }
  }
  MonitorAutoLock lock(mMonitor);
  mProcessState = PROCESS_CONNECTED;
  lock.Notify();
}

RefPtr<ProcessHandlePromise> GeckoChildProcessHost::WhenProcessHandleReady() {
  MOZ_ASSERT(mHandlePromise != nullptr);
  return mHandlePromise;
}


#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool GeckoChildProcessHost::AppendMacSandboxParams(StringVector& aArgs) {
  MacSandboxInfo info;
  if (NS_WARN_IF(!FillMacSandboxInfo(info))) {
    return false;
  }
  info.AppendAsParams(aArgs);
  return true;
}

// Fill |aInfo| with the flags needed to launch the utility sandbox
bool GeckoChildProcessHost::FillMacSandboxInfo(MacSandboxInfo& aInfo) {
  aInfo.type = GetDefaultMacSandboxType();
  aInfo.shouldLog = Preferences::GetBool("security.sandbox.logging.enabled") ||
                    PR_GetEnv("MOZ_SANDBOX_LOGGING");

  nsAutoCString appPath;
  if (!nsMacUtilsImpl::GetAppPath(appPath)) {
    MOZ_CRASH("Failed to get app path");
  }
  aInfo.appPath.assign(appPath.get());
  return true;
}

void GeckoChildProcessHost::DisableOSActivityMode() {
  mDisableOSActivityMode = true;
}

//
// If early sandbox startup is enabled for this process type, map the
// process type to the sandbox type and enable the sandbox. Returns true
// if no errors were encountered or if early sandbox startup is not
// enabled for this process. Returns false if an error was encountered.
//
/* static */
bool GeckoChildProcessHost::StartMacSandbox(int aArgc, char** aArgv,
                                            std::string& aErrorMessage) {
  MacSandboxType sandboxType = MacSandboxType_Invalid;
  switch (XRE_GetProcessType()) {
    // For now, only support early sandbox startup for content,
    // RDD, and GMP processes. Add case statements for the additional
    // process types once early sandbox startup is implemented for them.
    case GeckoProcessType_Content:
      // Content processes don't use GeckoChildProcessHost
      // to configure sandboxing so hard code the sandbox type.
      sandboxType = MacSandboxType_Content;
      break;
    case GeckoProcessType_RDD:
      sandboxType = RDDProcessHost::GetMacSandboxType();
      break;
    case GeckoProcessType_Socket:
      sandboxType = net::SocketProcessHost::GetMacSandboxType();
      break;
    case GeckoProcessType_GMPlugin:
      sandboxType = gmp::GMPProcessParent::GetMacSandboxType();
      break;
    case GeckoProcessType_Utility:
      sandboxType = ipc::UtilityProcessHost::GetMacSandboxType();
      break;
    case GeckoProcessType_GPU:
      sandboxType = gfx::GPUProcessHost::GetMacSandboxType();
      break;
    default:
      return true;
  }

  return mozilla::StartMacSandboxIfEnabled(sandboxType, aArgc, aArgv,
                                           aErrorMessage);
}

#endif /* XP_MACOSX && MOZ_SANDBOX */

/* static */
nsTArray<RefPtr<GeckoChildProcessHost>> GeckoChildProcessHost::GetAll() {
  nsTArray<RefPtr<GeckoChildProcessHost>> allHosts;
  {
    StaticMutexAutoLock lock(sGeckoChildProcessHostsMutex);
    if (sGeckoChildProcessHosts) {
      allHosts.SetCapacity(sGeckoChildProcessHosts->Length());
      for (auto& weak : *sGeckoChildProcessHosts) {
        RefPtr<GeckoChildProcessHost> host(weak);
        if (host) {
          allHosts.AppendElement(host);
        }
      }
    }
  }
  return allHosts;
}

RefPtr<ProcessLaunchPromise> BaseProcessLauncher::Launch(
    GeckoChildProcessHost* aHost) {
  AssertIOThread();

  // The ForkServer doesn't use IPC::Channel for communication, so we can skip
  // initializing it.
  if (mProcessType != GeckoProcessType_ForkServer) {
    IPC::Channel::ChannelHandle clientHandle;
    if (!aHost->InitializeChannel(&clientHandle)) {
      return ProcessLaunchPromise::CreateAndReject(
          LaunchError("InitializeChannel"), __func__);
    }

    if (auto* handle = std::get_if<UniqueFileHandle>(&clientHandle)) {
      geckoargs::sIPCHandle.Put(std::move(*handle), mChildArgs);
    }
    // NOTE: We don't support passing UniqueMachReceiveRight instances on the
    // command line, so clientHandle must be a send right.
    else if (auto* port = std::get_if<UniqueMachSendRight>(&clientHandle)) {
      geckoargs::sIPCPort.Put(std::move(*port), mChildArgs);
    }
    else {
      MOZ_ASSERT_UNREACHABLE();
      return ProcessLaunchPromise::CreateAndReject(LaunchError("BadPipeType"),
                                                   __func__);
    }
  }

  return InvokeAsync(mLaunchThread, this, __func__,
                     &BaseProcessLauncher::PerformAsyncLaunch);
}

}  // namespace ipc
}  // namespace mozilla
