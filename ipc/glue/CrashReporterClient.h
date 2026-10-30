/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_CrashReporterClient_h
#define mozilla_ipc_CrashReporterClient_h

#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsExceptionHandler.h"
#include "nsISupportsImpl.h"

namespace mozilla::ipc {

class CrashReporterClient {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CrashReporterClient);

  static void InitSingleton();

  static void DestroySingleton();
  static RefPtr<CrashReporterClient> GetSingleton();

 private:
  explicit CrashReporterClient();
  ~CrashReporterClient();

  static StaticMutex sLock;
  static StaticRefPtr<CrashReporterClient> sClientSingleton
      MOZ_GUARDED_BY(sLock);
};

}  // namespace mozilla::ipc

#endif  // mozilla_ipc_CrashReporterClient_h
