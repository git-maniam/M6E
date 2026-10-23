/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AwakeTimeStamp.h"


#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"

namespace mozilla {

static constexpr uint64_t kUSperS = 1000000;
static constexpr uint64_t kUSperMS = 1000;
static constexpr uint64_t kNSperUS = 1000;

double AwakeTimeDuration::ToSeconds() const {
  return static_cast<double>(mValueUs) / kUSperS;
}
double AwakeTimeDuration::ToMilliseconds() const {
  return static_cast<double>(mValueUs) / kUSperMS;
}
double AwakeTimeDuration::ToMicroseconds() const {
  return static_cast<double>(mValueUs);
}

AwakeTimeDuration AwakeTimeDuration::FromSeconds(uint64_t aSeconds) {
  return AwakeTimeDuration(aSeconds * 1000000);
}
AwakeTimeDuration AwakeTimeDuration::FromMilliseconds(uint64_t aMilliseconds) {
  return AwakeTimeDuration(aMilliseconds * 1000);
}
AwakeTimeDuration AwakeTimeDuration::FromMicroseconds(uint64_t aMicroseconds) {
  return AwakeTimeDuration(aMicroseconds);
}

AwakeTimeDuration AwakeTimeStamp::operator-(
    AwakeTimeStamp const& aOther) const {
  return AwakeTimeDuration(mValueUs - aOther.mValueUs);
}

AwakeTimeStamp AwakeTimeStamp::operator-(
    AwakeTimeDuration const& aOther) const {
  return AwakeTimeStamp(mValueUs - aOther.mValueUs);
}

AwakeTimeStamp AwakeTimeStamp::operator+(
    const AwakeTimeDuration& aDuration) const {
  return AwakeTimeStamp(mValueUs + aDuration.mValueUs);
}

void AwakeTimeStamp::operator+=(const AwakeTimeDuration& aOther) {
  mValueUs += aOther.mValueUs;
}

void AwakeTimeStamp::operator-=(const AwakeTimeDuration& aOther) {
  MOZ_ASSERT(mValueUs >= aOther.mValueUs);
  mValueUs -= aOther.mValueUs;
}

// Apple things
#  include <time.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <mach/mach_time.h>

AwakeTimeStamp AwakeTimeStamp::Now() {
  return AwakeTimeStamp(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / kNSperUS);
}

AwakeTimeStamp AwakeTimeStamp::NowLoRes() { return Now(); }


};  // namespace mozilla
