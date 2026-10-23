/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Atomics.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Services.h"
#include "nsCOMPtr.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIRunnable.h"
#include "nsISimpleEnumerator.h"
#include "nsMemory.h"
#include "nsThreadUtils.h"


static mozilla::Atomic<bool> sIsFlushing;
static PRIntervalTime sLastFlushTime = 0;

// static
bool nsMemory::IsLowMemoryPlatform() {
  return false;
}

static void RunFlushers(const char16_t* aReason) {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    // Instead of:
    //  os->NotifyObservers(this, "memory-pressure", aReason);
    // we are going to do this manually to see who/what is
    // deallocating.

    nsCOMPtr<nsISimpleEnumerator> e;
    os->EnumerateObservers("memory-pressure", getter_AddRefs(e));

    if (e) {
      nsCOMPtr<nsIObserver> observer;
      bool loop = true;

      while (NS_SUCCEEDED(e->HasMoreElements(&loop)) && loop) {
        nsCOMPtr<nsISupports> supports;
        e->GetNext(getter_AddRefs(supports));

        if (!supports) {
          continue;
        }

        observer = do_QueryInterface(supports);
        observer->Observe(observer, "memory-pressure", aReason);
      }
    }
  }

  sIsFlushing = false;
}

static nsresult FlushMemory(const char16_t* aReason, bool aImmediate) {
  if (aImmediate) {
    // They've asked us to run the flusher *immediately*. We've
    // got to be on the UI main thread for us to be able to do
    // that...are we?
    if (!NS_IsMainThread()) {
      NS_ERROR("can't synchronously flush memory: not on UI thread");
      return NS_ERROR_FAILURE;
    }
  }

  bool lastVal = sIsFlushing.exchange(true);
  if (lastVal) {
    return NS_OK;
  }

  PRIntervalTime now = PR_IntervalNow();

  // Run the flushers immediately if we can; otherwise, proxy to the
  // UI thread and run 'em asynchronously.
  nsresult rv = NS_OK;
  if (aImmediate) {
    RunFlushers(aReason);
  } else {
    // Don't broadcast more than once every 1000ms to avoid being noisy
    if (PR_IntervalToMicroseconds(now - sLastFlushTime) > 1000) {
      nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
          "FlushMemory",
          [reason = aReason]() -> void { RunFlushers(reason); }));
      NS_DispatchToMainThread(runnable.forget());
    }
  }

  sLastFlushTime = now;
  return rv;
}

nsresult nsMemory::HeapMinimize(bool aImmediate) {
  return FlushMemory(u"heap-minimize", aImmediate);
}
