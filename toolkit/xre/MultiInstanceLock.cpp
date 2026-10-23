/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MultiInstanceLock.h"

#include "commonupdatedir.h"  // for GetInstallHash
#include "mozilla/UniquePtr.h"
#include "nsPrintfCString.h"
#include "nsPromiseFlatString.h"
#include "nsXULAppAPI.h"
#include "updatedefines.h"  // for NS_t* definitions

#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>


#  include "nsILocalFileMac.h"

namespace mozilla {

bool GetMultiInstanceLockFileName(const char* nameToken,
                                  const char16_t* installPath,
                                  nsCString& filePath) {
  mozilla::UniquePtr<NS_tchar[]> pathHash;
  if (!GetInstallHash(installPath, pathHash)) {
    return false;
  }

  // On POSIX platforms the base path is /tmp/[vendor][nameToken]-[pathHash].
  filePath = nsPrintfCString("/tmp/%s%s-%s", MOZ_APP_VENDOR, nameToken,
                             pathHash.get());


  return true;
}

MultiInstLockHandle OpenMultiInstanceLock(const char* nameToken,
                                          const char16_t* installPath) {
  nsCString filePath;
  if (!GetMultiInstanceLockFileName(nameToken, installPath, filePath)) {
    return MULTI_INSTANCE_LOCK_HANDLE_ERROR;
  }

  // Open a file handle with full privileges and sharing, and then attempt to
  // take a shared (nonexclusive, read-only) lock on it.
  int fd = ::open(PromiseFlatCString(filePath).get(),
                  O_CLOEXEC | O_CREAT | O_NOFOLLOW,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (fd != -1) {
    // We would like to ensure that the lock file is deleted when we are done
    // with it. The normal way to do that would be to call unlink on it right
    // now, but that would immediately delete the name from the file system, and
    // we need other instances to be able to open that name and get the same
    // inode, so we can't unlink the file before we're done with it. This means
    // we accept some unreliability in getting the file deleted, but it's a zero
    // byte file in the tmp directory, so having it stay around isn't the worst.
    struct flock l = {0};
    l.l_start = 0;
    l.l_len = 0;
    l.l_type = F_RDLCK;
    if (::fcntl(fd, F_SETLK, &l)) {
      ::close(fd);
      fd = -1;
    }
  }
  return fd;

}

void ReleaseMultiInstanceLock(MultiInstLockHandle lock) {
  if (lock != MULTI_INSTANCE_LOCK_HANDLE_ERROR) {
    // If we're the last instance, then unlink the lock file. There is a race
    // condition here that may cause an instance to fail to open the same inode
    // as another even though they use the same path, but there's no reasonable
    // way to avoid that without skipping deleting the file at all, so we accept
    // that risk.
    bool otherInstance = true;
    if (IsOtherInstanceRunning(lock, &otherInstance) && !otherInstance) {
      // Recover the file's path so we can unlink it.
      // There's no error checking in here because we're content to let the file
      // hang around if any of this fails (which can happen if for example we're
      // on a system where /proc/self/fd does not exist); this is a zero-byte
      // file in the tmp directory after all.
      UniquePtr<NS_tchar[]> linkPath = MakeUnique<NS_tchar[]>(MAXPATHLEN + 1);
      NS_tsnprintf(linkPath.get(), MAXPATHLEN + 1, "/proc/self/fd/%d", lock);
      UniquePtr<NS_tchar[]> lockFilePath =
          MakeUnique<NS_tchar[]>(MAXPATHLEN + 1);
      if (::readlink(linkPath.get(), lockFilePath.get(), MAXPATHLEN + 1) !=
          -1) {
        ::unlink(lockFilePath.get());
      }
    }
    // Now close the lock file, which will release the lock.
    ::close(lock);
  }
}

bool IsOtherInstanceRunning(MultiInstLockHandle lock, bool* aResult) {
  // Every running instance has opened a readonly lock, and read locks prevent
  // write locks from being opened, so to see if we are the only instance, we
  // attempt to take a write lock, and if it succeeds then that must mean there
  // are no other read locks open and therefore no other instances.
  if (lock == MULTI_INSTANCE_LOCK_HANDLE_ERROR) {
    return false;
  }

  // See if we would be allowed to set a write lock (no need to actually do so).
  struct flock l = {0};
  l.l_start = 0;
  l.l_len = 0;
  l.l_type = F_WRLCK;
  if (::fcntl(lock, F_GETLK, &l)) {
    return false;
  }
  *aResult = l.l_type != F_UNLCK;
  return true;

}

already_AddRefed<nsIFile> GetNormalizedAppFile(nsIFile* aAppFile) {
  // If we're given an app file, use it; otherwise, get it from the ambient
  // directory service.
  nsresult rv;
  nsCOMPtr<nsIFile> appFile;
  if (aAppFile) {
    rv = aAppFile->Clone(getter_AddRefs(appFile));
    NS_ENSURE_SUCCESS(rv, nullptr);
  } else {
    nsCOMPtr<nsIProperties> dirSvc =
        do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
    NS_ENSURE_TRUE(dirSvc, nullptr);

    rv = dirSvc->Get(XRE_EXECUTABLE_FILE, NS_GET_IID(nsIFile),
                     getter_AddRefs(appFile));
    NS_ENSURE_SUCCESS(rv, nullptr);
  }

  // It is possible that the path we have is on a case insensitive
  // filesystem in which case the path may vary depending on how the
  // application is called. We want to normalize the case somehow.
  // On Linux XRE_EXECUTABLE_FILE already seems to be set to the correct path.
  //
  // See similar nsXREDirProvider::GetInstallHash. The main difference here is
  // to allow lookup to fail on OSX, because some tests use a nonexistent
  // appFile.
  // On OSX roundtripping through an FSRef fixes the case.
  FSRef ref;
  nsCOMPtr<nsILocalFileMac> macFile = do_QueryInterface(appFile);
  if (macFile && NS_SUCCEEDED(macFile->GetFSRef(&ref)) &&
      NS_SUCCEEDED(NS_NewLocalFileWithFSRef(&ref, getter_AddRefs(macFile)))) {
    appFile = macFile.forget();
  } else {
    NS_WARNING("Failed to resolve install directory.");
  }

  return appFile.forget();
}

};  // namespace mozilla
