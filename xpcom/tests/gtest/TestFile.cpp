/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIFile.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "prio.h"
#include "prsystem.h"

#  include <limits.h>

#  include "nsILocalFileMac.h"

#include <algorithm>

#include "gtest/gtest.h"
#include "mozilla/Debug.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gtest/MozAssertions.h"

// RAII guard for enabling/disabling the DOS device path prefix (\\?\) in tests.
// Prefixed paths allow exceeding MAX_PATH (260 characters).
// Saves the current value on construction, restores it on destruction.
// No-op on non-Windows platforms.
class TestWithPrefixWinScope {
 public:
  explicit TestWithPrefixWinScope(bool) {}
  static bool IsEnabled() { return false; }

  TestWithPrefixWinScope(const TestWithPrefixWinScope&) = delete;
  TestWithPrefixWinScope& operator=(const TestWithPrefixWinScope&) = delete;

};

static bool VerifyResult(nsresult aRV, const char* aMsg) {
  bool failed = NS_FAILED(aRV);
  EXPECT_FALSE(failed) << aMsg << " rv=" << std::hex << (unsigned int)aRV;
  return !failed;
}


static already_AddRefed<nsIFile> NewFile(nsIFile* aBase) {
  nsresult rv;
  nsCOMPtr<nsIFile> file;
  rv = NS_NewLocalFileWithFile(aBase, getter_AddRefs(file));
  VerifyResult(rv, "NS_NewLocalFileWithFile");


  return file.forget();
}

template <typename char_type>
static nsTString<char_type> FixName(const char_type* aName) {
  nsTString<char_type> name;
  for (uint32_t i = 0; aName[i]; ++i) {
    char_type ch = aName[i];
    // PR_GetPathSeparator returns the wrong value on Mac so don't use it
    name.Append(ch);
  }
  return name;
}

// Test nsIFile::AppendNative, verifying that aName is not a valid file name
static bool TestInvalidFileName(nsIFile* aBase, const char* aName) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (NS_SUCCEEDED(rv)) {
    EXPECT_NS_FAILED(rv) << "AppendNative with invalid filename " << name.get();
    return false;
  }

  return true;
}

// Test nsIFile::Create, verifying that the file exists and did not exist
// before, and leaving it there for future tests
static bool TestCreate(nsIFile* aBase, const char* aName, int32_t aType,
                       int32_t aPerm, nsIFile** aNewFile = nullptr) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " already exists";
  if (exists) {
    return false;
  }

  rv = file->Create(aType, aPerm);
  if (!VerifyResult(rv, "Create")) return false;

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_TRUE(exists) << "File " << name.get() << " was not created";
  if (!exists) {
    return false;
  }

  if (aNewFile) {
    file.forget(aNewFile);
  }

  return true;
}

// Test nsIFile::CreateUnique, verifying that the new file exists and if it
// existed before, the new file has a different name. The new file is left in
// place.
static bool TestCreateUnique(nsIFile* aBase, const char* aName, int32_t aType,
                             int32_t aPerm) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool existsBefore;
  rv = file->Exists(&existsBefore);
  if (!VerifyResult(rv, "Exists (before)")) return false;

  rv = file->CreateUnique(aType, aPerm);
  if (!VerifyResult(rv, "Create")) return false;

  bool existsAfter;
  rv = file->Exists(&existsAfter);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_TRUE(existsAfter) << "File " << name.get() << " was not created";
  if (!existsAfter) {
    return false;
  }

  if (existsBefore) {
    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    if (!VerifyResult(rv, "GetNativeLeafName")) return false;
    EXPECT_FALSE(leafName.Equals(name))
        << "File " << name.get() << " was not given a new name by CreateUnique";
    if (leafName.Equals(name)) {
      return false;
    }
  }

  return true;
}

// Test nsIFile::OpenNSPRFileDesc with DELETE_ON_CLOSE, verifying that the file
// exists and did not exist before, and leaving it there for future tests
static bool TestDeleteOnClose(nsIFile* aBase, const char* aName, int32_t aFlags,
                              int32_t aPerm) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " already exists";
  if (exists) {
    return false;
  }

  PRFileDesc* fileDesc;
  rv = file->OpenNSPRFileDesc(aFlags | nsIFile::DELETE_ON_CLOSE, aPerm,
                              &fileDesc);
  if (!VerifyResult(rv, "OpenNSPRFileDesc")) return false;
  PRStatus status = PR_Close(fileDesc);
  EXPECT_EQ(status, PR_SUCCESS)
      << "File " << name.get() << " could not be closed";
  if (status != PR_SUCCESS) {
    return false;
  }

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " was not removed on close";
  if (exists) {
    return false;
  }

  return true;
}

// Test nsIFile::Remove, verifying that the file does not exist and did before
static bool TestRemove(nsIFile* aBase, const char* aName, bool aRecursive,
                       uint32_t aExpectedRemoveCount = 1) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_TRUE(exists);
  if (!exists) {
    return false;
  }

  uint32_t removeCount = 0;
  rv = file->Remove(aRecursive, &removeCount);
  if (!VerifyResult(rv, "Remove")) return false;
  EXPECT_EQ(removeCount, aExpectedRemoveCount) << "Removal count was wrong";

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " was not removed";
  if (exists) {
    return false;
  }

  return true;
}

// Test nsIFile::MoveToNative, verifying that the file did not exist at the new
// location before and does afterward, and that it does not exist at the old
// location anymore
static bool TestMove(nsIFile* aBase, nsIFile* aDestDir, const char* aName,
                     const char* aNewName) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_TRUE(exists);
  if (!exists) {
    return false;
  }

  nsCOMPtr<nsIFile> newFile = NewFile(file);
  nsCString newName = FixName(aNewName);
  rv = newFile->MoveToNative(aDestDir, newName);
  if (!VerifyResult(rv, "MoveToNative")) return false;

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " was not moved";
  if (exists) {
    return false;
  }

  file = NewFile(aDestDir);
  if (!file) return false;
  rv = file->AppendNative(newName);
  if (!VerifyResult(rv, "AppendNative")) return false;
  bool equal;
  rv = file->Equals(newFile, &equal);
  if (!VerifyResult(rv, "Equals")) return false;
  EXPECT_TRUE(equal) << "File object was not updated to destination";
  if (!equal) {
    return false;
  }

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (new after)")) return false;
  EXPECT_TRUE(exists) << "Destination file " << newName.get()
                      << " was not created";
  if (!exists) {
    return false;
  }


  return true;
}

// Test nsIFile::CopyToNative, verifying that the file did not exist at the new
// location before and does afterward, and that it does exist at the old
// location too
static bool TestCopy(nsIFile* aBase, nsIFile* aDestDir, const char* aName,
                     const char* aNewName) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_TRUE(exists);
  if (!exists) {
    return false;
  }

  nsCOMPtr<nsIFile> newFile = NewFile(file);
  nsCString newName = FixName(aNewName);
  rv = newFile->CopyToNative(aDestDir, newName);
  if (!VerifyResult(rv, "MoveToNative")) return false;
  bool equal;
  rv = file->Equals(newFile, &equal);
  if (!VerifyResult(rv, "Equals")) return false;
  EXPECT_TRUE(equal) << "File object updated unexpectedly";
  if (!equal) {
    return false;
  }

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_TRUE(exists) << "File " << name.get() << " was removed";
  if (!exists) {
    return false;
  }

  file = NewFile(aDestDir);
  if (!file) return false;
  rv = file->AppendNative(newName);
  if (!VerifyResult(rv, "AppendNative")) return false;

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (new after)")) return false;
  EXPECT_TRUE(exists) << "Destination file " << newName.get()
                      << " was not created";
  if (!exists) {
    return false;
  }

  return true;
}

// Test nsIFile::GetParent
static bool TestParent(nsIFile* aBase, nsIFile* aStart) {
  nsCOMPtr<nsIFile> file = NewFile(aStart);
  if (!file) return false;

  nsCOMPtr<nsIFile> parent;
  nsresult rv = file->GetParent(getter_AddRefs(parent));
  VerifyResult(rv, "GetParent");

  bool equal;
  rv = parent->Equals(aBase, &equal);
  VerifyResult(rv, "Equals");
  EXPECT_TRUE(equal) << "Incorrect parent";
  if (!equal) {
    return false;
  }

  return true;
}

// Test nsIFile::Contains
static bool TestContains(nsIFile* aBase) {
  nsCOMPtr<nsIFile> sub = NewFile(aBase);
  if (!sub) return false;
  nsresult rv = sub->AppendNative(nsDependentCString("sub"));
  if (!VerifyResult(rv, "AppendNative sub")) return false;

  nsCOMPtr<nsIFile> subSlashSub = NewFile(sub);
  if (!subSlashSub) return false;
  rv = subSlashSub->AppendNative(nsDependentCString("sub"));
  if (!VerifyResult(rv, "AppendNative sub/sub")) return false;

  nsCOMPtr<nsIFile> subsub = NewFile(aBase);
  if (!subsub) return false;
  rv = subsub->AppendNative(nsDependentCString("subsub"));
  if (!VerifyResult(rv, "AppendNative subsub")) return false;

  bool contains;
  rv = sub->Contains(subsub, &contains);
  VerifyResult(rv, "sub contains subsub");
  EXPECT_FALSE(contains) << "sub contains subsub is false";

  rv = sub->Contains(sub, &contains);
  VerifyResult(rv, "sub contains sub (itself)");
  EXPECT_FALSE(contains) << "sub contains sub (itself) is false";

  rv = sub->Contains(subSlashSub, &contains);
  VerifyResult(rv, "sub contains sub/sub");
  EXPECT_TRUE(contains) << "sub contains sub/sub is true";

  // Regression test for bug 1975674: Contains null should not crash.
  rv = sub->Contains(nullptr, &contains);
  EXPECT_NS_FAILED(rv) << "Contains null fails";
  EXPECT_EQ(rv, NS_ERROR_INVALID_ARG) << "rv for contains(null)";

  return true;
}

// Test nsIFile::Normalize and native path setting/getting
static bool TestNormalizeNativePath(nsIFile* aBase, nsIFile* aStart) {
  nsCOMPtr<nsIFile> file = NewFile(aStart);
  if (!file) return false;

  auto path = file->NativePath();
  path.Append(FixName("/./.."));
  nsresult rv = file->InitWithNativePath(path);
  VerifyResult(rv, "InitWithNativePath");
  rv = file->Normalize();
  VerifyResult(rv, "Normalize");
  path = file->NativePath();

  auto basePath = aBase->NativePath();
  VerifyResult(rv, "GetNativePath (base)");

  EXPECT_TRUE(path.Equals(basePath))
      << "Incorrect normalization: " << file->HumanReadablePath().get() << " - "
      << aBase->HumanReadablePath().get();
  if (!path.Equals(basePath)) {
    return false;
  }

  return true;
}

// Test nsIFile::GetDiskSpaceAvailable
static bool TestDiskSpaceAvailable(nsIFile* aBase) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  int64_t diskSpaceAvailable = 0;
  nsresult rv = file->GetDiskSpaceAvailable(&diskSpaceAvailable);
  VerifyResult(rv, "GetDiskSpaceAvailable");

  EXPECT_GE(diskSpaceAvailable, 0);

  return true;
}

// Test nsIFile::GetDiskCapacity
static bool TestDiskCapacity(nsIFile* aBase) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  int64_t diskCapacity = 0;
  nsresult rv = file->GetDiskCapacity(&diskCapacity);
  VerifyResult(rv, "GetDiskCapacity");

  EXPECT_GE(diskCapacity, 0);

  return true;
}

static void SetupAndTestFunctions(const nsAString& aDirName,
                                  bool aTestCreateUnique, bool aTestNormalize) {
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_TRUE(VerifyResult(rv, "Getting temp directory"));


  rv = base->Append(aDirName);
  ASSERT_TRUE(
      VerifyResult(rv, nsPrintfCString("Appending %s to temp directory name",
                                       NS_ConvertUTF16toUTF8(aDirName).get())
                           .get()));

  // Remove the directory in case tests failed and left it behind.
  // don't check result since it might not be there
  base->Remove(true);

  // Now create the working directory we're going to use
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_TRUE(VerifyResult(rv, "Creating temp directory"));

  // Now we can safely normalize the path
  if (aTestNormalize) {
    rv = base->Normalize();
    ASSERT_TRUE(VerifyResult(rv, "Normalizing temp directory name"));
  }

  // Initialize subdir object for later use
  nsCOMPtr<nsIFile> subdir = NewFile(base);
  ASSERT_TRUE(subdir);

  rv = subdir->AppendNative(nsDependentCString("subdir"));
  ASSERT_TRUE(VerifyResult(rv, "Appending 'subdir' to test dir name"));

  // ---------------
  // End setup code.
  // ---------------

  // Test leafName
  nsString leafName;
  rv = base->GetLeafName(leafName);
  ASSERT_TRUE(VerifyResult(rv, "Getting leafName"));
  ASSERT_TRUE(leafName.Equals(aDirName));

  // Test path parsing
  ASSERT_TRUE(TestInvalidFileName(base, "a/b"));
  ASSERT_TRUE(TestParent(base, subdir));

  // Test nsIFile::Contains
  ASSERT_TRUE(TestContains(base));

  // Test file creation
  ASSERT_TRUE(TestCreate(base, "file.txt", nsIFile::NORMAL_FILE_TYPE, 0600));
  ASSERT_TRUE(TestRemove(base, "file.txt", false));

  // Test directory creation
  ASSERT_TRUE(TestCreate(base, "subdir", nsIFile::DIRECTORY_TYPE, 0700));

  // Test move and copy in the base directory
  ASSERT_TRUE(TestCreate(base, "file.txt", nsIFile::NORMAL_FILE_TYPE, 0600));
  ASSERT_TRUE(TestMove(base, base, "file.txt", "file2.txt"));
  ASSERT_TRUE(TestCopy(base, base, "file2.txt", "file3.txt"));

  // Test moving across directories
  ASSERT_TRUE(TestMove(base, subdir, "file2.txt", "file2.txt"));

  // Test moving across directories and renaming at the same time
  ASSERT_TRUE(TestMove(subdir, base, "file2.txt", "file4.txt"));

  // SECURITY_SID_SIZE() is not defined in mingw headers

  // Test copying across directories
  ASSERT_TRUE(TestCopy(base, subdir, "file4.txt", "file5.txt"));

  if (aTestNormalize) {
    // Run normalization tests while the directory exists
    ASSERT_TRUE(TestNormalizeNativePath(base, subdir));
  }

  // Test recursive directory removal
  ASSERT_TRUE(TestRemove(base, "subdir", true, 2));

  if (aTestCreateUnique) {
    ASSERT_TRUE(TestCreateUnique(base, "foo", nsIFile::NORMAL_FILE_TYPE, 0600));
    ASSERT_TRUE(TestCreateUnique(base, "foo", nsIFile::NORMAL_FILE_TYPE, 0600));
    ASSERT_TRUE(
        TestCreateUnique(base, "bar.xx", nsIFile::DIRECTORY_TYPE, 0700));
    ASSERT_TRUE(
        TestCreateUnique(base, "bar.xx", nsIFile::DIRECTORY_TYPE, 0700));
  }

  ASSERT_TRUE(
      TestDeleteOnClose(base, "file7.txt", PR_RDWR | PR_CREATE_FILE, 0600));

  ASSERT_TRUE(TestDiskSpaceAvailable(base));
  ASSERT_TRUE(TestDiskCapacity(base));

  // Clean up temporary stuff
  rv = base->Remove(true);
  VerifyResult(rv, "Cleaning up temp directory");
}

TEST(TestFile, Unprefixed)
{
  TestWithPrefixWinScope prefixed(false);

  SetupAndTestFunctions(u"mozfiletests"_ns,
                        /* aTestCreateUnique */ true,
                        /* aTestNormalize */ true);
}

// This simulates what QM_NewLocalFile does (NS_NewLocalFiles and then
// SetUseDOSDevicePathSyntax if it's on Windows for NewFile)
TEST(TestFile, PrefixedOnWin)
{
  TestWithPrefixWinScope prefixed(true);

  SetupAndTestFunctions(u"mozfiletests"_ns,
                        /* aTestCreateUnique */ true,
                        /* aTestNormalize */ true);
}

TEST(TestFile, PrefixedOnWin_PathExceedsMaxPath)
{
  TestWithPrefixWinScope prefixed(true);

  // We want to verify if the prefix would allow as to create a file with over
  // 260 char for its path. However, on Windows, the maximum length of filename
  // is 255. Given the base file path and we are going append some other file
  // to the current base file, let's assume the file path will exceed 260 so
  // that we are able to verify if the prefix works or not.
  nsString dirName;
  dirName.AssignLiteral("mozfiletests");
  for (uint32_t i = 255 - dirName.Length(); i > 0; --i) {
    dirName.AppendLiteral("a");
  }

  // Bypass the test for CreateUnique because there is a check for the max
  // length of the root on all platforms.
  SetupAndTestFunctions(dirName, /* aTestCreateUnique */ false,
                        /* aTestNormalize */ true);
}

TEST(TestFile, PrefixedOnWin_ComponentEndsWithPeriod)
{
  TestWithPrefixWinScope prefixed(true);

  // Bypass the normalization for this because it would strip the trailing
  // period.
  SetupAndTestFunctions(u"mozfiletests."_ns,
                        /* aTestCreateUnique */ true,
                        /* aTestNormalize */ false);
}

TEST(TestFile, GetRelativePath)
{
  nsCOMPtr<nsIFile> base;
  ASSERT_NS_SUCCEEDED(
      NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base)));

  nsCOMPtr<nsIFile> child;
  ASSERT_NS_SUCCEEDED(base->Clone(getter_AddRefs(child)));
  ASSERT_NS_SUCCEEDED(child->AppendNative("places.sqlite"_ns));

  nsAutoCString result;

  // Path without \\?\ prefix: child relative to base is just the filename.
  ASSERT_NS_SUCCEEDED(child->GetRelativePath(base, result));
  EXPECT_STREQ(result.get(), "places.sqlite");

}

// Writes aLen bytes of aData to aFile, creating/truncating it.
static bool WriteFileContent(nsIFile* aFile, const char* aData, int32_t aLen) {
  PRFileDesc* fd = nullptr;
  nsresult rv = aFile->OpenNSPRFileDesc(
      PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0600, &fd);
  if (NS_FAILED(rv) || !fd) return false;
  bool ok = aLen == 0 || PR_Write(fd, aData, aLen) == aLen;
  PR_Close(fd);
  return ok;
}

// Reads the full content of aFile into aOut.
static bool ReadFileContent(nsIFile* aFile, nsCString& aOut) {
  PRFileDesc* fd = nullptr;
  nsresult rv = aFile->OpenNSPRFileDesc(PR_RDONLY, 0, &fd);
  if (NS_FAILED(rv) || !fd) return false;
  char buf[4096];
  int32_t n;
  while ((n = PR_Read(fd, buf, sizeof(buf))) > 0) {
    aOut.Append(buf, n);
  }
  PR_Close(fd);
  return n == 0;
}

TEST(TestFile, CopyToContent)
{
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_NS_SUCCEEDED(rv);
  rv = base->AppendNative("copyto_content_test"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  base->Remove(true);
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIFile> subdir = NewFile(base);
  ASSERT_TRUE(subdir);
  rv = subdir->AppendNative("subdir"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  rv = subdir->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);

  // Empty file copy.
  {
    nsCOMPtr<nsIFile> src = NewFile(base);
    ASSERT_TRUE(src);
    ASSERT_NS_SUCCEEDED(src->AppendNative("empty.dat"_ns));
    ASSERT_TRUE(WriteFileContent(src, nullptr, 0));

    ASSERT_NS_SUCCEEDED(src->CopyToNative(base, "empty_copy.dat"_ns));

    nsCOMPtr<nsIFile> dest = NewFile(base);
    ASSERT_TRUE(dest);
    ASSERT_NS_SUCCEEDED(dest->AppendNative("empty_copy.dat"_ns));
    int64_t size = -1;
    ASSERT_NS_SUCCEEDED(dest->GetFileSize(&size));
    EXPECT_EQ(size, 0);
  }

  // Small content copy: exact byte verification, source unchanged.
  {
    constexpr nsLiteralCString kData = "Hello, CopyToNative!"_ns;

    nsCOMPtr<nsIFile> src = NewFile(base);
    ASSERT_TRUE(src);
    ASSERT_NS_SUCCEEDED(src->AppendNative("small_src.dat"_ns));
    ASSERT_TRUE(WriteFileContent(src, kData.get(), kData.Length()));

    ASSERT_NS_SUCCEEDED(src->CopyToNative(subdir, "small_copy.dat"_ns));

    nsCOMPtr<nsIFile> dest = NewFile(subdir);
    ASSERT_TRUE(dest);
    ASSERT_NS_SUCCEEDED(dest->AppendNative("small_copy.dat"_ns));
    nsCString content;
    ASSERT_TRUE(ReadFileContent(dest, content));
    EXPECT_EQ(content, kData);

    bool exists = false;
    ASSERT_NS_SUCCEEDED(src->Exists(&exists));
    EXPECT_TRUE(exists);
    nsCString srcContent;
    ASSERT_TRUE(ReadFileContent(src, srcContent));
    EXPECT_EQ(srcContent, kData);
  }

  // Large file copy (1 MiB): size and full content verified.
  {
    const int32_t kLargeSize = 1024 * 1024;
    nsCString largeData;
    largeData.SetLength(kLargeSize);
    for (int32_t i = 0; i < kLargeSize; ++i) {
      largeData.BeginWriting()[i] = static_cast<char>(i & 0xFF);
    }

    nsCOMPtr<nsIFile> src = NewFile(base);
    ASSERT_TRUE(src);
    ASSERT_NS_SUCCEEDED(src->AppendNative("large_src.dat"_ns));
    ASSERT_TRUE(WriteFileContent(src, largeData.get(), kLargeSize));

    ASSERT_NS_SUCCEEDED(src->CopyToNative(base, "large_copy.dat"_ns));

    nsCOMPtr<nsIFile> dest = NewFile(base);
    ASSERT_TRUE(dest);
    ASSERT_NS_SUCCEEDED(dest->AppendNative("large_copy.dat"_ns));

    int64_t size = 0;
    ASSERT_NS_SUCCEEDED(dest->GetFileSize(&size));
    EXPECT_EQ(size, static_cast<int64_t>(kLargeSize));

    nsCString destContent;
    ASSERT_TRUE(ReadFileContent(dest, destContent));
    ASSERT_EQ(destContent.Length(), static_cast<size_t>(kLargeSize));
    EXPECT_EQ(destContent, largeData);
  }

  // Permission preservation (Unix only, Android's filesystem sandbox does not
  // honour traditional Unix permissions).
  {
    nsCOMPtr<nsIFile> src = NewFile(base);
    ASSERT_TRUE(src);
    ASSERT_NS_SUCCEEDED(src->AppendNative("perms_src.dat"_ns));
    ASSERT_TRUE(WriteFileContent(src, "x", 1));
    ASSERT_NS_SUCCEEDED(src->SetPermissions(0640));

    ASSERT_NS_SUCCEEDED(src->CopyToNative(base, "perms_copy.dat"_ns));

    nsCOMPtr<nsIFile> dest = NewFile(base);
    ASSERT_TRUE(dest);
    ASSERT_NS_SUCCEEDED(dest->AppendNative("perms_copy.dat"_ns));
    uint32_t perms = 0;
    ASSERT_NS_SUCCEEDED(dest->GetPermissions(&perms));
    EXPECT_EQ(perms, static_cast<uint32_t>(0640));
  }

  // Quarantine attribute handling.
  // Check two things:
  // - a source file with quarantine attribute shall result in the attribute
  //   being present in the destination file (i.e. attribute preserved)
  // - a source file without quarantine attribute shall result in the attribute
  //   not being present in the destination file
  {
    constexpr auto kQuarantineAttr = "com.apple.quarantine"_ns;
    // A copy of a file that is quarantined should be quarantined too
    nsCOMPtr<nsIFile> src = NewFile(base);
    ASSERT_TRUE(src);
    ASSERT_NS_SUCCEEDED(src->AppendNative("src.dat"_ns));
    ASSERT_TRUE(WriteFileContent(src, "x", 1));

    // macOS quarantines every newly created file, including the one just
    // written above.
    // Check the previous statement is correct, and if so, use that quarantined
    // file for our test
    nsCOMPtr<nsILocalFileMac> srcMac = do_QueryInterface(src);
    ASSERT_TRUE(srcMac);
    bool hasQuarantineAttr = true;
    ASSERT_NS_SUCCEEDED(srcMac->HasXAttr(kQuarantineAttr, &hasQuarantineAttr));
    ASSERT_TRUE(hasQuarantineAttr);

    // Copy the file
    {
      constexpr auto kDestFilenameQuarantined = "dst_quarantined.dat"_ns;
      ASSERT_NS_SUCCEEDED(src->CopyToNative(base, kDestFilenameQuarantined));
      // Check the destination file is quarantined too (like source file)
      nsCOMPtr<nsIFile> quarantinedCopy = NewFile(base);
      ASSERT_TRUE(quarantinedCopy);
      ASSERT_NS_SUCCEEDED(
          quarantinedCopy->AppendNative(kDestFilenameQuarantined));
      nsCOMPtr<nsILocalFileMac> quarantinedCopyMac =
          do_QueryInterface(quarantinedCopy);
      ASSERT_TRUE(quarantinedCopyMac);
      ASSERT_NS_SUCCEEDED(
          quarantinedCopyMac->HasXAttr(kQuarantineAttr, &hasQuarantineAttr));
      EXPECT_TRUE(hasQuarantineAttr);
    }

    // Remove the quarantine attribute from the source file
    (void)srcMac->DelXAttr(kQuarantineAttr);
    // Check it was actually removed
    ASSERT_NS_SUCCEEDED(srcMac->HasXAttr(kQuarantineAttr, &hasQuarantineAttr));
    EXPECT_FALSE(hasQuarantineAttr);

    // Copy the file again
    {
      constexpr auto kDestFilenameNotQuarantined = "dst_not_quarantined.dat"_ns;
      ASSERT_NS_SUCCEEDED(src->CopyToNative(base, kDestFilenameNotQuarantined));
      // Check the destination file is not quarantined too (like source file)
      nsCOMPtr<nsIFile> notQuarantinedCopy = NewFile(base);
      ASSERT_TRUE(notQuarantinedCopy);
      ASSERT_NS_SUCCEEDED(
          notQuarantinedCopy->AppendNative(kDestFilenameNotQuarantined));
      nsCOMPtr<nsILocalFileMac> notQuarantinedCopyMac =
          do_QueryInterface(notQuarantinedCopy);
      ASSERT_TRUE(notQuarantinedCopyMac);
      ASSERT_NS_SUCCEEDED(
          notQuarantinedCopyMac->HasXAttr(kQuarantineAttr, &hasQuarantineAttr));
      EXPECT_FALSE(hasQuarantineAttr);
    }
  }

  // Directory copy: structure and file contents preserved.
  {
    nsCOMPtr<nsIFile> srcDir = NewFile(base);
    ASSERT_TRUE(srcDir);
    ASSERT_NS_SUCCEEDED(srcDir->AppendNative("dir_src"_ns));
    ASSERT_NS_SUCCEEDED(srcDir->Create(nsIFile::DIRECTORY_TYPE, 0700));

    nsCOMPtr<nsIFile> fileA = NewFile(srcDir);
    ASSERT_TRUE(fileA);
    ASSERT_NS_SUCCEEDED(fileA->AppendNative("a.txt"_ns));
    ASSERT_TRUE(WriteFileContent(fileA, "aaa", 3));

    nsCOMPtr<nsIFile> fileB = NewFile(srcDir);
    ASSERT_TRUE(fileB);
    ASSERT_NS_SUCCEEDED(fileB->AppendNative("b.txt"_ns));
    ASSERT_TRUE(WriteFileContent(fileB, "bbb", 3));

    ASSERT_NS_SUCCEEDED(srcDir->CopyToNative(base, "dir_copy"_ns));

    nsCOMPtr<nsIFile> destDir = NewFile(base);
    ASSERT_TRUE(destDir);
    ASSERT_NS_SUCCEEDED(destDir->AppendNative("dir_copy"_ns));
    bool isDir = false;
    ASSERT_NS_SUCCEEDED(destDir->IsDirectory(&isDir));
    EXPECT_TRUE(isDir);

    nsCOMPtr<nsIFile> destA = NewFile(destDir);
    ASSERT_TRUE(destA);
    ASSERT_NS_SUCCEEDED(destA->AppendNative("a.txt"_ns));
    nsCString contentA;
    ASSERT_TRUE(ReadFileContent(destA, contentA));
    EXPECT_EQ(contentA, "aaa"_ns);

    nsCOMPtr<nsIFile> destB = NewFile(destDir);
    ASSERT_TRUE(destB);
    ASSERT_NS_SUCCEEDED(destB->AppendNative("b.txt"_ns));
    nsCString contentB;
    ASSERT_TRUE(ReadFileContent(destB, contentB));
    EXPECT_EQ(contentB, "bbb"_ns);
  }

  base->Remove(true);
}

// These match the limits in nsLocalFileCommon.cpp's CreateUnique.
static constexpr size_t kMaxUniqueFilenameLength = 250;
static constexpr int32_t kMaxUniquePathLength = PATH_MAX - 6;

TEST(TestFile, CreateUnique_TruncatesLongFilename)
{
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_NS_SUCCEEDED(rv);

  rv = base->AppendNative("cu_trunc_leaf_test"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  base->Remove(true);
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);

  // 260-char root + ".txt" = 264-char leaf, exceeds kMaxUniqueFilenameLength.
  // CreateUnique should truncate the root so the leaf fits.
  {
    nsCOMPtr<nsIFile> file = NewFile(base);
    ASSERT_TRUE(file);

    nsAutoCString name;
    for (int i = 0; i < 260; ++i) {
      name.Append('a');
    }
    name.AppendLiteral(".txt");

    rv = file->AppendNative(name);
    ASSERT_NS_SUCCEEDED(rv);

    rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    ASSERT_NS_SUCCEEDED(rv);

    bool exists;
    rv = file->Exists(&exists);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(exists);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_LE(leafName.Length(), kMaxUniqueFilenameLength);
    EXPECT_TRUE(StringEndsWith(leafName, ".txt"_ns));
  }

  // 260-char leaf with no extension.
  {
    nsCOMPtr<nsIFile> file = NewFile(base);
    ASSERT_TRUE(file);

    nsAutoCString name;
    for (int i = 0; i < 260; ++i) {
      name.Append('b');
    }

    rv = file->AppendNative(name);
    ASSERT_NS_SUCCEEDED(rv);

    rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    ASSERT_NS_SUCCEEDED(rv);

    bool exists;
    rv = file->Exists(&exists);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(exists);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_LE(leafName.Length(), kMaxUniqueFilenameLength);
  }

  base->Remove(true);
}

TEST(TestFile, CreateUnique_TruncatesLongPath)
{
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_NS_SUCCEEDED(rv);

  rv = base->AppendNative("cu_trunc_path_test"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  base->Remove(true);
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);
  rv = base->Normalize();
  ASSERT_NS_SUCCEEDED(rv);

  // Build nested directories until the path is long enough that appending a
  // moderate leaf name (well under kMaxUniqueFilenameLength) pushes the total
  // past kMaxUniquePathLength.
  const int32_t kLeafLen = 104;  // 100-char root + ".dat"
  const int32_t kOvershoot = 50;

  nsCOMPtr<nsIFile> deepDir = NewFile(base);
  ASSERT_TRUE(deepDir);

  nsAutoCString dirPath;
  rv = deepDir->GetNativePath(dirPath);
  ASSERT_NS_SUCCEEDED(rv);

  int32_t targetDirLen = kMaxUniquePathLength - kLeafLen - 1 + kOvershoot;

  while (static_cast<int32_t>(dirPath.Length()) < targetDirLen) {
    int32_t remaining = targetDirLen - dirPath.Length() - 1;
    int32_t nameLen = std::min(remaining, 200);
    if (nameLen < 1) break;

    nsAutoCString dirName;
    for (int32_t i = 0; i < nameLen; ++i) {
      dirName.Append('d');
    }

    rv = deepDir->AppendNative(dirName);
    ASSERT_NS_SUCCEEDED(rv);
    rv = deepDir->Create(nsIFile::DIRECTORY_TYPE, 0700);
    ASSERT_NS_SUCCEEDED(rv);

    rv = deepDir->GetNativePath(dirPath);
    ASSERT_NS_SUCCEEDED(rv);
  }

  // The leaf name is short enough for kMaxUniqueFilenameLength, but the total
  // path would exceed kMaxUniquePathLength.
  {
    nsCOMPtr<nsIFile> file = NewFile(deepDir);
    ASSERT_TRUE(file);

    nsAutoCString name;
    for (int i = 0; i < 100; ++i) {
      name.Append('f');
    }
    name.AppendLiteral(".dat");

    rv = file->AppendNative(name);
    ASSERT_NS_SUCCEEDED(rv);

    nsAutoCString pathBefore;
    rv = file->GetNativePath(pathBefore);
    ASSERT_NS_SUCCEEDED(rv);
    ASSERT_GT(static_cast<int32_t>(pathBefore.Length()), kMaxUniquePathLength)
        << "Test setup: path should exceed the limit before CreateUnique";

    rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    ASSERT_NS_SUCCEEDED(rv);

    bool exists;
    rv = file->Exists(&exists);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(exists);

    nsAutoCString pathAfter;
    rv = file->GetNativePath(pathAfter);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_LE(static_cast<int32_t>(pathAfter.Length()), kMaxUniquePathLength);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(StringEndsWith(leafName, ".dat"_ns));
    EXPECT_LT(leafName.Length(), static_cast<size_t>(kLeafLen));
  }

  base->Remove(true);
}

// The following test was made to quickly assess the performance improvements
// made in the scope of bug 2047435, but there is no need to run it everytime,
// so keep it disabled
TEST(TestFile, DISABLED_CopyToPerf)
{
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_NS_SUCCEEDED(rv);
  rv = base->AppendNative("copyto_perf_test"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  base->Remove(true);
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);

  struct TestCase {
    const char* label;
    int32_t sizeBytes;
    int iterations;
  };
  // clang-format off
  static const TestCase kCases[] = {
      {"4 KB", 4 * 1024, 200},
      {"256 KB", 256 * 1024, 100},
      {"1 MB", 1 * 1024 * 1024, 50},
      {"10 MB", 10 * 1024 * 1024, 10},
      {"50 MB", 50 * 1024 * 1024, 5},
  };
  // clang-format on

  for (const auto& tc : kCases) {
    nsCOMPtr<nsIFile> src = NewFile(base);
    ASSERT_TRUE(src);
    nsAutoCString srcName;
    srcName.AppendLiteral("src_");
    srcName.AppendInt(tc.sizeBytes);
    srcName.AppendLiteral(".dat");
    ASSERT_NS_SUCCEEDED(src->AppendNative(srcName));

    nsCString data;
    data.SetLength(tc.sizeBytes);
    for (int32_t i = 0; i < tc.sizeBytes; ++i) {
      data.BeginWriting()[i] = static_cast<char>(i & 0xFF);
    }
    ASSERT_TRUE(WriteFileContent(src, data.get(), tc.sizeBytes));

    // Warm up once to populate the OS buffer cache before timing.
    ASSERT_NS_SUCCEEDED(src->CopyToNative(base, "warmup.dat"_ns));
    {
      nsCOMPtr<nsIFile> warmup = NewFile(base);
      ASSERT_TRUE(warmup);
      ASSERT_NS_SUCCEEDED(warmup->AppendNative("warmup.dat"_ns));
      warmup->Remove(false);
    }

    mozilla::TimeStamp t0 = mozilla::TimeStamp::Now();
    for (int i = 0; i < tc.iterations; ++i) {
      nsAutoCString destName;
      destName.AppendLiteral("dest_");
      destName.AppendInt(i);
      destName.AppendLiteral(".dat");
      ASSERT_NS_SUCCEEDED(src->CopyToNative(base, destName));
      nsCOMPtr<nsIFile> dest = NewFile(base);
      ASSERT_TRUE(dest);
      ASSERT_NS_SUCCEEDED(dest->AppendNative(destName));
      dest->Remove(false);
    }
    double elapsedMs = (mozilla::TimeStamp::Now() - t0).ToMilliseconds();

    double totalMB = (double)tc.sizeBytes * tc.iterations / (1024.0 * 1024.0);
    double throughputMBs = totalMB / (elapsedMs / 1000.0);
    double avgMs = elapsedMs / tc.iterations;
    printf_stderr("  CopyTo %s: %.2f ms/copy, %.0f MB/s (%d copies)\n",
                  tc.label, avgMs, throughputMBs, tc.iterations);
  }

  base->Remove(true);
}
