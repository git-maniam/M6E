/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef Mutex_h
#define Mutex_h

#  include <pthread.h>
#  include <os/lock.h>

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/ThreadSafety.h"

// For information about the following undocumented flags and functions see
// https://github.com/apple/darwin-xnu/blob/main/bsd/sys/ulock.h and
// https://github.com/apple/darwin-libplatform/blob/main/private/os/lock_private.h
#  define OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION (0x00010000)
#  define OS_UNFAIR_LOCK_ADAPTIVE_SPIN (0x00040000)

extern "C" {

typedef uint32_t os_unfair_lock_options_t;
OS_UNFAIR_LOCK_AVAILABILITY
OS_EXPORT OS_NOTHROW OS_NONNULL_ALL void os_unfair_lock_lock_with_options(
    os_unfair_lock_t lock, os_unfair_lock_options_t options);
}

// Mutexes are based on spinlocks.  We can't use normal pthread spinlocks in all
// places, because they require malloc()ed memory, which causes bootstrapping
// issues in some cases.  We also can't use non-constexpr constructors, because
// for statics, they would fire after the first use of malloc, resetting the
// locks.
//
// A constexpr constructor is provided so that Mutex can be part of something
// that is constinit, but the mutex won't be initialised, you must still
// call Init() before the mutex can be used.
struct MOZ_CAPABILITY("mutex") Mutex
{
  os_unfair_lock mMutex = OS_UNFAIR_LOCK_INIT;

#ifdef MOZ_DEBUG
  bool mInitialised = false;

  // Called by StaticMutex
  explicit constexpr Mutex(bool aInitialised) : mInitialised(aInitialised) {}
#else
  explicit constexpr Mutex(bool aIgnored) {}
#endif

  // Although a constexpr constructor is provided, it will not initialise the
  // mutex and calling Init() is required.
  constexpr Mutex() = default;

  // (Re-)initializes a mutex. Returns whether initialization succeeded.
  inline bool Init() {
#ifdef MOZ_DEBUG
    mInitialised = true;
#endif
    mMutex = OS_UNFAIR_LOCK_INIT;
    return true;
  }

  inline void Lock() MOZ_CAPABILITY_ACQUIRE() {
    MOZ_ASSERT(mInitialised);

    // We rely on a non-public function to improve performance here.
    // The OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION flag informs the kernel that
    // the calling thread is able to make progress even in absence of actions
    // from other threads and the OS_UNFAIR_LOCK_ADAPTIVE_SPIN one causes the
    // kernel to spin on a contested lock if the owning thread is running on
    // the same physical core (presumably only on x86 CPUs given that ARM
    // macs don't have cores capable of SMT).
    os_unfair_lock_lock_with_options(
        &mMutex,
        OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION | OS_UNFAIR_LOCK_ADAPTIVE_SPIN);
  }

  [[nodiscard]] bool TryLock() MOZ_TRY_ACQUIRE(true);

  inline void Unlock() MOZ_CAPABILITY_RELEASE() {
    MOZ_ASSERT(mInitialised);

    os_unfair_lock_unlock(&mMutex);
  }

  static bool SpinInKernelSpace();
  static const bool gSpinInKernelSpace;
};

// Mutex that can be used for static initialization.
struct MOZ_CAPABILITY("mutex") StaticMutex : public Mutex {
  constexpr StaticMutex() : Mutex(true) {}
};

typedef pthread_t ThreadId;
inline ThreadId GetThreadId() { return pthread_self(); }
inline bool ThreadIdEqual(ThreadId a, ThreadId b) {
  return pthread_equal(a, b);
}

class MOZ_CAPABILITY("mutex") MaybeMutex : public Mutex {
 public:
  enum DoLock {
    MUST_LOCK,
    AVOID_LOCK_UNSAFE,
  };

  bool Init(DoLock aDoLock) {
    mDoLock = aDoLock;
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    mThreadId = GetThreadId();
#endif
    return Mutex::Init();
  }

  // Re initialise after fork(), assumes that mDoLock is already initialised.
  void Reinit(pthread_t aForkingThread) {
    if (mDoLock == MUST_LOCK) {
      Mutex::Init();
      return;
    }
#  ifdef MOZ_DEBUG
    // If this is an eluded lock we can only safely re-initialise it if the
    // thread that called fork is the one that owns the lock.
    if (pthread_equal(mThreadId, aForkingThread)) {
      mThreadId = GetThreadId();
      Mutex::Init();
    } else {
      // We can't guantee that whatever resource this lock protects (probably a
      // jemalloc arena) is in a consistent state.
      mDeniedAfterFork = true;
    }
#  endif
  }

  inline void Lock() MOZ_CAPABILITY_ACQUIRE() {
    if (ShouldLock()) {
      Mutex::Lock();
    }
  }

  inline void Unlock() MOZ_CAPABILITY_RELEASE() {
    if (ShouldLock()) {
      Mutex::Unlock();
    }
  }

  // Return true if we can use this resource from this thread, either because
  // we'll use the lock or because this is the only thread that will access the
  // protected resource.
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool SafeOnThisThread() const {
    return mDoLock == MUST_LOCK || ThreadIdEqual(GetThreadId(), mThreadId);
  }
#endif

  bool LockIsEnabled() const { return mDoLock == MUST_LOCK; }

 private:
  bool ShouldLock() {
    MOZ_ASSERT(!mDeniedAfterFork);

    if (mDoLock == MUST_LOCK) {
      return true;
    }

    MOZ_ASSERT(SafeOnThisThread());
    return false;
  }

  DoLock mDoLock = MUST_LOCK;
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  ThreadId mThreadId;
#endif
#if (!defined(XP_WIN) && defined(DEBUG))
  bool mDeniedAfterFork = false;
#endif
};

template <typename T>
struct MOZ_SCOPED_CAPABILITY MOZ_RAII AutoLock {
  explicit AutoLock(T& aMutex) MOZ_CAPABILITY_ACQUIRE(aMutex) : mMutex(aMutex) {
    mMutex.Lock();
  }

  ~AutoLock() MOZ_CAPABILITY_RELEASE() { mMutex.Unlock(); }

  AutoLock(const AutoLock&) = delete;
  AutoLock(AutoLock&&) = delete;

 private:
  T& mMutex;
};

using MutexAutoLock = AutoLock<Mutex>;

using MaybeMutexAutoLock = AutoLock<MaybeMutex>;

extern StaticMutex gInitLock MOZ_UNANNOTATED;

#endif
