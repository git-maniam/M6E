/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/gtest/MozAssertions.h"

#include "nsXREDirProvider.h"
#include "nsAppRunner.h"

#include "nsDirectoryService.h"
#include "nsIDirectoryService.h"
#include "nsTHashMap.h"

#include "nsAppDirectoryServiceDefs.h"
#include "prenv.h"
#include "nsIFile.h"
#include "SpecialSystemDirectory.h"
#include "nsPrintfCString.h"

#  include <unistd.h>
#  include <sys/stat.h>

using namespace mozilla;


#include "mozilla/XREAppData.h"

#if defined(MOZ_THUNDERBIRD)
#  define EXPECTED_APP_NAME_CASED "Thunderbird"
#  define EXPECTED_APP_NAME_NONCASED "thunderbird"
#  define EXPECTED_VENDOR_APP_NAME_NONCASED "thunderbird"
#else
#  define EXPECTED_APP_NAME_CASED "Firefox"
#  define EXPECTED_APP_NAME_NONCASED "firefox"
#  define EXPECTED_VENDOR_APP_NAME_NONCASED "mozilla/firefox"
#endif

class BaseXREAppDir : public ::testing::Test {
 protected:
  void SetUp() override {
    // Get a Clone() of gDataDirProfileLocal / gDataDirProfile
    // They will get restored in TearDown()
    nsresult rv = nsXREDirProvider::ClearUserDataProfileDirectoryFromGTest(
        getter_AddRefs(mDataDirProfileLocal), getter_AddRefs(mDataDirProfile));
    EXPECT_NS_SUCCEEDED(rv);

// There is no need to mock on macOS because nsXREDirProvider relies on macOS
// level APIs, see below

    mFakeAppData = XREAppData();
    mFakeAppData.profile = "fooprofile";
    mFakeAppData.name = EXPECTED_APP_NAME_CASED;
    mFakeAppData.vendor = "Mozilla";


    rv = FindFolder(kApplicationSupportFolderType, mAppSupport);
    EXPECT_NS_SUCCEEDED(rv);
    rv = FindFolder(kCachedDataFolderType, mAppCache);
    EXPECT_NS_SUCCEEDED(rv);
    rv = FindFolder(kDomainLibraryFolderType, mLibrary);
    EXPECT_NS_SUCCEEDED(rv);
    rv = FindFolder(kCurrentUserFolderType, mHomeRoot);
    EXPECT_NS_SUCCEEDED(rv);
  }


  nsresult FindFolder(OSType folderType, nsACString& retval) {
    FSRef fsRef;
    nsCOMPtr<nsILocalFileMac> dirFileMac;
    OSErr err =
        ::FSFindFolder(kUserDomain, folderType, kDontCreateFolder, &fsRef);
    if (err != noErr) {
      return NS_ERROR_FAILURE;
    }

    nsresult rv = NS_NewLocalFileWithFSRef(&fsRef, getter_AddRefs(dirFileMac));
    EXPECT_NS_SUCCEEDED(rv);
    retval = dirFileMac->NativePath().get();

    return NS_OK;
  }

  void TearDown() override {
    nsresult rv = nsXREDirProvider::RestoreUserDataProfileDirectoryFromGTest(
        mDataDirProfileLocal, mDataDirProfile);
    EXPECT_NS_SUCCEEDED(rv);


    if (mOriginalAppData) {
      SwitchFakeAppDataOff();
    }

#if defined(MOZ_THUNDERBIRD)
    EXPECT_STREQ(gAppData->profile, EXPECTED_APP_NAME_NONCASED);
#else
    EXPECT_STREQ(gAppData->profile, nullptr);
#endif
    EXPECT_STREQ(gAppData->name, EXPECTED_APP_NAME_CASED);
    EXPECT_STREQ(gAppData->vendor, "Mozilla");
  }

  nsCString GetPath(nsCOMPtr<nsIFile> aFile) {
    return aFile->NativePath();
  }

  void SwitchFakeAppDataOn() {
    EXPECT_EQ(mOriginalAppData, nullptr);
    mOriginalAppData = gAppData;
    gAppData = &mFakeAppData;
    EXPECT_STREQ(gAppData->profile, "fooprofile");
  }

  void SwitchFakeAppDataOff() {
    EXPECT_NE(mOriginalAppData, nullptr);
    gAppData = mOriginalAppData;
    mOriginalAppData = nullptr;
#if defined(MOZ_THUNDERBIRD)
    // Thunderbird always sets MOZ_APP_PROFILE=thunderbird
    EXPECT_STREQ(gAppData->profile, EXPECTED_APP_NAME_NONCASED);
#else
    EXPECT_STREQ(gAppData->profile, nullptr);
#endif
  }

  // Used by
  //  - XRE_USER_APP_DATA_DIR
  //  - NS_APP_APPLICATION_REGISTRY_DIR
  //  - NS_APP_APPLICATION_REGISTRY_FILE
  nsCString GetUserAppDataDirectory() {
    nsCOMPtr<nsIFile> localDir;
    nsresult rv = NS_OK;
    nsXREDirProvider::GetUserAppDataDirectory(getter_AddRefs(localDir));
    EXPECT_NS_SUCCEEDED(rv);
    return GetPath(localDir);
  }

  nsCString GetUserProfilesRootDir() {
    RefPtr<nsXREDirProvider> ds = nsXREDirProvider::GetSingleton();

    nsCOMPtr<nsIFile> localDir;
    nsresult rv = ds->GetUserProfilesRootDir(getter_AddRefs(localDir));
    EXPECT_NS_SUCCEEDED(rv);
    return GetPath(localDir);
  }

  nsCString GetUserProfilesLocalDir() {
    RefPtr<nsXREDirProvider> ds = nsXREDirProvider::GetSingleton();

    nsCOMPtr<nsIFile> localDir;
    nsresult rv = ds->GetUserProfilesLocalDir(getter_AddRefs(localDir));
    EXPECT_NS_SUCCEEDED(rv);
    return GetPath(localDir);
  }

  nsCString GetUserLocalDataDirectory() {
    nsCOMPtr<nsIFile> localDir;
    nsresult rv =
        nsXREDirProvider::GetUserLocalDataDirectory(getter_AddRefs(localDir));
    EXPECT_NS_SUCCEEDED(rv);
    return GetPath(localDir);
  }

  nsCString GetFromXREDirProvider(const char* aName) {
    RefPtr<nsXREDirProvider> ds = nsXREDirProvider::GetSingleton();

    bool dummy;
    nsCOMPtr<nsIFile> localDir;
    nsresult rv = ds->GetFile(aName, &dummy, getter_AddRefs(localDir));
    EXPECT_NS_SUCCEEDED(rv);
    return GetPath(localDir);
  }

  nsCString GetFromDirectoryService(const char* aName) {
    nsCOMPtr<nsIFile> localDir;
    nsDirectoryService::gService->Undefine(aName);
    nsresult rv = nsDirectoryService::gService->Get(aName, NS_GET_IID(nsIFile),
                                                    getter_AddRefs(localDir));
    EXPECT_NS_SUCCEEDED(rv);
    return GetPath(localDir);
  }

  // Create a temp dir and set HOME to it.  Upon successful completion, return
  // the string with the path of the homedir.

  nsTHashMap<nsCString, nsCString> mRestoreEnv;

  static const char* GetEnv(const char* aName) { return PR_GetEnv(aName); }

  void SaveEnv(const char* aName) {
    mRestoreEnv.LookupOrInsertWith(nsDependentCString(aName), [aName] {
      const char* value = GetEnv(aName);
      return value ? nsCString(value) : ""_ns;
    });
  }

  void SetEnv(const char* aName, const char* aValue) {
    SaveEnv(aName);
    auto envValue = Smprintf("%s=%s", aName, aValue);
    ASSERT_EQ(0, PR_SetEnv(envValue.release()));
    ASSERT_STREQ(aValue, GetEnv(aName));
  }

  void UnsetEnv(const char* aName) { SetEnv(aName, ""); }

  void MkHomeSubdir(const char* aSubdir, nsCString& subdir) {
  }


  nsCString mAppSupport;
  nsCString mAppCache;
  nsCString mLibrary;
  nsCString mHomeRoot;


  const XREAppData* mOriginalAppData = nullptr;
  nsCOMPtr<nsIFile> mDataDirProfileLocal;
  nsCOMPtr<nsIFile> mDataDirProfile;
  XREAppData mFakeAppData{};
};

class ExistentLegacyXREAppDir_Generic : public BaseXREAppDir {
 protected:
  void SetUp() override {
    BaseXREAppDir::SetUp();
#if defined(MOZ_THUNDERBIRD)
    MkHomeSubdir("." EXPECTED_APP_NAME_NONCASED, mMozDir);
#else
    MkHomeSubdir(".mozilla", mMozDir);
    MkHomeSubdir(".mozilla/" EXPECTED_APP_NAME_NONCASED, mMozDir);
#endif
  }
  nsCString mMozDir;
};

class ExistentLegacyXREAppDir_Generic_AppDataProfile
    : public ExistentLegacyXREAppDir_Generic {
 protected:
  void SetUp() override {
    ExistentLegacyXREAppDir_Generic::SetUp();
    SwitchFakeAppDataOn();
#if defined(MOZ_THUNDERBIRD)
    nsAutoCString profile(".");
    profile.Append(gAppData->profile);
    MkHomeSubdir(profile.get(), mMozDir);
#endif
  }
};

#if defined(MOZ_THUNDERBIRD)
class ExistentLegacyXREAppDir_Generic_TBirdAppDataProfile
    : public ExistentLegacyXREAppDir_Generic {
 protected:
  void SetUp() override {
    ExistentLegacyXREAppDir_Generic::SetUp();
    nsAutoCString profile(".");
    profile.Append(gAppData->profile);
    MkHomeSubdir(profile.get(), mMozDir);
  }
};
#endif

class NonExistentLegacyXREAppDir_Generic : public BaseXREAppDir {};

class NonExistentLegacyXREAppDir_Generic_AppDataProfile : public BaseXREAppDir {
 protected:
  void SetUp() override {
    BaseXREAppDir::SetUp();
    SwitchFakeAppDataOn();
  }
};

#if defined(MOZ_THUNDERBIRD)
class NonExistentLegacyXREAppDir_Generic_TBirdAppDataProfile
    : public BaseXREAppDir {};
#endif

/*
 * Tests the legacy behavior when there is
 *  - no MOZ_LEGACY_HOME
 *  - existing $HOME/.mozilla
 */
class ExistentLegacyXREAppDir_NoEnv : public ExistentLegacyXREAppDir_Generic {};

class ExistentLegacyXREAppDir_NoEnv_AppDataProfile
    : public ExistentLegacyXREAppDir_Generic_AppDataProfile {};

class NonExistentLegacyXREAppDir_NoEnv
    : public NonExistentLegacyXREAppDir_Generic {};

class NonExistentLegacyXREAppDir_NoEnv_AppDataProfile
    : public NonExistentLegacyXREAppDir_Generic_AppDataProfile {};

#if defined(MOZ_THUNDERBIRD)
class NonExistentLegacyXREAppDir_NoEnv_TBirdAppDataProfile
    : public NonExistentLegacyXREAppDir_Generic_TBirdAppDataProfile {};
#endif

/*
 * Tests the legacy behavior when there is
 *  - MOZ_LEGACY_HOME = 0
 *  - existing $HOME/.mozilla
 */
class ExistentLegacyXREAppDir_BadEnv : public ExistentLegacyXREAppDir_Generic {
 protected:
  void SetUp() override {
    ExistentLegacyXREAppDir_Generic::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "0");
  }
};

class ExistentLegacyXREAppDir_BadEnv_AppDataProfile
    : public ExistentLegacyXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    ExistentLegacyXREAppDir_Generic_AppDataProfile::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "0");
  }
};

class NonExistentLegacyXREAppDir_BadEnv
    : public NonExistentLegacyXREAppDir_Generic {
 protected:
  void SetUp() override {
    NonExistentLegacyXREAppDir_Generic::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "0");
  }
};

class NonExistentLegacyXREAppDir_BadEnv_AppDataProfile
    : public NonExistentLegacyXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    NonExistentLegacyXREAppDir_Generic_AppDataProfile::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "0");
  }
};

#if defined(MOZ_THUNDERBIRD)
class NonExistentLegacyXREAppDir_BadEnv_TBirdAppDataProfile
    : public NonExistentLegacyXREAppDir_Generic_TBirdAppDataProfile {};
#endif

/*
 * Tests the legacy behavior when there is
 *  - MOZ_LEGACY_HOME = 1
 *  - existing $HOME/.mozilla
 */
class ExistentLegacyXREAppDir_GoodEnv : public ExistentLegacyXREAppDir_Generic {
 protected:
  void SetUp() override {
    ExistentLegacyXREAppDir_Generic::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "1");
  }
};

class ExistentLegacyXREAppDir_GoodEnv_AppDataProfile
    : public ExistentLegacyXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    ExistentLegacyXREAppDir_Generic_AppDataProfile::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "1");
  }
};

class NonExistentLegacyXREAppDir_GoodEnv
    : public NonExistentLegacyXREAppDir_Generic {
 protected:
  void SetUp() override {
    NonExistentLegacyXREAppDir_Generic::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "1");
  }
};

class NonExistentLegacyXREAppDir_GoodEnv_AppDataProfile
    : public NonExistentLegacyXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    NonExistentLegacyXREAppDir_Generic_AppDataProfile::SetUp();
    SetEnv("MOZ_LEGACY_HOME", "1");
  }
};

class XDGXREAppDir_Generic : public BaseXREAppDir {};

class XDGXREAppDir_Generic_AppDataProfile : public XDGXREAppDir_Generic {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic::SetUp();
    SwitchFakeAppDataOn();
  }
};

#if defined(MOZ_THUNDERBIRD)
class XDGXREAppDir_Generic_TBirdAppDataProfile : public XDGXREAppDir_Generic {
 protected:
  void SetUp() override { XDGXREAppDir_Generic::SetUp(); }
};
#endif

/*
 * Tests the new default behavior when there is:
 *  - no MOZ_LEGACY_HOME
 *  - no XDG_CONFIG_HOME
 *  - no existing $HOME/.mozilla
 * => $HOME/.config/mozilla/
 */
class XDGXREAppDir_NoEnv : public XDGXREAppDir_Generic {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic::SetUp();
    UnsetEnv("MOZ_LEGACY_HOME");
    UnsetEnv("XDG_CONFIG_HOME");
  }
};

class XDGXREAppDir_NoEnv_AppDataProfile
    : public XDGXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic_AppDataProfile::SetUp();
    UnsetEnv("MOZ_LEGACY_HOME");
    UnsetEnv("XDG_CONFIG_HOME");
  }
};

#if defined(MOZ_THUNDERBIRD)
class XDGXREAppDir_NoEnv_TBirdAppDataProfile
    : public XDGXREAppDir_Generic_TBirdAppDataProfile {};
#endif

/*
 * Tests the new default behavior when there is:
 *  - no MOZ_LEGACY_HOME
 *  - XDG_CONFIG_HOME
 *  - no existing $HOME/.mozilla
 * => $XDG_CONFIG_HOME/mozilla
 */
class XDGXREAppDir_Env : public XDGXREAppDir_Generic {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic::SetUp();
    // mXdgDir will be cleaned when we clean the temp home we created
    MkHomeSubdir(".xdgConfigDir", mXdgDir);
    SetEnv("XDG_CONFIG_HOME", mXdgDir.get());
  }

  nsCString mXdgDir;
};

class XDGXREAppDir_Env_AppDataProfile
    : public XDGXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic_AppDataProfile::SetUp();
    // mXdgDir will be cleaned when we clean the temp home we created
    MkHomeSubdir(".xdgConfigDir", mXdgDir);
    SetEnv("XDG_CONFIG_HOME", mXdgDir.get());
  }

  nsCString mXdgDir;
};

#if defined(MOZ_THUNDERBIRD)
class XDGXREAppDir_Env_TBirdAppDataProfile
    : public XDGXREAppDir_Generic_TBirdAppDataProfile {
 protected:
  void SetUp() {
    XDGXREAppDir_Generic_TBirdAppDataProfile::SetUp();
    // mXdgDir will be cleaned when we clean the temp home we created
    MkHomeSubdir(".xdgConfigDir", mXdgDir);
    SetEnv("XDG_CONFIG_HOME", mXdgDir.get());
  }
  nsCString mXdgDir;
};
#endif

/*
 * Tests the new default behavior when there is:
 *  - invalid XDG_CONFIG_HOME
 * => $HOME/.config/mozilla
 */
class XDGXREAppDir_InvalidEnv : public XDGXREAppDir_Generic {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic::SetUp();
    mXdgDir += "$HOME/invalid-xdg-dir/";
    SetEnv("XDG_CONFIG_HOME", mXdgDir.get());
  }

  nsCString mXdgDir;
};

class XDGXREAppDir_InvalidEnv_AppDataProfile
    : public XDGXREAppDir_Generic_AppDataProfile {
 protected:
  void SetUp() override {
    XDGXREAppDir_Generic_AppDataProfile::SetUp();
    mXdgDir += "$HOME/invalid-xdg-dir/";
    SetEnv("XDG_CONFIG_HOME", mXdgDir.get());
  }

  nsCString mXdgDir;
};

#if defined(MOZ_THUNDERBIRD)
class XDGXREAppDir_InvalidEnv_TBirdAppDataProfile
    : public XDGXREAppDir_Generic_TBirdAppDataProfile {};
#endif

class CacheXREAppDir_Env : public BaseXREAppDir {
 protected:
  void SetUp() override {
    BaseXREAppDir::SetUp();
    // mXdgCache will be cleaned when we clean the temp home we created
    MkHomeSubdir(".xdgCacheDir", mXdgCache);
    SetEnv("XDG_CACHE_HOME", mXdgCache.get());
  }

  nsCString mXdgCache;
};

class CacheXREAppDir_NoEnv : public BaseXREAppDir {
 protected:
  void SetUp() override {
    BaseXREAppDir::SetUp();
    UnsetEnv("XDG_CACHE_HOME");
  }
};

// GetUserAppDataDirectory

// Check if '$HOME/.mozilla' is used when it exists.
TEST_F(ExistentLegacyXREAppDir_NoEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

// Check if '$HOME/.mozilla' is used when it exists and MOZ_LEGACY_HOME != 1
TEST_F(ExistentLegacyXREAppDir_BadEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

// Check if '$HOME/.mozilla' is not used when the env variable MOZ_LEGACY_HOME
// is set to 0.
TEST_F(NonExistentLegacyXREAppDir_BadEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

// Check if '$HOME/.mozilla' is used when it does not exists and MOZ_LEGACY_HOME
// = 1
TEST_F(NonExistentLegacyXREAppDir_GoodEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

// Check if '$HOME/.config/mozilla' is used if $HOME/.mozilla does not exist
// and the env variable XDG_CONFIG_HOME is not set.
TEST_F(XDGXREAppDir_NoEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

// Check if '$HOME/.config/mozilla' is used if $HOME/.mozilla does not exist
// and the env variable XDG_CONFIG_HOME is invalid.
TEST_F(XDGXREAppDir_InvalidEnv, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

TEST_F(XDGXREAppDir_Env, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}

// XREUserNativeManifests

TEST_F(ExistentLegacyXREAppDir_NoEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(ExistentLegacyXREAppDir_BadEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(NonExistentLegacyXREAppDir_BadEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(NonExistentLegacyXREAppDir_GoodEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(XDGXREAppDir_NoEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(XDGXREAppDir_InvalidEnv, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(XDGXREAppDir_Env, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

// XREUserSysExtensionDir

TEST_F(ExistentLegacyXREAppDir_NoEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(ExistentLegacyXREAppDir_BadEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_BadEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_GoodEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(XDGXREAppDir_NoEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(XDGXREAppDir_InvalidEnv, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(XDGXREAppDir_Env, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

// GetUserProfilesRootDir

TEST_F(XDGXREAppDir_NoEnv, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}

TEST_F(XDGXREAppDir_InvalidEnv, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}

TEST_F(XDGXREAppDir_Env, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}

// GetDefaultUserProfileRoot

// Check if '$HOME/.mozilla' is used when it exists.
TEST_F(ExistentLegacyXREAppDir_NoEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

// Check if '$HOME/.mozilla' is used when it exists and MOZ_LEGACY_HOME != 1
TEST_F(ExistentLegacyXREAppDir_BadEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

// Check if '$HOME/.mozilla' is not used when the env variable MOZ_LEGACY_HOME
// is set to 0.
TEST_F(NonExistentLegacyXREAppDir_BadEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

// Check if '$HOME/.mozilla' is used when it does not exists and MOZ_LEGACY_HOME
// = 1
TEST_F(NonExistentLegacyXREAppDir_GoodEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

// Check if '$HOME/.config/mozilla' is used if $HOME/.mozilla does not exist
// and the env variable XDG_CONFIG_HOME is not set.
TEST_F(XDGXREAppDir_NoEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

// Check if '$HOME/.config/mozilla' is used if $HOME/.mozilla does not exist
// and the env variable XDG_CONFIG_HOME is invalid.
TEST_F(XDGXREAppDir_InvalidEnv, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(XDGXREAppDir_Env, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

// GetTempDefaultUserProfileRoot

// Check if '$HOME/.mozilla' is used when it exists.
TEST_F(ExistentLegacyXREAppDir_NoEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

// Check if '$HOME/.mozilla' is used when it exists and MOZ_LEGACY_HOME != 1
TEST_F(ExistentLegacyXREAppDir_BadEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

// Check if '$HOME/.mozilla' is not used when the env variable MOZ_LEGACY_HOME
// is set to 0.
TEST_F(NonExistentLegacyXREAppDir_BadEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

// Check if '$HOME/.mozilla' is used when it does not exists and MOZ_LEGACY_HOME
// = 1
TEST_F(NonExistentLegacyXREAppDir_GoodEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

// Check if '$HOME/.config/mozilla' is used if $HOME/.mozilla does not exist
// and the env variable XDG_CONFIG_HOME is not set.
TEST_F(XDGXREAppDir_NoEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

// Check if '$HOME/.config/mozilla' is used if $HOME/.mozilla does not exist
// and the env variable XDG_CONFIG_HOME is invalid.
TEST_F(XDGXREAppDir_InvalidEnv, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(XDGXREAppDir_Env, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

/// AppDataProfile variants

TEST_F(ExistentLegacyXREAppDir_NoEnv_AppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

TEST_F(ExistentLegacyXREAppDir_BadEnv_AppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(NonExistentLegacyXREAppDir_NoEnv_TBirdAppDataProfile,
       GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}
#endif

TEST_F(NonExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

TEST_F(NonExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

TEST_F(XDGXREAppDir_NoEnv_AppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_NoEnv_TBirdAppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}
#endif

TEST_F(XDGXREAppDir_InvalidEnv_AppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_InvalidEnv_TBirdAppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}
#endif

TEST_F(XDGXREAppDir_Env_AppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile"_ns)
          ,
      GetUserAppDataDirectory());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_Env_TBirdAppDataProfile, GetUserAppDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserAppDataDirectory());
}
#endif

// XREUserNativeManifests

TEST_F(ExistentLegacyXREAppDir_NoEnv_AppDataProfile, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(ExistentLegacyXREAppDir_BadEnv_AppDataProfile, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv_AppDataProfile, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(NonExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(NonExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(XDGXREAppDir_NoEnv_AppDataProfile, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(XDGXREAppDir_InvalidEnv_AppDataProfile, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

TEST_F(XDGXREAppDir_Env_AppDataProfile, XREUserNativeManifests) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_NATIVE_MANIFESTS));
}

// XREUserSysExtensionDir

TEST_F(ExistentLegacyXREAppDir_NoEnv_AppDataProfile, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(ExistentLegacyXREAppDir_BadEnv_AppDataProfile, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv_AppDataProfile, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(XDGXREAppDir_NoEnv_AppDataProfile, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(XDGXREAppDir_InvalidEnv_AppDataProfile, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

TEST_F(XDGXREAppDir_Env_AppDataProfile, XREUserSysExtensionDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/Mozilla/Extensions"_ns)
          ,
      GetFromXREDirProvider(XRE_USER_SYS_EXTENSION_DIR));
}

// GetUserProfilesRootDir

TEST_F(XDGXREAppDir_NoEnv_AppDataProfile, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_NoEnv_TBirdAppDataProfile, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}
#endif

TEST_F(XDGXREAppDir_InvalidEnv_AppDataProfile, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_InvalidEnv_TBirdAppDataProfile, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}
#endif

TEST_F(XDGXREAppDir_Env_AppDataProfile, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/fooprofile/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_Env_TBirdAppDataProfile, GetUserProfilesRootDir) {
  ASSERT_EQ(
      nsCString(mAppSupport + "/"_ns EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesRootDir());
}
#endif

// GetDefaultUserProfileRoot

TEST_F(ExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(ExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(NonExistentLegacyXREAppDir_NoEnv_TBirdAppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}
#endif

TEST_F(NonExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(NonExistentLegacyXREAppDir_BadEnv_TBirdAppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}
#endif

TEST_F(NonExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

TEST_F(XDGXREAppDir_NoEnv_AppDataProfile, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_NoEnv_TBirdAppDataProfile, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}
#endif

TEST_F(XDGXREAppDir_InvalidEnv_AppDataProfile, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_InvalidEnv_TBirdAppDataProfile, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}
#endif

TEST_F(XDGXREAppDir_Env_AppDataProfile, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_Env_TBirdAppDataProfile, GetDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mLibrary + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_ROOT_DIR));
}
#endif

// GetTempDefaultUserProfileRoot

TEST_F(ExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(ExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(ExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(NonExistentLegacyXREAppDir_NoEnv_AppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(NonExistentLegacyXREAppDir_NoEnv_TBirdAppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}
#endif

TEST_F(NonExistentLegacyXREAppDir_BadEnv_AppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(NonExistentLegacyXREAppDir_BadEnv_TBirdAppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}
#endif

TEST_F(NonExistentLegacyXREAppDir_GoodEnv_AppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

TEST_F(XDGXREAppDir_NoEnv_AppDataProfile, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_NoEnv_TBirdAppDataProfile, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}
#endif

TEST_F(XDGXREAppDir_InvalidEnv_AppDataProfile, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_InvalidEnv_TBirdAppDataProfile,
       GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}
#endif

TEST_F(XDGXREAppDir_Env_AppDataProfile, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}

#if defined(MOZ_THUNDERBIRD)
TEST_F(XDGXREAppDir_Env_TBirdAppDataProfile, GetTempDefaultUserProfileRoot) {
  ASSERT_EQ(
      nsCString(mAppCache + "/Mozilla/Profiles"_ns)
          ,
      GetFromDirectoryService(NS_APP_USER_PROFILES_LOCAL_ROOT_DIR));
}
#endif

/// END AppDataProfile variants

/// Tests for XDG_CACHE_HOME

// GetUserLocalDataDirectory

// Check if '$HOME/.cache/mozilla' is used when '$XDG_CACHE_HOME' is not set.
TEST_F(CacheXREAppDir_NoEnv, GetUserLocalDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppCache + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserLocalDataDirectory());
}

// Check if '$XDG_CACHE_HOME/mozilla' is used when '$XDG_CACHE_HOME' is set.
TEST_F(CacheXREAppDir_Env, GetUserLocalDataDirectory) {
  ASSERT_EQ(
      nsCString(mAppCache + "/"_ns EXPECTED_APP_NAME_CASED)
          ,
      GetUserLocalDataDirectory());
}

// GetUserProfilesLocalDir

TEST_F(CacheXREAppDir_NoEnv, GetUserProfilesLocalDir) {
  ASSERT_EQ(
      nsCString(mAppCache + "/" EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesLocalDir());
}

TEST_F(CacheXREAppDir_Env, GetUserProfilesLocalDir) {
  ASSERT_EQ(
      nsCString(mAppCache + "/" EXPECTED_APP_NAME_CASED "/Profiles"_ns)
          ,
      GetUserProfilesLocalDir());
}
