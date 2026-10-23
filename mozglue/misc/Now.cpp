/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Now.h"


#include <stdint.h>

#include "mozilla/TimeStamp.h"
#include "mozilla/Maybe.h"

namespace mozilla {

// Apple things
#  include <time.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <mach/mach_time.h>

static constexpr uint64_t kNSperMS = 1000000;

Maybe<uint64_t> NowExcludingSuspendMs() {
  return Some(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / kNSperMS);
}

Maybe<uint64_t> NowIncludingSuspendMs() {
  return Some(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / kNSperMS);
}


}  // namespace mozilla
