/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Implement TimeStamp::Now() with POSIX clocks.
//
// The "tick" unit for POSIX clocks is simply a nanosecond, as this is
// the smallest unit of time representable by struct timespec.  That
// doesn't mean that a nanosecond is the resolution of TimeDurations
// obtained with this API; see TimeDuration::Resolution;
//

#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
#  include <sys/param.h>
#  include <sys/sysctl.h>
#endif


#  define KINFO_PROC struct kinfo_proc

#  define KP_START_SEC p_ustart_sec
#  define KP_START_USEC p_ustart_usec

#include "mozilla/RoundedMulDiv.h"
#include "mozilla/Sprintf.h"
#include "mozilla/TimeStamp.h"

#if !defined(__wasi__)
#  include <pthread.h>
#endif

#ifdef CLOCK_MONOTONIC_COARSE
static bool sSupportsMonotonicCoarseClock = false;
#endif

#if !defined(__wasi__)
static const uint16_t kNsPerUs = 1000;
#endif

static const uint64_t kNsPerSec = 1000000000;
static const double kNsPerMsd = 1000000.0;
static const double kNsPerSecd = 1000000000.0;

static uint64_t TimespecToNs(const struct timespec& aTs) {
  uint64_t baseNs = uint64_t(aTs.tv_sec) * kNsPerSec;
  return baseNs + uint64_t(aTs.tv_nsec);
}

static uint64_t ClockTimeNs(const clockid_t aClockId = CLOCK_MONOTONIC) {
  struct timespec ts;
#ifdef CLOCK_MONOTONIC_COARSE
  MOZ_RELEASE_ASSERT(
      aClockId == CLOCK_MONOTONIC ||
      (sSupportsMonotonicCoarseClock && aClockId == CLOCK_MONOTONIC_COARSE));
#else
  MOZ_RELEASE_ASSERT(aClockId == CLOCK_MONOTONIC);
#endif
  // this can't fail: we know &ts is valid, and TimeStamp::Startup()
  // checks that CLOCK_MONOTONIC / CLOCK_MONOTONIC_COARSE are
  // supported (and aborts if the former is not).
  clock_gettime(aClockId, &ts);

  // tv_sec is defined to be relative to an arbitrary point in time,
  // but it would be madness for that point in time to be earlier than
  // the Epoch.  So we can safely assume that even if time_t is 32
  // bits, tv_sec won't overflow while the browser is open.  Revisit
  // this argument if we're still building with 32-bit time_t around
  // the year 2037.
  return TimespecToNs(ts);
}

namespace mozilla {

double BaseTimeDurationPlatformUtils::ToSeconds(int64_t aTicks) {
  return double(aTicks) / kNsPerSecd;
}

int64_t BaseTimeDurationPlatformUtils::TicksFromMilliseconds(
    double aMilliseconds) {
  double result = aMilliseconds * kNsPerMsd;
  // NOTE: this MUST be a >= test, because int64_t(double(INT64_MAX))
  // overflows and gives INT64_MIN.
  if (result >= double(INT64_MAX)) {
    return INT64_MAX;
  }
  if (result <= INT64_MIN) {
    return INT64_MIN;
  }

  return result;
}

int64_t BaseTimeDurationPlatformUtils::ToTicksAtRate(int64_t aTicks,
                                                     uint32_t aRate) {
  // The tick unit is a nanosecond, so this is an exact integer/rational
  // conversion rounded to the nearest aRate tick.
  return RoundedMulDiv(aTicks, aRate, kNsPerSec);
}

static bool gInitialized = false;

void TimeStamp::Startup() {
  if (gInitialized) {
    return;
  }

  struct timespec dummy;
  if (clock_gettime(CLOCK_MONOTONIC, &dummy) != 0) {
    MOZ_CRASH("CLOCK_MONOTONIC is absent!");
  }

#ifdef CLOCK_MONOTONIC_COARSE
  if (clock_gettime(CLOCK_MONOTONIC_COARSE, &dummy) == 0) {
    sSupportsMonotonicCoarseClock = true;
  }
#endif

  gInitialized = true;
}

void TimeStamp::Shutdown() {}

TimeStamp TimeStamp::Now(bool aHighResolution) {
#ifdef CLOCK_MONOTONIC_COARSE
  if (!aHighResolution && sSupportsMonotonicCoarseClock) {
    return TimeStamp(ClockTimeNs(CLOCK_MONOTONIC_COARSE));
  }
#endif
  return TimeStamp(ClockTimeNs(CLOCK_MONOTONIC));
}

#if   defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)

// Computes and returns the process uptime in us on various BSD flavors.
// Returns 0 if an error was encountered.

uint64_t TimeStamp::ComputeProcessUptime() {
  struct timespec ts;
  int rv = clock_gettime(CLOCK_REALTIME, &ts);

  if (rv == -1) {
    return 0;
  }

  int mib[] = {
      // clang-format off
      CTL_KERN,
      KERN_PROC,
      KERN_PROC_PID,
      getpid(),
      // clang-format on
  };
  u_int mibLen = sizeof(mib) / sizeof(mib[0]);

  KINFO_PROC proc;
  size_t bufferSize = sizeof(proc);
  rv = sysctl(mib, mibLen, &proc, &bufferSize, nullptr, 0);

  if (rv == -1) {
    return 0;
  }

  uint64_t startTime = ((uint64_t)proc.KP_START_SEC * kNsPerSec) +
                       (proc.KP_START_USEC * kNsPerUs);
  uint64_t now = ((uint64_t)ts.tv_sec * kNsPerSec) + ts.tv_nsec;

  if (startTime > now) {
    return 0;
  }

  return (now - startTime) / kNsPerUs;
}

#else

uint64_t TimeStamp::ComputeProcessUptime() { return 0; }

#endif

}  // namespace mozilla
