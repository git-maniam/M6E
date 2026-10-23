/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* XPConnect JavaScript interactive shell. */

#include "mozilla/Bootstrap.h"

#include <stdio.h>

#include "XREShellData.h"

#  include "xpcshellMacUtils.h"


#include "mozilla/BaseProfiler.h"

#ifdef LIBFUZZER
#  include "FuzzerDefs.h"
#endif

int main(int argc, char** argv, char** envp) {

  InitAutoreleasePool();

  // unbuffer stdout so that output is in the correct order; note that stderr
  // is unbuffered by default
  setbuf(stdout, nullptr);

#ifdef HAS_DLL_BLOCKLIST
  DllBlocklist_Initialize();
#endif

  char aLocal;
  mozilla::baseprofiler::profiler_init(&aLocal);

  XREShellData shellData;

  auto bootstrapResult = mozilla::GetBootstrap();
  if (bootstrapResult.isErr()) {
    return 2;
  }

  mozilla::Bootstrap::UniquePtr bootstrap = bootstrapResult.unwrap();

#ifdef LIBFUZZER
  shellData.fuzzerDriver = fuzzer::FuzzerDriver;
#endif
#ifdef AFLFUZZ
  shellData.fuzzerDriver = afl_interface_raw;
#endif

  int result = bootstrap->XRE_XPCShellMain(argc, argv, envp, &shellData);

  mozilla::baseprofiler::profiler_shutdown();

#if defined(DEBUG) && defined(HAS_DLL_BLOCKLIST)
  DllBlocklist_Shutdown();
#endif

  FinishAutoreleasePool();

  return result;
}
