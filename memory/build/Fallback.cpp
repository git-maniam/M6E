/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozmemory.h"
#include "mozjemalloc.h"

#ifndef HAVE_MEMALIGN
MOZ_MEMORY_API void* memalign(size_t aAlignment, size_t aSize) {
  void* ret;
  if (posix_memalign(&ret, aAlignment, aSize) != 0) {
    return nullptr;
  }
  return ret;
}
#endif

struct SystemMalloc {
#define MALLOC_DECL(name, return_type, ...)                                \
  static inline return_type name(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    return ::name(ARGS_HELPER(ARGS, ##__VA_ARGS__));                       \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"
};

#define MALLOC_DECL(name, return_type, ...)                                   \
  MOZ_JEMALLOC_API return_type name(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    return DummyArenaAllocator<SystemMalloc>::name(                           \
        ARGS_HELPER(ARGS, ##__VA_ARGS__));                                    \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_ARENA
#include "malloc_decls.h"
