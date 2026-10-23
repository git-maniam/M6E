/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file implements functions from ProfilerUtils.h on all platforms.
// Functions with platform-specific implementations are separated in #if blocks
// below, with each block being self-contained with all the #includes and
// definitions it needs, to keep platform code easier to maintain in isolation.

#include "mozilla/ProfilerUtils.h"

// --------------------------------------------- Windows process & thread ids
// All non-Windows platforms are assumed to be POSIX, which has getpid().

#  include <unistd.h>

ProfilerProcessId profiler_current_process_id() {
  return ProfilerProcessId::FromNativeId(getpid());
}

// --------------------------------------------- Non-Windows thread id
// ------------------------------------------------------- macOS

#    include <pthread.h>

ProfilerThreadId profiler_current_thread_id() {
  uint64_t tid;
  if (pthread_threadid_np(nullptr, &tid) != 0) {
    return ProfilerThreadId{};
  }
  return ProfilerThreadId::FromNativeId(tid);
}

// ------------------------------------------------------- Android
// Test Android before Linux, because Linux includes Android.

// --------------------------------------------- Platform-agnostic definitions

#include "MainThreadUtils.h"
#include "mozilla/Assertions.h"

static ProfilerThreadId scProfilerMainThreadId;

void profiler_init_main_thread_id() {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::baseprofiler::profiler_init_main_thread_id();
  if (!scProfilerMainThreadId.IsSpecified()) {
    scProfilerMainThreadId = profiler_current_thread_id();
  }
}

[[nodiscard]] ProfilerThreadId profiler_main_thread_id() {
  return scProfilerMainThreadId;
}

[[nodiscard]] bool profiler_is_main_thread() {
  return profiler_current_thread_id() == scProfilerMainThreadId;
}
