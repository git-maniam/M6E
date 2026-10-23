/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Various JS utility functions. */

#include "js/Utility.h"

#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/ThreadLocal.h"

#include <stdio.h>

#include "jstypes.h"

#include "util/Poison.h"
#include "vm/HelperThreads.h"
#include "vm/JSContext.h"

using namespace js;

using mozilla::Maybe;

#if defined(DEBUG) || defined(JS_OOM_BREAKPOINT)
/* For OOM testing functionality in Utility.h. */
namespace js {

mozilla::Atomic<AutoEnterOOMUnsafeRegion*> AutoEnterOOMUnsafeRegion::owner_;

namespace oom {

JS_PUBLIC_DATA FailureSimulator simulator;
static MOZ_THREAD_LOCAL(uint32_t) threadType;

bool InitThreadType() { return threadType.init(); }

void SetThreadType(ThreadType type) { threadType.set(type); }

uint32_t GetThreadType(void) { return threadType.get(); }

static inline bool IsHelperThreadType(uint32_t thread) {
  return thread != THREAD_TYPE_NONE && thread != THREAD_TYPE_MAIN;
}

void FailureSimulator::simulateFailureAfter(Kind kind, uint64_t checks,
                                            uint32_t thread, bool always) {
  Maybe<AutoLockHelperThreadState> lock;
  if (IsHelperThreadType(targetThread_) || IsHelperThreadType(thread)) {
    lock.emplace();
    WaitForAllHelperThreads(lock.ref());
  }

  MOZ_ASSERT(counter_ + checks > counter_);
  MOZ_ASSERT(thread > js::THREAD_TYPE_NONE && thread < js::THREAD_TYPE_MAX);
  targetThread_ = thread;
  maxChecks_ = counter_ + checks;
  failAlways_ = always;
  kind_ = kind;
}

void FailureSimulator::reset() {
  Maybe<AutoLockHelperThreadState> lock;
  if (IsHelperThreadType(targetThread_)) {
    lock.emplace();
    WaitForAllHelperThreads(lock.ref());
  }

  targetThread_ = THREAD_TYPE_NONE;
  maxChecks_ = UINT64_MAX;
  failAlways_ = false;
  kind_ = Kind::Nothing;
}

}  // namespace oom
}  // namespace js
#endif  // defined(DEBUG) || defined(JS_OOM_BREAKPOINT)

#if defined(FUZZING)
namespace js {
namespace oom {
JS_PUBLIC_DATA size_t largeAllocLimit = 0;
void InitLargeAllocLimit() {
  char* limitStr = getenv("MOZ_FUZZ_LARGE_ALLOC_LIMIT");
  if (limitStr) {
    largeAllocLimit = atoll(limitStr);
  }
}
}  // namespace oom
}  // namespace js
#endif

JS_PUBLIC_DATA arena_id_t js::MallocArena;
JS_PUBLIC_DATA arena_id_t js::BackgroundMallocArena;
JS_PUBLIC_DATA arena_id_t js::ArrayBufferContentsArena;
JS_PUBLIC_DATA arena_id_t js::StringBufferArena;

void js::InitMallocAllocator() {
  arena_params_t mallocArenaParams;
  mallocArenaParams.mMaxDirtyIncreaseOverride = 5;
  mallocArenaParams.mLabel = "JS malloc";
  MallocArena = moz_create_arena_with_params(&mallocArenaParams);
  BackgroundMallocArena = moz_create_arena_with_params(&mallocArenaParams);

  arena_params_t params;
  params.mMaxDirtyIncreaseOverride = 5;
  params.mFlags |= ARENA_FLAG_RANDOMIZE_SMALL_ENABLED;
  params.mLabel = "Array buffer contents";
  ArrayBufferContentsArena = moz_create_arena_with_params(&params);
  params.mLabel = "String buffer contents";
  StringBufferArena = moz_create_arena_with_params(&params);
}

void js::ShutDownMallocAllocator() {
  // Until Bug 1364359 is fixed it is unsafe to call moz_dispose_arena.
  // moz_dispose_arena(MallocArena);
  // moz_dispose_arena(ArrayBufferContentsArena);
}

extern void js::AssertJSStringBufferInCorrectArena(const void* ptr) {
//  `jemalloc_ptr_info()` only exists if MOZ_MEMORY is defined, and it only
//  returns an arenaId if MOZ_DEBUG is defined. Otherwise, this function is
//  a no-op.
#if defined(MOZ_MEMORY) && defined(MOZ_DEBUG)
  if (ptr && !TlsContext.get()->nursery().isInside(ptr)) {
    jemalloc_ptr_info_t ptrInfo{};
    jemalloc_ptr_info(ptr, &ptrInfo);
    MOZ_ASSERT(ptrInfo.tag != TagUnknown);
    MOZ_ASSERT(ptrInfo.arenaId == js::StringBufferArena);
  }
#endif
}

JS_PUBLIC_API void JS_Assert(const char* s, const char* file, int ln) {
  MOZ_ReportAssertionFailure(s, file, ln);
  MOZ_CRASH();
}

