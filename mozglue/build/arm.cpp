/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* compile-time and runtime tests for whether to use various ARM extensions */

#include "arm.h"
#include "mozilla/Attributes.h"

#if defined(MOZILLA_ARM_HAVE_CPUID_DETECTION)

// arm.h has parallel #ifs which declare MOZILLA_ARM_HAVE_CPUID_DETECTION.
// We don't check it here so that we get compile errors if it's defined, but
// we don't compile one of these detection methods. The detection code here is
// based on the CPU detection in libtheora.


namespace mozilla {
namespace arm_private {
#  if !defined(MOZILLA_PRESUME_EDSP)
MOZ_RUNINIT bool edsp_enabled = check_edsp();
#  endif
#  if !defined(MOZILLA_PRESUME_ARMV6)
MOZ_RUNINIT bool armv6_enabled = check_armv6();
#  endif
#  if !defined(MOZILLA_PRESUME_ARMV7)
MOZ_RUNINIT bool armv7_enabled = check_armv7();
#  endif
#  if !defined(MOZILLA_PRESUME_NEON)
MOZ_RUNINIT bool neon_enabled = check_neon();
#  endif
#  if !defined(MOZILLA_PRESUME_ARM_AES)
MOZ_RUNINIT bool aes_enabled = check_aes();
#  endif
}  // namespace arm_private
}  // namespace mozilla

#endif  // MOZILLA_ARM_HAVE_CPUID_DETECTION
