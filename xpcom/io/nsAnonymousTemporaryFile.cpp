/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAnonymousTemporaryFile.h"

#include "SpecialSystemDirectory.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsXULAppAPI.h"
#include "prio.h"


using namespace mozilla;

// We store the temp files in the system temp dir.
//
// On Windows systems in particular we use a sub-directory of the temp
// directory, because:
//   1. DELETE_ON_CLOSE is unreliable on Windows, in particular if we power
//      cycle (and perhaps if we crash) the files are not deleted. We store
//      the temporary files in a known sub-dir so that we can find and delete
//      them easily and quickly.
//   2. On Windows NT the system temp dir is in the user's $HomeDir/AppData,
//      so we can be sure the user always has write privileges to that
//      directory; if the sub-dir for our temp files was in some shared location
//      and was created by a privileged user, it's possible that other users
//      wouldn't have write access to that sub-dir. (Non-Windows systems
//      don't store their temp files in a sub-dir, so this isn't an issue on
//      those platforms).
//   3. Content processes can access the system temp dir
//      (NS_GetSpecialDirectory fails on NS_APP_USER_PROFILE_LOCAL_50_DIR
//      for content process for example, which is where we previously stored
//      temp files on Windows). This argument applies to all platforms, not
//      just Windows.
static nsresult GetTempDir(nsIFile** aTempDir) {
  if (NS_WARN_IF(!aTempDir)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv =
      GetSpecialSystemDirectory(OS_TemporaryDirectory, getter_AddRefs(tmpFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }


  tmpFile.forget(aTempDir);

  return NS_OK;
}

nsresult NS_OpenAnonymousTemporaryNsIFile(nsIFile** aFile) {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (NS_WARN_IF(!aFile)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsresult rv;
  nsCOMPtr<nsIFile> tmpFile;
  rv = GetTempDir(getter_AddRefs(tmpFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Give the temp file a name with a random element. CreateUnique will also
  // append a counter to the name if it encounters a name collision. Adding
  // a random element to the name reduces the likelihood of a name collision,
  // so that CreateUnique() doesn't end up trying a lot of name variants in
  // its "try appending an incrementing counter" loop, as file IO can be
  // expensive on some mobile flash drives.
  nsAutoCString name("mozilla-temp-");
  name.AppendInt(rand());

  rv = tmpFile->AppendNative(name);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = tmpFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  tmpFile.forget(aFile);
  return NS_OK;
}

nsresult NS_OpenAnonymousTemporaryFile(PRFileDesc** aOutFileDesc) {
  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = NS_OpenAnonymousTemporaryNsIFile(getter_AddRefs(tmpFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = tmpFile->OpenNSPRFileDesc(PR_RDWR | nsIFile::DELETE_ON_CLOSE, PR_IRWXU,
                                 aOutFileDesc);

  return rv;
}

