/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_COMMON_TEST_SANDBOXTESTINGCHILDTESTS_H_
#define SECURITY_SANDBOX_COMMON_TEST_SANDBOXTESTINGCHILDTESTS_H_

#include "SandboxTestingChild.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "nsXULAppAPI.h"

#  include <fcntl.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>

#  include "mozilla/Sandbox.h"
#  if defined(__SSE2__) || defined(_M_X64) || \
      (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    include "emmintrin.h"
#  endif
#  include <AudioToolbox/AudioToolbox.h>
#  include <CoreFoundation/CoreFoundation.h>
#  include <CoreGraphics/CoreGraphics.h>
#  include <dirent.h>
#  include <fcntl.h>
#  include <limits.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/sysctl.h>
#  include <unistd.h>
namespace ApplicationServices {
#  include <ApplicationServices/ApplicationServices.h>
}
extern "C" int sandbox_check(pid_t pid, const char* operation, int type, ...);



constexpr bool kIsDebug =
#ifdef DEBUG
    true;
#else
    false;
#endif

namespace mozilla {


// Tests that apply to every process type (more or less)
static void RunGenericTests(SandboxTestingChild* child, bool aIsGMP = false) {
}


/*
 * Test if this process can launch another process with posix_spawnp,
 * exec, and LSOpenCFURLRef. All launches are expected to fail. Only the
 * failure itself is checked, not the error that is returned: the error
 * depends on which sandbox rule stops the launch first, and that differs
 * between process types, macOS versions and CPU architectures.
 */
void RunMacTestLaunchProcess(SandboxTestingChild* child) {
  // Test that posix_spawnp fails
  char* argv[2];
  argv[0] = const_cast<char*>("bash");
  argv[1] = NULL;
  int rv = posix_spawnp(NULL, "/bin/bash", NULL, NULL, argv, NULL);
  nsPrintfCString posixSpawnMessage(
      "posix_spawnp returned %d, expected a non-zero error", rv);
  child->SendReportTestResults("posix_spawnp test"_ns, rv != 0,
                               posixSpawnMessage);

  // Test that exec fails
  child->ErrnoTest("execv /bin/bash test"_ns, false, [&] {
    char* argvp = NULL;
    return execv("/bin/bash", &argvp);
  });

  // Test that launching an application using LSOpenCFURLRef fails
  char* uri = const_cast<char*>("/System/Applications/Utilities/Console.app");
  CFStringRef filePath = ::CFStringCreateWithCString(kCFAllocatorDefault, uri,
                                                     kCFStringEncodingUTF8);
  CFURLRef urlRef = ::CFURLCreateWithFileSystemPath(
      kCFAllocatorDefault, filePath, kCFURLPOSIXPathStyle, false);
  ::CFRelease(filePath);
  if (!urlRef) {
    child->SendReportTestResults("LSOpenCFURLRef"_ns, false,
                                 "CFURLCreateWithFileSystemPath failed"_ns);
    return;
  }

  OSStatus status = ApplicationServices::LSOpenCFURLRef(urlRef, NULL);
  ::CFRelease(urlRef);
  nsPrintfCString lsMessage(
      "LSOpenCFURLRef returned %d, expected a non-zero error", status);
  child->SendReportTestResults("LSOpenCFURLRef"_ns, status != noErr, lsMessage);
}

/*
 * Test if this process can connect to the macOS window server.
 * When |aShouldHaveAccess| is true, the test passes if access is __permitted__.
 * When |aShouldHaveAccess| is false, the test passes if access is __blocked__.
 */
void RunMacTestWindowServer(SandboxTestingChild* child,
                            bool aShouldHaveAccess = false) {
  // CGSessionCopyCurrentDictionary() returns NULL when a
  // connection to the window server is not available.
  CFDictionaryRef windowServerDict = CGSessionCopyCurrentDictionary();
  bool gotWindowServerDetails = (windowServerDict != nullptr);
  bool testPassed = (gotWindowServerDetails == aShouldHaveAccess);
  child->SendReportTestResults(
      "CGSessionCopyCurrentDictionary"_ns, testPassed,
      gotWindowServerDetails
          ? "dictionary returned, access is permitted"_ns
          : "no dictionary returned, access appears blocked"_ns);
  if (windowServerDict != nullptr) {
    CFRelease(windowServerDict);
  }
}

/*
 * Test if this process can get access to audio components on macOS.
 * When |aShouldHaveAccess| is true, the test passes if access is __permitted__.
 * When |aShouldHaveAccess| is false, the test passes if access is __blocked__.
 */
void RunMacTestAudioAPI(SandboxTestingChild* child,
                        bool aShouldHaveAccess = false) {
  AudioStreamBasicDescription inputFormat;
  inputFormat.mFormatID = kAudioFormatMPEG4AAC;
  inputFormat.mSampleRate = 48000.0;
  inputFormat.mChannelsPerFrame = 2;
  inputFormat.mBitsPerChannel = 0;
  inputFormat.mFormatFlags = 0;
  inputFormat.mFramesPerPacket = 1024;
  inputFormat.mBytesPerPacket = 0;

  UInt32 inputFormatSize = sizeof(inputFormat);
  OSStatus status = AudioFormatGetProperty(
      kAudioFormatProperty_FormatInfo, 0, NULL, &inputFormatSize, &inputFormat);

  bool gotAudioFormat = (status == 0);
  bool testPassed = (gotAudioFormat == aShouldHaveAccess);
  child->SendReportTestResults(
      "AudioFormatGetProperty"_ns, testPassed,
      gotAudioFormat ? "got audio format, access is permitted"_ns
                     : "no audio format, access appears blocked"_ns);
}


void RunTestsContent(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

  struct stat st;
  static const char kAllowedPath[] = "/usr/lib";

  child->ErrnoTest("fstatat_as_stat"_ns, true,
                   [&] { return fstatat(AT_FDCWD, kAllowedPath, &st, 0); });
  child->ErrnoTest("fstatat_as_lstat"_ns, true, [&] {
    return fstatat(AT_FDCWD, kAllowedPath, &st, AT_SYMLINK_NOFOLLOW);
  });


  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(child, true);

}

void RunTestsSocket(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

  child->ErrnoTest("getaddrinfo"_ns, true, [&] {
    struct addrinfo* res;
    int rv = getaddrinfo("localhost", nullptr, nullptr, &res);
    if (res != nullptr) {
      freeaddrinfo(res);
    }
    return rv;
  });

}

void RunTestsRDD(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(child, true);
}

void RunTestsGMPlugin(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child, /* aIsGMP = */ true);

  RunMacTestLaunchProcess(child);
  /* The Mac GMP process requires access to the window server */
  RunMacTestWindowServer(child, true /* aShouldHaveAccess */);
  RunMacTestAudioAPI(child);
}

void RunTestsGenericUtility(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(child);
}

void RunTestsUtilityMediaService(SandboxTestingChild* child,
                                 ipc::SandboxingKind aSandbox) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(
      child,
      aSandbox == ipc::SandboxingKind::UTILITY_AUDIO_DECODING_APPLE_MEDIA);
}

void RunTestsGPU(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);


  // Check if the GPU process sandbox has been started
  bool isSandboxStarted = sandbox_check(getpid(), NULL, 0) == 1;
  nsCString gpuSandboxCheckMessage;
  if (isSandboxStarted) {
    gpuSandboxCheckMessage.AppendLiteral(
        "sandbox_check() indicates GPU process sandbox is running");
  } else {
    gpuSandboxCheckMessage.AppendLiteral(
        "sandbox_check() indicates GPU process sandbox is not running");
  }
  child->SendReportTestResults("sandbox_check()"_ns, isSandboxStarted,
                               gpuSandboxCheckMessage);

  // Home directory tests
  const char* home = getenv("HOME");
  if (home) {
    // Test write to home directory
    nsCString testFile(home);
    testFile.Append("/gpu_sbox_writetest.tmp");

    child->ErrnoTest("write denied ($HOME)"_ns, false,
                     [p = std::string(testFile.get())] {
                       int fd = open(p.c_str(), O_CREAT | O_WRONLY, 0600);
                       if (fd >= 0) {
                         close(fd);
                       }
                       return fd;
                     });

    // Test reading from home directory - file already created by parent process
    nsCString testReadFile(home);
    testReadFile.Append("/.mozilla_gpu_sandbox_read_test");
    std::string path = std::string(testReadFile.get());
    if (access(path.c_str(), F_OK) == 0) {
      child->ErrnoTest("read denied (home test file)"_ns, false, [path] {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
          close(fd);
        }
        return fd;
      });
    } else {
      child->SendReportTestResults(
          "read denied (home test file)"_ns, false,
          "Test file does not exist, test setup failure"_ns);
    }
  } else {
    child->SendReportTestResults("HOME check"_ns, false,
                                 "HOME environment variable not set"_ns);
  }

  // System directory
  child->ErrnoTest("write denied (/tmp)"_ns, false, [] {
    int fd = open("/tmp/gpu_sandbox_test_file.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  child->ErrnoTest("write denied (/private/tmp)"_ns, false, [] {
    int fd = open("/private/tmp/sandboxTest.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  // Shader cache tests
  char buf[PATH_MAX];
  if (confstr(_CS_DARWIN_USER_CACHE_DIR, buf, sizeof(buf)) > 0) {
    nsCString cache(buf);

    // Can't create directories at the cache root.
    nsCString subdir = cache + "/mozilla-gpu-sbox-test"_ns;
    child->ErrnoTest("mkdir denied (cache root subdir)"_ns, false,
                     [s = std::string(subdir.get())] {
                       int rv = mkdir(s.c_str(), 0700);
                       if (rv == 0) {
                         rmdir(s.c_str());  // cleanup if somehow created
                       }
                       return rv;
                     });

    // Can't write files at the cache root.
    nsCString file = cache + "/gpu_test_file.txt"_ns;
    child->ErrnoTest("write denied (cache root)"_ns, false,
                     [f = std::string(file.get())] {
                       int fd = open(f.c_str(), O_CREAT | O_WRONLY, 0600);
                       if (fd >= 0) {
                         close(fd);
                         unlink(f.c_str());
                       }
                       return fd;
                     });

    // Can't create a fake GPU bundle cache directory.
    nsCString fakeGpuDir = cache + "/org.mozilla.firefox-fake-gpu"_ns;
    child->ErrnoTest("mkdir denied (fake GPU cache dir)"_ns, false,
                     [dir = std::string(fakeGpuDir.get())] {
                       int rv = mkdir(dir.c_str(), 0700);
                       if (rv == 0) {
                         rmdir(dir.c_str());  // cleanup if somehow created
                       }
                       return rv;
                     });

    // Actual GPU bundle cache directory
    nsCString actualGpuCacheDir =
        cache + "/"_ns + nsCString(MOZ_GPU_PROCESS_BUNDLEID);

    // Allowed to write a regular file in the GPU bundle cache.
    nsCString legitGpuFile = actualGpuCacheDir + "/gpu_test.cache"_ns;
    child->ErrnoTest("write allowed (GPU bundle cache)"_ns, true,
                     [f = std::string(legitGpuFile.get())] {
                       int fd = open(f.c_str(), O_CREAT | O_WRONLY, 0600);
                       if (fd >= 0) {
                         close(fd);
                         unlink(f.c_str());  // cleanup
                         return 0;
                       }
                       return fd;
                     });

    // Symlink test inside the GPU bundle cache.
    nsCString symlinkInGpuCache = actualGpuCacheDir + "/bad_symlink"_ns;
    child->ErrnoTest("symlink denied (GPU bundle cache)"_ns, false,
                     [s = std::string(symlinkInGpuCache.get())] {
                       int rv = symlink("/etc/passwd", s.c_str());
                       if (rv == 0) {  // cleanup if somehow created
                         int fd = open(s.c_str(), O_RDONLY);
                         if (fd >= 0) {
                           close(fd);
                         }
                         unlink(s.c_str());
                       }
                       return rv;
                     });

  } else {
    child->SendReportTestResults("cache dir check"_ns, false,
                                 "Could not get user cache directory"_ns);
  }

  // Test read permissions
  child->ErrnoTest(
      "read allowed (/System/.../SystemVersion.plist)"_ns, true, [] {
        int fd =
            open("/System/Library/CoreServices/SystemVersion.plist", O_RDONLY);
        if (fd >= 0) {
          close(fd);
          return 0;
        }
        return fd;
      });

  // Test directory listing permissions
  child->ErrnoTest("list allowed (/Library/ColorSync/Profiles)"_ns, true, [] {
    DIR* d = opendir("/Library/ColorSync/Profiles");
    if (!d) return -1;
    struct dirent* entry = readdir(d);
    closedir(d);
    return entry ? 0 : -1;
  });

  child->ErrnoTest("list allowed (/private/var/db/CVMS)"_ns, true, [] {
    DIR* d = opendir("/private/var/db/CVMS");
    if (!d) return -1;
    struct dirent* entry = readdir(d);
    closedir(d);
    return entry ? 0 : -1;
  });

  // Test sysctl permissions
  child->ErrnoTest("sysctl allowed (kern.ostype)"_ns, true, [] {
    char buf[256];
    size_t sz = sizeof(buf);
    int rv = sysctlbyname("kern.ostype", buf, &sz, nullptr, 0);
    return rv;
  });

  child->ErrnoTest("sysctl allowed (hw.memsize)"_ns, true, [] {
    uint64_t mem = 0;
    size_t sz = sizeof(mem);
    int rv = sysctlbyname("hw.memsize", &mem, &sz, nullptr, 0);
    return rv;
  });

  // System directory write
  child->ErrnoTest("write denied (root)"_ns, false, [] {
    int fd = open("/gpu_root_test.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  child->ErrnoTest("write denied (/usr/local)"_ns, false, [] {
    int fd = open("/usr/local/gpu_bad_test.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  child->ErrnoTest("write denied (/etc)"_ns, false, [] {
    int fd = open("/etc/gpu_bad_test.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  RunMacTestLaunchProcess(child);
  RunMacTestAudioAPI(child);
  RunMacTestWindowServer(child, ProcessIsX86_64());

}

}  // namespace mozilla

#endif  // SECURITY_SANDBOX_COMMON_TEST_SANDBOXTESTINGCHILDTESTS_H_
