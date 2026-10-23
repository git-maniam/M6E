/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XREShellData_h
#define XREShellData_h

#if defined(LIBFUZZER)
#  include "FuzzerRegistry.h"  // LibFuzzerDriver
#endif
#if defined(AFLFUZZ)
#  include "FuzzingInterface.h"  // FuzzingTestFuncRaw
#endif


/**
 * Data needed by XRE_XPCShellMain.
 */
struct XREShellData {
#if defined(LIBFUZZER)
  LibFuzzerDriver fuzzerDriver;
#endif
#if defined(AFLFUZZ)
  int (*fuzzerDriver)(FuzzingTestFuncRaw);
#endif
};

#endif  // XREShellData_h
