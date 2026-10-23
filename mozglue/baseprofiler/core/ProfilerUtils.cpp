/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file implements functions from BaseProfilerUtils.h on all platforms.
// Functions with platform-specific implementations are separated in #if blocks
// below, with each block being self-contained with all the #includes and
// definitions it needs, to keep platform code easier to maintain in isolation.

#include "mozilla/BaseProfilerUtils.h"

// --------------------------------------------- WASI process & thread ids
#if defined(__wasi__)

namespace mozilla::baseprofiler {

// WASI is single-process and single-thread for now.

BaseProfilerProcessId profiler_current_process_id() {
  return BaseProfilerProcessId::FromNativeId(1u);
}

BaseProfilerThreadId profiler_current_thread_id() {
  return BaseProfilerThreadId::FromNativeId(1u);
}

}  // namespace mozilla::baseprofiler

// --------------------------------------------- Windows process & thread ids
#else
// All non-Windows platforms are assumed to be POSIX, which has getpid().

#  include <unistd.h>

namespace mozilla::baseprofiler {

BaseProfilerProcessId profiler_current_process_id() {
  return BaseProfilerProcessId::FromNativeId(getpid());
}

}  // namespace mozilla::baseprofiler

// --------------------------------------------- Non-Windows thread id
// ------------------------------------------------------- macOS

#    include <pthread.h>

namespace mozilla::baseprofiler {

BaseProfilerThreadId profiler_current_thread_id() {
  uint64_t tid;
  if (pthread_threadid_np(nullptr, &tid) != 0) {
    return BaseProfilerThreadId{};
  }
  return BaseProfilerThreadId::FromNativeId(tid);
}

}  // namespace mozilla::baseprofiler

// ------------------------------------------------------- Android
// Test Android before Linux, because Linux includes Android.
#endif  // End of non-XP_WIN.

// --------------------------------------------- Platform-agnostic definitions

namespace mozilla::baseprofiler {

static BaseProfilerThreadId scBaseProfilerMainThreadId{};

void profiler_init_main_thread_id() {
  if (!scBaseProfilerMainThreadId.IsSpecified()) {
    scBaseProfilerMainThreadId = profiler_current_thread_id();
  }
}

BaseProfilerThreadId profiler_main_thread_id() {
  return scBaseProfilerMainThreadId;
}

bool profiler_is_main_thread() {
  return profiler_current_thread_id() == scBaseProfilerMainThreadId;
}

}  // namespace mozilla::baseprofiler
