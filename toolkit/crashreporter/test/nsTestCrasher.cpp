#include "mozilla/Assertions.h"

#include <stdio.h>
#include <map>
#include <vector>

#include "nscore.h"
#include "ExceptionThrower.h"

#  include <sys/types.h>
#  include <sys/fcntl.h>
#  include <unistd.h>
#  include <dlfcn.h>  // For dlsym()
#  include <sys/syscall.h>
static void InvokeNonExistentSyscall() {
#  pragma clang diagnostic push
  // syscall() is deprecated on macos but doesn't have a replacement.
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // Unassigned syscall number.
  syscall(0x7ffffff0);
#  pragma clang diagnostic pop
}
// See https://github.com/apple/darwin-xnu/blob/main/bsd/sys/guarded.h
#  define GUARD_CLOSE (1u << 0)
#  define GUARD_DUP (1u << 1)
#  define GUARD_SOCKET_IPC (1u << 2)
#  define GUARD_FILEPORT (1u << 3)
#  define GUARD_WRITE (1u << 4)
typedef uint64_t guardid_t;
typedef int (*guarded_open_np_t)(const char*, const guardid_t*, u_int, int,
                                 ...);

#  include <pthread.h>

#ifdef MOZ_PHC
#  include "PHC.h"
#endif

/*
 * This pure virtual call example is from MSDN
 */
class A;

void fcn(A*);

class A {
 public:
  virtual void f() = 0;
  A() { fcn(this); }
};

class B : A {
  void f() override {}

 public:
  void use() {}
};

void fcn(A* p) { p->f(); }

void PureVirtualCall() {
  // generates a pure virtual function call
  B b;
  b.use();  // make sure b's actually used
}

extern "C" {
}

// Keep these in sync with CrashTestUtils.sys.mjs!
const int16_t CRASH_INVALID_POINTER_DEREF = 0;
const int16_t CRASH_PURE_VIRTUAL_CALL = 1;
const int16_t CRASH_OOM = 3;
const int16_t CRASH_MOZ_CRASH = 4;
const int16_t CRASH_ABORT = 5;
const int16_t CRASH_UNCAUGHT_EXCEPTION = 6;
#ifdef MOZ_PHC
const int16_t CRASH_PHC_USE_AFTER_FREE = 21;
const int16_t CRASH_PHC_DOUBLE_FREE = 22;
const int16_t CRASH_PHC_BOUNDS_VIOLATION = 23;
#endif
const int16_t CRASH_EXC_GUARD = 25;
const int16_t CRASH_STACK_OVERFLOW = 26;
const int16_t CRASH_STL_VECTOR_OOB = 27;
const int16_t CRASH_SIGSYS = 28;


#ifdef MOZ_PHC
uint8_t* GetPHCAllocation(size_t aSize) {
  // A crude but effective way to get a PHC allocation.
  for (int i = 0; i < 2000000; i++) {
    uint8_t* p = (uint8_t*)malloc(aSize);
    if (mozilla::phc::IsPHCAllocation(p, nullptr)) {
      return p;
    }
    free(p);
  }
  // This failure doesn't seem to occur in practice...
  MOZ_CRASH("failed to get a PHC allocation");
}
#endif

static int64_t recurse(int64_t aRandom) {
  char buff[256] = {};
  int64_t result = aRandom;

  strncpy(buff, "This is gibberish", sizeof(buff));

  for (auto& c : buff) {
    result += c;
  }

  if (result == 0) {
    return result;
  }

  return recurse(result) + 1;
}

static void* overflow_stack(void* aInput) {
  int64_t result = recurse(*((int64_t*)(aInput)));

  return (void*)result;
}

extern "C" NS_EXPORT void Crash(int16_t how) {
  switch (how) {
    case CRASH_INVALID_POINTER_DEREF: {
      volatile int* foo = (int*)0x42;
      *foo = 0;
      // not reached
      break;
    }
    case CRASH_PURE_VIRTUAL_CALL: {
      PureVirtualCall();
      // not reached
      break;
    }
    case CRASH_OOM: {
      [[maybe_unused]] void* r0 = moz_xmalloc((size_t)-1);
      [[maybe_unused]] void* r1 = moz_xmalloc((size_t)-1);
      [[maybe_unused]] void* r2 = moz_xmalloc((size_t)-1);
      break;
    }
    case CRASH_MOZ_CRASH: {
      MOZ_CRASH();
      break;
    }
    case CRASH_ABORT: {
      abort();
      break;
    }
    case CRASH_UNCAUGHT_EXCEPTION: {
      ThrowException();
      break;
    }
#ifdef MOZ_PHC
    case CRASH_PHC_USE_AFTER_FREE: {
      // Do a UAF, triggering a crash.
      uint8_t* p = GetPHCAllocation(32);
      free(p);
      p[0] = 0;
      // not reached
    }
    case CRASH_PHC_DOUBLE_FREE: {
      // Do a double free, triggering a crash.
      uint8_t* p = GetPHCAllocation(64);
      free(p);
      free(p);
      // not reached
    }
    case CRASH_PHC_BOUNDS_VIOLATION: {
      // Do a bounds violation, triggering a crash.
      uint8_t* p = GetPHCAllocation(96);
      p[96] = 0;
      // not reached
    }
#endif
    case CRASH_SIGSYS: {
      InvokeNonExistentSyscall();
      break;  // This should be unreachable
    }
    case CRASH_EXC_GUARD: {
      guarded_open_np_t dl_guarded_open_np;
      void* kernellib =
          (void*)dlopen("/usr/lib/system/libsystem_kernel.dylib", RTLD_GLOBAL);
      dl_guarded_open_np =
          (guarded_open_np_t)dlsym(kernellib, "guarded_open_np");
      const guardid_t guard = 0x123456789ABCDEFULL;
      // Guard the file descriptor against regular close() calls
      int fd = dl_guarded_open_np(
          "/tmp/try.txt", &guard,
          GUARD_CLOSE | GUARD_DUP | GUARD_SOCKET_IPC | GUARD_FILEPORT,
          O_CREAT | O_CLOEXEC | O_RDWR, 0666);

      if (fd != -1) {
        close(fd);
        // not reached
      }
    }
    case CRASH_STACK_OVERFLOW: {
      pthread_t thread_id;
      int64_t data = 1337;
      int rv = pthread_create(&thread_id, nullptr, overflow_stack, &data);
      if (!rv) {
        pthread_join(thread_id, nullptr);
      }

      break;  // This should be unreachable
    }
    case CRASH_STL_VECTOR_OOB: {
      // Make a vector with a power-of-2 size, remove the last element, then
      // access just past the new end of the vector. The idea here is that if
      // hardening is not enabled then the out-of-bounds access likely won't
      // crash, because most implementations probably won't shrink the
      // underlying buffer.
      std::vector<int32_t> v;
      const size_t initSize = 8;
      v.resize(initSize, 9);
      v.pop_back();
      // Out-of-bounds access.
      printf("CRASH_STL_VECTOR_OOB: %d\n", v[initSize - 1]);
      // This should be unreachable, if hardening is enabled.
      break;
    }
    default:
      break;
  }
}

extern "C" NS_EXPORT void EnablePHC() {
#ifdef MOZ_PHC
  mozilla::phc::SetPHCState(mozilla::phc::PHCState::Enabled);
#endif
};


extern "C" NS_EXPORT uint32_t GetWin64CFITestFnAddrOffset(int16_t fnid) {
  return 0;
}
