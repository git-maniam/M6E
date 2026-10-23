/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CrossProcessMutex_h
#define mozilla_CrossProcessMutex_h

#include "base/process.h"
#include "mozilla/Mutex.h"

#  include <pthread.h>
#  include "mozilla/ipc/SharedMemoryMapping.h"
#  include "mozilla/Atomics.h"

namespace IPC {
template <typename T>
struct ParamTraits;
}  // namespace IPC

//
// Provides:
//
//  - CrossProcessMutex, a non-recursive mutex that can be shared across
//    processes
//  - CrossProcessMutexAutoLock, an RAII class for ensuring that Mutexes are
//    properly locked and unlocked
//
// Using CrossProcessMutexAutoLock/CrossProcessMutexAutoUnlock is MUCH
// preferred to making bare calls to CrossProcessMutex.Lock and Unlock.
//
namespace mozilla {
typedef mozilla::ipc::MutableSharedMemoryHandle CrossProcessMutexHandle;

class CrossProcessMutex {
 public:
  /**
   * CrossProcessMutex
   * @param name A name which can reference this lock (currently unused)
   **/
  explicit CrossProcessMutex(const char* aName);
  /**
   * CrossProcessMutex
   * @param handle A handle of an existing cross process mutex that can be
   *               opened.
   */
  explicit CrossProcessMutex(CrossProcessMutexHandle aHandle);

  /**
   * ~CrossProcessMutex
   **/
  ~CrossProcessMutex();

  /**
   * Lock
   * This will lock the mutex. Any other thread in any other process that
   * has access to this mutex calling lock will block execution until the
   * initial caller of lock has made a call to Unlock.
   *
   * If the owning process is terminated unexpectedly the mutex will be
   * released.
   **/
  void Lock();

  /**
   * Unlock
   * This will unlock the mutex. A single thread currently waiting on a lock
   * call will resume execution and aquire ownership of the lock. No
   * guarantees are made as to the order in which waiting threads will resume
   * execution.
   **/
  void Unlock();

  /**
   * CloneHandle
   * This function is called to generate a serializable structure that can
   * be sent to the specified process and opened on the other side.
   *
   * @returns A handle that can be shared to another process
   */
  CrossProcessMutexHandle CloneHandle();

 private:
  friend struct IPC::ParamTraits<CrossProcessMutex>;

  // Prevent clang-tidy from thinking those are unimplemented constructors when
  // analyzing only headers.
  // NOLINTBEGIN(modernize-use-equals-delete)
  CrossProcessMutex();
  CrossProcessMutex(const CrossProcessMutex&);
  CrossProcessMutex& operator=(const CrossProcessMutex&);
  // NOLINTEND(modernize-use-equals-delete)

  mozilla::ipc::SharedMemoryMappingWithHandle mSharedBuffer;
  pthread_mutex_t* mMutex;
  mozilla::Atomic<int32_t>* mCount;
};

typedef detail::BaseAutoLock<CrossProcessMutex&> CrossProcessMutexAutoLock;
typedef detail::BaseAutoUnlock<CrossProcessMutex&> CrossProcessMutexAutoUnlock;

}  // namespace mozilla

#endif
