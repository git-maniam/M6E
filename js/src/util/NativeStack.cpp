/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util/NativeStack.h"

#include "mozilla/Assertions.h"  // MOZ_ASSERT, MOZ_RELEASE_ASSERT, MOZ_CRASH

#if   defined(__wasi__)
// Nothing
#else
#  include <pthread.h>
#  if defined(SOLARIS) || defined(AIX)
#    include <ucontext.h>
#  endif
#endif

#include "js/friend/StackLimits.h"  // JS_STACK_GROWTH_DIRECTION

#if   defined(SOLARIS)

static_assert(JS_STACK_GROWTH_DIRECTION < 0);

void* js::GetNativeStackBaseImpl() {
  stack_t st;
  stack_getbounds(&st);
  return static_cast<char*>(st.ss_sp) + st.ss_size;
}

#elif defined(AIX)

static_assert(JS_STACK_GROWTH_DIRECTION < 0);

void* js::GetNativeStackBaseImpl() {
  ucontext_t context;
  getcontext(&context);
  return static_cast<char*>(context.uc_stack.ss_sp) + context.uc_stack.ss_size;
}

#elif defined(__wasi__)

// Since we rearrange the layout for wasi via --stack-first flag for the linker
// the final layout is: 0x00 | <- stack | data | heap -> |.
static void* const NativeStackBase = __builtin_frame_address(0);

void* js::GetNativeStackBaseImpl() {
  MOZ_ASSERT(JS_STACK_GROWTH_DIRECTION < 0);
  return NativeStackBase;
}

#else  // __wasi__

void* js::GetNativeStackBaseImpl() {
  pthread_t thread = pthread_self();
  return pthread_get_stackaddr_np(thread);

}

#endif /* !XP_WIN */
