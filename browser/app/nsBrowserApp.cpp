/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULAppAPI.h"
#include "mozilla/XREAppData.h"
#include "XREChildData.h"
#include "XREShellData.h"
#include "ApplicationData.h"
#include "mozilla/Bootstrap.h"
#include "mozilla/ProcessType.h"
#include "mozilla/RuntimeExceptionModule.h"
#include "mozilla/ScopeExit.h"
#include "BrowserDefines.h"
#  include <sys/resource.h>
#  include <unistd.h>
#  include <fcntl.h>

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "nsCOMPtr.h"

#include "BinaryPath.h"

#include "nsXPCOMPrivate.h"  // for MAXPATHLEN and XPCOM_DLL

#include "mozilla/BaseProfiler.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StartupTimeline.h"

#ifdef LIBFUZZER
#  include "FuzzerDefs.h"
#endif

#ifdef MOZ_LINUX_32_SSE2_STARTUP_ERROR
#  include <cpuid.h>

static bool IsSSE2Available() {
  // The rest of the app has been compiled to assume that SSE2 is present
  // unconditionally, so we can't use the normal copy of SSE.cpp here.
  // Since SSE.cpp caches the results and we need them only transiently,
  // instead of #including SSE.cpp here, let's just inline the specific check
  // that's needed.
  unsigned int level = 1u;
  unsigned int eax, ebx, ecx, edx;
  unsigned int bits = (1u << 26);
  unsigned int max = __get_cpuid_max(0, nullptr);
  if (level > max) {
    return false;
  }
  __cpuid_count(level, 0, eax, ebx, ecx, edx);
  return (edx & bits) == bits;
}

static const char sSSE2Message[] =
    "This browser version requires a processor with the SSE2 instruction "
    "set extension.\nYou may be able to obtain a version that does not "
    "require SSE2 from your Linux distribution.\n";

__attribute__((constructor)) static void SSE2Check() {
  if (IsSSE2Available()) {
    return;
  }
  // Using write() in order to avoid jemalloc-based buffering. Ignoring return
  // values, since there isn't much we could do on failure and there is no
  // point in trying to recover from errors.
  (void)write(STDERR_FILENO, sSSE2Message, std::size(sSSE2Message) - 1);
  // _exit() instead of exit() to avoid running the usual "at exit" code.
  _exit(255);
}
#endif


using namespace mozilla;

#define kDesktopFolder "browser"

// Only `Output` below cares about this, and only on Windows, where it decides
// whether to pop up a message box.

static MOZ_FORMAT_PRINTF(1, 2) void Output(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  vfprintf(stderr, fmt, ap);

  va_end(ap);
}

/**
 * Return true if |arg| is a flag with the given string.
 *
 * A flag starts with one or two `-` characters, or a `/` character on windows.
 */
static bool IsFlag(const char* arg, const char* s) {
  if (*arg == '-') {
    if (*++arg == '-') ++arg;
    return !strcasecmp(arg, s);
  }


  return false;
}


constinit Bootstrap::UniquePtr gBootstrap;

static int do_main(int argc, char* argv[], char* envp[]) {
  // Allow firefox.exe to launch XULRunner apps via -app <application.ini>
  // Note that -app must be the *first* argument.
  const char* appDataFile = getenv("XUL_APP_FILE");
  if ((!appDataFile || !*appDataFile) && (argc > 1 && IsFlag(argv[1], "app"))) {
    if (argc == 2) {
      Output("Incorrect number of arguments passed to -app");
      return 255;
    }
    appDataFile = argv[2];

    char appEnv[MAXPATHLEN];
    SprintfLiteral(appEnv, "XUL_APP_FILE=%s", argv[2]);
    if (putenv(strdup(appEnv))) {
      Output("Couldn't set %s.\n", appEnv);
      return 255;
    }
    argv[2] = argv[0];
    argv += 2;
    argc -= 2;
  } else if (argc > 1 && IsFlag(argv[1], "xpcshell")) {
    for (int i = 1; i < argc; i++) {
      argv[i] = argv[i + 1];
    }

    XREShellData shellData;

#ifdef LIBFUZZER
    shellData.fuzzerDriver = fuzzer::FuzzerDriver;
#endif
#ifdef AFLFUZZ
    shellData.fuzzerDriver = afl_interface_raw;
#endif

    return gBootstrap->XRE_XPCShellMain(--argc, argv, envp, &shellData);
  }

  BootstrapConfig config;

  if (appDataFile && *appDataFile) {
    config.appData = nullptr;
    config.appDataPath = appDataFile;
  } else {
    // no -app flag so we use the compiled-in app data
    config.appData = kStaticAppData;
    config.appDataPath = kDesktopFolder;
  }


#ifdef LIBFUZZER
  if (getenv("FUZZER"))
    gBootstrap->XRE_LibFuzzerSetDriver(fuzzer::FuzzerDriver);
#endif

  EnsureBrowserCommandlineSafe(argc, argv);

  return gBootstrap->XRE_main(argc, argv, config);
}

static nsresult InitXPCOMGlue(LibLoadingStrategy aLibLoadingStrategy) {
  if (gBootstrap) {
    return NS_OK;
  }

  UniqueFreePtr<char> exePath = BinaryPath::Get();
  if (!exePath) {
    Output("Couldn't find the application directory.\n");
    return NS_ERROR_FAILURE;
  }

  auto bootstrapResult =
      mozilla::GetBootstrap(exePath.get(), aLibLoadingStrategy);
  if (bootstrapResult.isErr()) {
    Output("Couldn't load XPCOM.\n");
    return NS_ERROR_FAILURE;
  }

  gBootstrap = bootstrapResult.unwrap();

  // This will set this thread as the main thread.
  gBootstrap->NS_LogInit();

  return NS_OK;
}

#ifdef HAS_DLL_BLOCKLIST
// NB: This must be extern, as this value is checked elsewhere
uint32_t gBlocklistInitFlags = eDllBlocklistInitFlagDefault;
#endif

static void ReserveDefaultFileDescriptors() {
  // Reserve the lower positions of the file descriptors to make sure
  // we don't reuse stdin/stdout/stderr in case they were closed
  // before launch.
  // Otherwise code explicitly writing to fd 1 or 2 might accidentally
  // write to something else, like in bug 1820896 where FD 1 is
  // reused for the X server display connection.
  int fd = open("/dev/null", O_RDONLY);
  for (int i = 0; i < 2; i++) {
    [[maybe_unused]] int r = dup(fd);
  }
}


int main(int argc, char* argv[], char* envp[]) {
  ReserveDefaultFileDescriptors();


#ifdef MOZ_BROWSER_CAN_BE_CONTENTPROC
  if (argc > 1 && IsFlag(argv[1], "contentproc")) {
    // Set the process type and gecko child id.
    SetGeckoProcessType(argv[--argc]);
    SetGeckoChildID(argv[--argc]);

#  if defined(MOZ_ENABLE_FORKSERVER)
    if (GetGeckoProcessType() == GeckoProcessType_ForkServer) {
      nsresult rv = InitXPCOMGlue(LibLoadingStrategy::NoReadAhead);
      if (NS_FAILED(rv)) {
        return 255;
      }

      // Run a fork server in this process, single thread. When it returns, it
      // means the fork server have been stopped or a new child process is
      // created.
      //
      // For the latter case, XRE_ForkServer() will return false, running in a
      // child process just forked from the fork server process. argc & argv
      // will be updated with the values passing from the chrome process, as
      // will GeckoProcessType and GeckoChildID. With the new values, this
      // function continues the reset of the code acting as a child process.
      if (gBootstrap->XRE_ForkServer(&argc, &argv)) {
        // Return from the fork server in the fork server process.
        // Stop the fork server.
        // InitXPCOMGlue calls NS_LogInit, so we need to balance it here.
        gBootstrap->NS_LogTerm();
        return 0;
      }
    }
#  endif
  }
#endif


  mozilla::TimeStamp start = mozilla::TimeStamp::Now();

  AUTO_BASE_PROFILER_INIT;
  AUTO_BASE_PROFILER_LABEL("nsBrowserApp main", OTHER);

  // Register an external module to report on otherwise uncatchable exceptions.
  // Note that in child processes this must be called after Gecko process type
  // has been set.
  CrashReporter::RegisterRuntimeExceptionModule();

  // Make sure we unregister the runtime exception module before returning.
  auto unregisterRuntimeExceptionModule =
      MakeScopeExit([] { CrashReporter::UnregisterRuntimeExceptionModule(); });

#ifdef MOZ_BROWSER_CAN_BE_CONTENTPROC
  // We are launching as a content process, delegate to the appropriate
  // main
  if (GetGeckoProcessType() != GeckoProcessType_Default) {

#  ifdef HAS_DLL_BLOCKLIST
    uint32_t initFlags =
        gBlocklistInitFlags | eDllBlocklistInitFlagIsChildProcess;
    SetDllBlocklistProcessTypeFlags(initFlags, GetGeckoProcessType());
    DllBlocklist_Initialize(initFlags);
#  endif  // HAS_DLL_BLOCKLIST


    nsresult rv = InitXPCOMGlue(LibLoadingStrategy::NoReadAhead);
    if (NS_FAILED(rv)) {
      return 255;
    }

    XREChildData childData;


    rv = gBootstrap->XRE_InitChildProcess(argc, argv, &childData);

#  if defined(DEBUG) && defined(HAS_DLL_BLOCKLIST)
    DllBlocklist_Shutdown();
#  endif

    // InitXPCOMGlue calls NS_LogInit, so we need to balance it here.
    gBootstrap->NS_LogTerm();

    return NS_FAILED(rv) ? 1 : 0;
  }
#endif

#ifdef HAS_DLL_BLOCKLIST
  DllBlocklist_Initialize(gBlocklistInitFlags);
#endif

// We will likely only ever support this as a command line argument on Windows
// and OSX, so we're ifdefing here just to not create any expectations.
  if (argc > 1 && IsFlag(argv[1], "silentmode")) {
    ::putenv(const_cast<char*>("MOZ_APP_SILENT_START=1"));
    ::putenv(const_cast<char*>("MOZ_APP_NO_DOCK=1"));
  }


  nsresult rv = InitXPCOMGlue(LibLoadingStrategy::ReadAhead);
  if (NS_FAILED(rv)) {
    return 255;
  }

  gBootstrap->XRE_StartupTimelineRecord(mozilla::StartupTimeline::START, start);

#ifdef MOZ_BROWSER_CAN_BE_CONTENTPROC
  gBootstrap->XRE_EnableSameExecutableForContentProc();
#endif

  int result = do_main(argc, argv, envp);


  gBootstrap->NS_LogTerm();

#if defined(DEBUG) && defined(HAS_DLL_BLOCKLIST)
  DllBlocklist_Shutdown();
#endif

  // Allow writes again. While we would like to catch writes from static
  // destructors to allow early exits to use _exit, we know that there is
  // at least one such write that we don't control (see bug 826029). For
  // now we enable writes again and early exits will have to use exit instead
  // of _exit.
  gBootstrap->XRE_StopLateWriteChecks();

  gBootstrap.reset();

  return result;
}
