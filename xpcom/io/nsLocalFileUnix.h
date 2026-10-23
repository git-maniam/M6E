/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of nsIFile for ``Unixy'' systems.
 */

#ifndef _nsLocalFileUNIX_H_
#define _nsLocalFileUNIX_H_

#include <sys/stat.h>

#include "nsString.h"
#include "nscore.h"
#  include "nsILocalFileMac.h"

// stat64 and lstat64 are deprecated on OS X. Normal stat and lstat are
// 64-bit by default on OS X 10.6+.
#  define STAT stat
#  define LSTAT lstat

#if defined(HAVE_SYS_QUOTA_H) && defined(HAVE_LINUX_QUOTA_H)
#  define USE_LINUX_QUOTACTL
#endif

class nsLocalFile final
    : public nsILocalFileMac
{
 public:
  NS_DEFINE_STATIC_CID_ACCESSOR(NS_LOCAL_FILE_CID)

  nsLocalFile();

  static nsresult nsLocalFileConstructor(const nsIID& aIID,
                                         void** aInstancePtr);

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIFILE
  NS_DECL_NSILOCALFILEMAC

 private:
  nsLocalFile(const nsLocalFile& aOther);
  ~nsLocalFile() = default;

 protected:
  nsCString mPath;

  void LocateNativeLeafName(nsACString::const_iterator&,
                            nsACString::const_iterator&);

  nsresult CopyDirectoryTo(nsIFile* aNewParent);
  nsresult CreateAllAncestors(uint32_t aPermissions);
  nsresult GetNativeTargetPathName(nsIFile* aNewParent,
                                   const nsACString& aNewName,
                                   nsACString& aResult);

  nsresult StatFile(struct STAT* statInfo);

  nsresult CreateAndKeepOpen(uint32_t aType, int aFlags, uint32_t aPermissions,
                             bool aSkipAncestors, PRFileDesc** aResult);

  enum class TimeField { AccessedTime, ModifiedTime };
  nsresult SetTimeImpl(PRTime aTime, TimeField aTimeField, bool aFollowLinks);
  nsresult GetTimeImpl(PRTime* aTime, TimeField aTimeField, bool aFollowLinks);

  nsresult GetCreationTimeImpl(PRTime* aCreationTime, bool aFollowLinks);

#if defined(USE_LINUX_QUOTACTL)
  template <typename StatInfoFunc, typename QuotaInfoFunc>
  nsresult GetDiskInfo(StatInfoFunc&& aStatInfoFunc,
                       QuotaInfoFunc&& aQuotaInfoFunc, int64_t* aResult);
#else
  template <typename StatInfoFunc>
  nsresult GetDiskInfo(StatInfoFunc&& aStatInfoFunc, int64_t* aResult);
#endif
};

#endif /* _nsLocalFileUNIX_H_ */
