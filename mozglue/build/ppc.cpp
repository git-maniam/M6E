/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* compile-time and runtime tests for whether to use Power ISA-specific
 * extensions */

#include "ppc.h"

#include "mozilla/Attributes.h"


const unsigned PPC_FLAG_VMX = 1;
const unsigned PPC_FLAG_VSX = 2;
const unsigned PPC_FLAG_VSX3 = 4;

static signed get_ppc_cpu_flags(void) {
  // This could be expensive, so cache the result.
  static signed cpu_flags = -1;

  if (cpu_flags > -1) {  // already checked
    return cpu_flags;
  }
  cpu_flags = 0;

  // Non-Linux detection here. Currently, on systems other than Linux,
  // no CPU SIMD features will be detected.

  return cpu_flags;
}

namespace mozilla {
namespace ppc_private {
MOZ_RUNINIT bool vmx_enabled = !!(get_ppc_cpu_flags() & PPC_FLAG_VMX);
MOZ_RUNINIT bool vsx_enabled = !!(get_ppc_cpu_flags() & PPC_FLAG_VSX);
MOZ_RUNINIT bool vsx3_enabled = !!(get_ppc_cpu_flags() & PPC_FLAG_VSX3);
}  // namespace ppc_private
}  // namespace mozilla
