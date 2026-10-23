/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"

#include "nsXULAppAPI.h"

#include <stdlib.h>

#include "prenv.h"

#include "nsIAppShell.h"
#include "nsIToolkitProfile.h"


#include "nsAppRunner.h"
#include "nsExceptionHandler.h"
#include "mozilla/RuntimeExceptionModule.h"
#include "nsThreadUtils.h"
#include "nsJSUtils.h"
#include "nsWidgetsCID.h"
#include "nsXREDirProvider.h"
#ifdef MOZ_ASAN_REPORTER
#  include "CmdLineAndEnvUtils.h"
#  include "nsIFile.h"
#endif

#include "mozilla/Omnijar.h"
#  include <mach/mach.h>
#  include <servers/bootstrap.h>
#  include "nsVersionComparator.h"
#  include "chrome/common/mach_ipc_mac.h"
#  include "gfxPlatformMac.h"
#include "nsX11ErrorHandler.h"
#include "nsGDKErrorHandler.h"
#include "base/at_exit.h"
#include "base/message_loop.h"
#include "base/process_util.h"

#include "mozilla/AbstractThread.h"
#include "mozilla/FilePreferences.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/NeverDestroyed.h"
#include "mozilla/ProcessType.h"
#include "mozilla/RDDProcessImpl.h"
#include "mozilla/ipc/UtilityProcessImpl.h"
#include "mozilla/UniquePtr.h"

#include "mozilla/ipc/IOThread.h"
#include "mozilla/ipc/ProcessChild.h"

#include "mozilla/dom/ContentProcess.h"
#include "mozilla/dom/ContentParent.h"

#include "mozilla/ipc/TestShellParent.h"

#include "GMPProcessChild.h"
#include "mozilla/gfx/GPUProcessImpl.h"
#include "mozilla/net/SocketProcessImpl.h"

#include "ProfilerControl.h"


#if defined(MOZ_SANDBOX)
#  include "XREChildData.h"
#  include "mozilla/SandboxSettings.h"
#endif




#if defined(MOZ_ENABLE_FORKSERVER)
#  include "mozilla/ipc/ForkServer.h"
#endif


#include "VRProcessChild.h"
#include "nsTraceRefcnt.h"

using namespace mozilla;

using mozilla::ipc::GeckoChildProcessHost;
using mozilla::ipc::IOThread;
using mozilla::ipc::ProcessChild;

using mozilla::dom::ContentParent;
using mozilla::dom::ContentProcess;
using mozilla::dom::UniqueContentParentKeepAlive;

using mozilla::gmp::GMPProcessChild;

using mozilla::ipc::TestShellCommandParent;
using mozilla::ipc::TestShellParent;

namespace mozilla::_ipdltest {
// Set in IPDLUnitTest.cpp when running gtests.
UniquePtr<mozilla::ipc::ProcessChild> (*gMakeIPDLUnitTestProcessChild)(
    IPC::Channel::ChannelHandle, base::ProcessId, const nsID&) = nullptr;
}  // namespace mozilla::_ipdltest

static NS_DEFINE_CID(kAppShellCID, NS_APPSHELL_CID);

const char* XRE_GeckoProcessTypeToString(GeckoProcessType aProcessType) {
  switch (aProcessType) {
#define GECKO_PROCESS_TYPE(enum_value, enum_name, string_name, proc_typename, \
                           process_bin_type, procinfo_typename,               \
                           webidl_typename, allcaps_name)                     \
  case GeckoProcessType::GeckoProcessType_##enum_name:                        \
    return string_name;
#include "mozilla/GeckoProcessTypes.inc"
#undef GECKO_PROCESS_TYPE
    default:
      return "invalid";
  }
}

const char* XRE_ChildProcessTypeToAnnotation(GeckoProcessType aProcessType) {
  switch (aProcessType) {
    case GeckoProcessType_GMPlugin:
      return "plugin";
    case GeckoProcessType_Default:
      return "";
    case GeckoProcessType_Content:
      return "content";
    default:
      return XRE_GeckoProcessTypeToString(aProcessType);
  }
}



#if defined(MOZ_SANDBOX)
void AddContentSandboxLevelAnnotation() {
  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    uint32_t contentSandboxLevel = GetEffectiveContentSandboxLevel();
    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::ContentSandboxLevel, contentSandboxLevel);
  } else if (XRE_GetProcessType() == GeckoProcessType_GPU) {
    uint32_t gpuSandboxLevel = GetEffectiveGpuSandboxLevel();
    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::GpuSandboxLevel, gpuSandboxLevel);
  }
}
#endif /* MOZ_SANDBOX */

namespace {

int GetDebugChildPauseTime() {
  auto pauseStr = PR_GetEnv("MOZ_DEBUG_CHILD_PAUSE");
  if (pauseStr && *pauseStr) {
    int pause = atoi(pauseStr);
    if (pause != 1) {  // must be !=1 since =1 enables the default pause time
      return pause;
    }
  }
  return 30;  // seconds
}

}  // namespace

nsresult XRE_InitChildProcess(int aArgc, char* aArgv[],
                              const XREChildData* aChildData) {
  NS_ENSURE_ARG_MIN(aArgc, 2);
  NS_ENSURE_ARG_POINTER(aArgv);
  NS_ENSURE_ARG_POINTER(aArgv[0]);
  MOZ_ASSERT(aChildData);

  NS_SetCurrentThreadName("MainThread");

#ifdef MOZ_ASAN_REPORTER
  // In ASan reporter builds, we need to set ASan's log_path as early as
  // possible, so it dumps its errors into files there instead of using
  // the default stderr location. Since this is crucial for ASan reporter
  // to work at all (and we don't want people to use a non-functional
  // ASan reporter build), all failures while setting log_path are fatal.
  //
  // We receive this log_path via the ASAN_REPORTER_PATH environment variable
  // because there is no other way to generically get the necessary profile
  // directory in all child types without adding support for that in each
  // child process type class (at the risk of missing this in a child).
  //
  // In certain cases (e.g. child startup through xpcshell or gtests), this
  // code needs to remain disabled, as no ASAN_REPORTER_PATH would be available.
  if (!PR_GetEnv("MOZ_DISABLE_ASAN_REPORTER") && !PR_GetEnv("MOZ_RUN_GTEST")) {
    nsCOMPtr<nsIFile> asanReporterPath = GetFileFromEnv("ASAN_REPORTER_PATH");
    if (!asanReporterPath) {
      MOZ_CRASH("Child did not receive ASAN_REPORTER_PATH!");
    }
    setASanReporterPath(asanReporterPath);
  }
#endif



  // NB: This must be called before profiler_init
  ScopedLogging logger;

  mozilla::LogModule::Init(aArgc, aArgv);
  nsTraceRefcnt::EarlyInit();

  AUTO_BASE_PROFILER_LABEL("XRE_InitChildProcess (around Gecko Profiler)",
                           OTHER);
  AUTO_PROFILER_INIT;
  AUTO_PROFILER_LABEL("XRE_InitChildProcess", OTHER);


  auto _supplementalFontThread = gfxPlatformMac::RegisterSupplementalFonts();

  // Ensure AbstractThread is minimally setup, so async IPC messages
  // work properly.
  AbstractThread::InitTLS();

  // Complete 'task_t' exchange for Mac OS X. This structure has the same size
  // regardless of architecture so we don't have any cross-arch issues here.
  if (aArgc < 1) return NS_ERROR_FAILURE;

#  if defined(MOZ_SANDBOX)
  // Save the original number of arguments to pass to the sandbox
  // setup routine which also uses the crash server argument.
  int allArgc = aArgc;
#  endif /* MOZ_SANDBOX */

  // Acquire the mach bootstrap port name from our command line, and send our
  // task_t to the parent process.
  const char* const mach_port_name = aArgv[--aArgc];

  const int kTimeoutMs = 1000;

  std::vector<mozilla::UniqueMachSendRight> sendRights;
  std::vector<mozilla::UniqueMachReceiveRight> receiveRights;
  if (NS_WARN_IF(!MachChildProcessCheckIn(mach_port_name, kTimeoutMs,
                                          sendRights, receiveRights))) {
    return NS_ERROR_FAILURE;
  }

  geckoargs::SetPassedMachSendRights(std::move(sendRights));
  geckoargs::SetPassedMachReceiveRights(std::move(receiveRights));

#  if defined(MOZ_SANDBOX)
  std::string sandboxError;
  if (!GeckoChildProcessHost::StartMacSandbox(allArgc, aArgv, sandboxError)) {
    printf_stderr("Sandbox error: %s\n", sandboxError.c_str());
    MOZ_CRASH("Sandbox initialization failed");
  }
#  endif /* MOZ_SANDBOX */


  SetupErrorHandling(aArgv[0]);

  bool exceptionHandlerIsSet = false;
  if (!CrashReporter::IsDummy()) {
    if (geckoargs::sCrashReporter.IsPresent(aArgc, aArgv)) {
      exceptionHandlerIsSet =
          CrashReporter::SetRemoteExceptionHandler(aArgc, aArgv);

      if (!exceptionHandlerIsSet) {
        // Bug 684322 will add better visibility into this condition
        NS_WARNING("Could not setup crash reporting");
      }
    } else {
      // We might have registered a runtime exception module very early in
      // process startup to catch early crashes. This is before we process the
      // crash reporter arg, so unregister here if it turns out the crash
      // reporter is disabled.
      CrashReporter::UnregisterRuntimeExceptionModule();
    }
  }


  if (PR_GetEnv("MOZ_DEBUG_CHILD_PROCESS") ||
      PR_GetEnv("MOZ_DEBUG_CHILD_PAUSE")) {
    printf_stderr(
        "\n\nCHILDCHILDCHILDCHILD (process type %s)\n  debug me @ %d\n\n",
        XRE_GetProcessTypeString(), base::GetCurrentProcId());
    sleep(GetDebugChildPauseTime());
  }



  Maybe<base::ProcessId> parentPID = geckoargs::sParentPid.Get(aArgc, aArgv);
  Maybe<const char*> initialChannelIdString =
      geckoargs::sInitialChannelID.Get(aArgc, aArgv);
  if (NS_WARN_IF(!parentPID || !initialChannelIdString)) {
    return NS_ERROR_FAILURE;
  }

  Maybe<IPC::Channel::ChannelHandle> clientChannel =
      geckoargs::sIPCHandle.Get(aArgc, aArgv);
  MOZ_ASSERT_IF(clientChannel, !geckoargs::sIPCPort.IsPresent(aArgc, aArgv));
  if (!clientChannel) {
    clientChannel = geckoargs::sIPCPort.Get(aArgc, aArgv);
  }
  if (NS_WARN_IF(!clientChannel)) {
    return NS_ERROR_FAILURE;
  }

  nsID messageChannelId{};
  if (NS_WARN_IF(!messageChannelId.Parse(
          nsDependentCString(*initialChannelIdString)))) {
    return NS_ERROR_FAILURE;
  }

  base::AtExitManager exitManager;

  nsresult rv = XRE_InitCommandLine(aArgc, aArgv);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  MessageLoop::Type uiLoopType;
  switch (XRE_GetProcessType()) {
    case GeckoProcessType_Content:
    case GeckoProcessType_GPU:
    case GeckoProcessType_IPDLUnitTest:
    case GeckoProcessType_VR:
    case GeckoProcessType_RDD:
    case GeckoProcessType_Socket:
    case GeckoProcessType_Utility:
      // Content processes need the XPCOM/chromium frankenventloop
      uiLoopType = MessageLoop::TYPE_MOZILLA_CHILD;
      break;
    case GeckoProcessType_GMPlugin:
      gmp::GMPProcessChild::InitStatics(aArgc, aArgv);
      uiLoopType = gmp::GMPProcessChild::UseXPCOM()
                       ? MessageLoop::TYPE_MOZILLA_CHILD
                       : MessageLoop::TYPE_DEFAULT;
      break;
    default:
      uiLoopType = MessageLoop::TYPE_UI;
      break;
  }

  {
    // This is a lexical scope for the MessageLoop below.  We want it
    // to go out of scope before NS_LogTerm() so that we don't get
    // spurious warnings about XPCOM objects being destroyed from a
    // static context.

    AutoIOInterposer ioInterposerGuard;

    // Associate this thread with a UI MessageLoop
    MessageLoop uiMessageLoop(uiLoopType);
    {
      UniquePtr<ProcessChild> process;
      switch (XRE_GetProcessType()) {
        case GeckoProcessType_Default:
          MOZ_CRASH("This makes no sense");
          break;

        case GeckoProcessType_Content:
          ioInterposerGuard.Init();
          process = MakeUnique<ContentProcess>(std::move(*clientChannel),
                                               *parentPID, messageChannelId);
          break;

        case GeckoProcessType_IPDLUnitTest:
          MOZ_RELEASE_ASSERT(mozilla::_ipdltest::gMakeIPDLUnitTestProcessChild,
                             "xul-gtest not loaded!");
          process = mozilla::_ipdltest::gMakeIPDLUnitTestProcessChild(
              std::move(*clientChannel), *parentPID, messageChannelId);
          break;

        case GeckoProcessType_GMPlugin:
          process = MakeUnique<gmp::GMPProcessChild>(
              std::move(*clientChannel), *parentPID, messageChannelId);
          break;

        case GeckoProcessType_GPU:
          process = MakeUnique<gfx::GPUProcessImpl>(
              std::move(*clientChannel), *parentPID, messageChannelId);
          break;

        case GeckoProcessType_VR:
          process = MakeUnique<gfx::VRProcessChild>(
              std::move(*clientChannel), *parentPID, messageChannelId);
          break;

        case GeckoProcessType_RDD:
          process = MakeUnique<RDDProcessImpl>(std::move(*clientChannel),
                                               *parentPID, messageChannelId);
          break;

        case GeckoProcessType_Socket:
          ioInterposerGuard.Init();
          process = MakeUnique<net::SocketProcessImpl>(
              std::move(*clientChannel), *parentPID, messageChannelId);
          break;

        case GeckoProcessType_Utility:
          process = MakeUnique<ipc::UtilityProcessImpl>(
              std::move(*clientChannel), *parentPID, messageChannelId);
          break;

#if defined(MOZ_ENABLE_FORKSERVER)
        case GeckoProcessType_ForkServer:
          MOZ_CRASH("Fork server should not go here");
          break;
#endif
        default:
          MOZ_CRASH("Unknown main thread class");
      }

      if (!process->Init(aArgc, aArgv)) {
        return NS_ERROR_FAILURE;
      }


      mozilla::FilePreferences::InitDirectoriesAllowlist();
      mozilla::FilePreferences::InitPrefs();

#if defined(MOZ_SANDBOX)
      AddContentSandboxLevelAnnotation();
#endif

      // Run the UI event loop on the main thread.
      uiMessageLoop.MessageLoop::Run();

      // Allow ProcessChild to clean up after itself before going out of
      // scope and being deleted
      process->CleanUp();
      mozilla::Omnijar::CleanUp();
    }
  }

  CrashReporter::UnsetRemoteExceptionHandler(exceptionHandlerIsSet);

  return XRE_DeinitCommandLine();
}

nsISerialEventTarget* XRE_GetAsyncIOEventTarget() {
  return IOThread::Get()->GetEventTarget();
}

nsresult XRE_RunAppShell() {
  nsCOMPtr<nsIAppShell> appShell(do_GetService(kAppShellCID));
  NS_ENSURE_TRUE(appShell, NS_ERROR_FAILURE);
  if (XRE_UseNativeEventProcessing()) {
    // In content processes that want XPCOM (and hence want
    // AppShell), we usually run our hybrid event loop through
    // MessagePump::Run(), by way of nsBaseAppShell::Run().  The
    // Cocoa nsAppShell impl, however, implements its own Run()
    // that's unaware of MessagePump.  That's all rather suboptimal,
    // but oddly enough not a problem... usually.
    //
    // The problem with this setup comes during startup.
    // XPCOM-in-subprocesses depends on IPC, e.g. to init the pref
    // service, so we have to init IPC first.  But, IPC also
    // indirectly kinda-depends on XPCOM, because MessagePump
    // schedules work from off-main threads (e.g. IO thread) by
    // using NS_DispatchToMainThread().  If the IO thread receives a
    // Message from the parent before nsThreadManager is
    // initialized, then DispatchToMainThread() will fail, although
    // MessagePump will remember the task.  This race condition
    // isn't a problem when appShell->Run() ends up in
    // MessagePump::Run(), because MessagePump will immediate see it
    // has work to do.  It *is* a problem when we end up in [NSApp
    // run], because it's not aware that MessagePump has work that
    // needs to be processed; that was supposed to be signaled by
    // nsIRunnable(s).
    //
    // So instead of hacking Cocoa nsAppShell or rewriting the
    // event-loop system, we compromise here by processing any tasks
    // that might have been enqueued on MessagePump, *before*
    // MessagePump::ScheduleWork was able to successfully
    // DispatchToMainThread().
    MessageLoop* loop = MessageLoop::current();
    bool couldNest = loop->NestableTasksAllowed();

    loop->SetNestableTasksAllowed(true);
    RefPtr<Runnable> task = new MessageLoop::QuitTask();
    loop->PostTask(task.forget());
    loop->Run();

    loop->SetNestableTasksAllowed(couldNest);
  }
  return appShell->Run();
}

void XRE_ShutdownChildProcess() {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");

  mozilla::DebugOnly<nsISerialEventTarget*> ioTarget =
      XRE_GetAsyncIOEventTarget();
  MOZ_ASSERT(!!ioTarget, "Bad shutdown order");

  // Quit() sets off the following chain of events
  //  (1) UI loop starts quitting
  //  (2) UI loop returns from Run() in XRE_InitChildProcess()
  //  (3) ProcessChild goes out of scope and terminates the IO thread
  //  (4) ProcessChild joins the IO thread
  //  (5) exit()
  MessageLoop::current()->Quit();

  nsCOMPtr<nsIAppShell> appShell(do_GetService(kAppShellCID));
  if (appShell) {
    // On Mac, we might be only above nsAppShell::Run(), not
    // MessagePump::Run().  See XRE_RunAppShell(). To account for
    // that case, we fire off an Exit() here.  If we were indeed
    // above MessagePump::Run(), this Exit() is just superfluous.
    appShell->Exit();
  }
}

namespace {

UniqueContentParentKeepAlive& TestShellContentParent() {
  static NeverDestroyed<UniqueContentParentKeepAlive> sContentParent;
  return *sContentParent;
}

already_AddRefed<TestShellParent> GetOrCreateTestShellParent() {
  if (!TestShellContentParent()) {
    // Use a "web" child process by default.  File a bug if you don't like
    // this and you're sure you wouldn't be better off writing a "browser"
    // chrome mochitest where you can have multiple types of content
    // processes.
    TestShellContentParent() = ContentParent::GetNewOrUsedBrowserProcess(
        mozilla::dom::RemoteType::SharedWeb({}));
  } else if (TestShellContentParent()->IsShuttingDown()) {
    return nullptr;
  }

  RefPtr<TestShellParent> tsp =
      TestShellContentParent()->GetTestShellSingleton();
  if (!tsp) {
    tsp = TestShellContentParent()->CreateTestShell();
  }
  return tsp.forget();
}

}  // namespace

bool XRE_SendTestShellCommand(JSContext* aCx, JSString* aCommand,
                              JS::Value* aCallback) {
  JS::Rooted<JSString*> cmd(aCx, aCommand);
  RefPtr<TestShellParent> tsp = GetOrCreateTestShellParent();
  NS_ENSURE_TRUE(tsp, false);

  nsAutoJSString command;
  NS_ENSURE_TRUE(command.init(aCx, cmd), false);

  if (!aCallback) {
    return tsp->SendExecuteCommand(command);
  }

  TestShellCommandParent* callback = static_cast<TestShellCommandParent*>(
      tsp->SendPTestShellCommandConstructor(command));
  NS_ENSURE_TRUE(callback, false);

  NS_ENSURE_TRUE(callback->SetCallback(aCx, *aCallback), false);

  return true;
}

bool XRE_ShutdownTestShell() {
  if (!TestShellContentParent()) {
    return true;
  }
  bool ret = true;
  if (TestShellContentParent()->IsAlive()) {
    RefPtr<TestShellParent> tsp =
        TestShellContentParent()->GetTestShellSingleton();
    ret = TestShellContentParent()->DestroyTestShell(tsp);
  }
  TestShellContentParent().reset();
  return ret;
}


#ifdef MOZ_ENABLE_FORKSERVER
int XRE_ForkServer(int* aArgc, char*** aArgv) {
  return mozilla::ipc::ForkServer::RunForkServer(aArgc, aArgv) ? 1 : 0;
}
#endif
