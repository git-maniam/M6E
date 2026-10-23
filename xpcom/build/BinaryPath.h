/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BinaryPath_h
#define mozilla_BinaryPath_h

#include "nsXPCOMPrivate.h"  // for MAXPATHLEN
#  include <CoreFoundation/CoreFoundation.h>
#if defined(__FreeBSD__) || defined(__DragonFly__) || \
    defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__)
#  include <sys/sysctl.h>
#endif
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"

#ifdef MOZILLA_INTERNAL_API
#  include "nsCOMPtr.h"
#  include "nsIFile.h"
#  include "nsString.h"
#endif

namespace mozilla {

class BinaryPath {
 public:
  static nsresult Get(char aResult[MAXPATHLEN]) {
    // Works even if we're not bundled.
    CFBundleRef appBundle = CFBundleGetMainBundle();
    if (!appBundle) {
      return NS_ERROR_FAILURE;
    }

    CFURLRef executableURL = CFBundleCopyExecutableURL(appBundle);
    if (!executableURL) {
      return NS_ERROR_FAILURE;
    }

    nsresult rv;
    if (CFURLGetFileSystemRepresentation(executableURL, false, (UInt8*)aResult,
                                         MAXPATHLEN)) {
      // Sanitize path in case the app was launched from Terminal via
      // './firefox' for example.
      size_t readPos = 0;
      size_t writePos = 0;
      while (aResult[readPos] != '\0') {
        if (aResult[readPos] == '.' && aResult[readPos + 1] == '/') {
          readPos += 2;
        } else {
          aResult[writePos] = aResult[readPos];
          readPos++;
          writePos++;
        }
      }
      aResult[writePos] = '\0';
      rv = NS_OK;
    } else {
      rv = NS_ERROR_FAILURE;
    }

    CFRelease(executableURL);
    return rv;
  }


 public:
  static UniqueFreePtr<char> Get() {
    char path[MAXPATHLEN];
    if (NS_FAILED(Get(path))) {
      return nullptr;
    }
    UniqueFreePtr<char> result;
    result.reset(strdup(path));
    return result;
  }

#ifdef MOZILLA_INTERNAL_API
  static nsresult GetFile(nsIFile** aResult) {
    nsCOMPtr<nsIFile> lf;
    char exePath[MAXPATHLEN];
    nsresult rv = Get(exePath);
    if (NS_FAILED(rv)) {
      return rv;
    }
    MOZ_TRY(NS_NewPathStringLocalFile(DependentPathString(exePath),
                                      getter_AddRefs(lf)));
    lf.forget(aResult);
    return NS_OK;
  }
#endif
};

}  // namespace mozilla

#endif /* mozilla_BinaryPath_h */
