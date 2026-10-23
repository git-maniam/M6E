/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsExceptionHandler.h"
#include "nsExceptionHandlerUtils.h"
#include "ExtraFileParser.h"

#include "json/json.h"
#include "nsIDUtils.h"
#include "nsIFileStreams.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "mozilla/GeckoArgs.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/Services.h"
#include "nsIObserverService.h"
#include "mozilla/RuntimeExceptionModule.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/ToString.h"
#include "mozilla/TimeStamp.h"

#include "nsPrintfCString.h"
#include "nsSystemInfo.h"
#include "prsystem.h"
#include "nsThreadUtils.h"
#include "nsThread.h"

#include "mozilla/toolkit/crashreporter/mozannotation_client_ffi_generated.h"
#include "mozilla/crash_helper_client_ffi_generated.h"

#ifdef MOZ_BACKGROUNDTASKS
#  include "mozilla/BackgroundTasks.h"
#endif


#  include "breakpad-client/mac/crash_generation/client_info.h"
#  include "breakpad-client/mac/crash_generation/crash_generation_server.h"
#  include "breakpad-client/mac/handler/exception_handler.h"
#  include <string>
#  include <Carbon/Carbon.h>
#  include <CoreFoundation/CoreFoundation.h>
#  include <crt_externs.h>
#  include <fcntl.h>
#  include <mach/mach.h>
#  include <mach/vm_statistics.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <spawn.h>
#  include <unistd.h>
#  include "mac_utils.h"

#include <fmt/format.h>
#include <fstream>
#include <optional>

#include <stdlib.h>
#include <time.h>
#include <prenv.h>
#include <prio.h>
#include "mozilla/Mutex.h"
#include "nsDebug.h"
#include "nsIFile.h"

#include "mozilla/IOInterposer.h"
#include "mozilla/mozalloc_oom.h"

CFStringRef reporterClientAppID = CFSTR("org.mozilla.crashreporter");

using google_breakpad::ClientInfo;
using google_breakpad::CrashGenerationServer;
using namespace mozilla;

// The gToolkitBuildID global is defined to MOZ_BUILDID via gen_buildid.py
// in toolkit/library. See related comment in toolkit/library/moz.build.
extern const char gToolkitBuildID[];

#ifdef MOZ_PHC

namespace mozilla::phc {

// Global instance that is retrieved by the process generating the crash report
MOZ_GLOBINIT mozilla::phc::AddrInfo gAddrInfo;

}  // namespace mozilla::phc

#endif  // defined(MOZ_PHC)

namespace CrashReporter {

typedef char XP_CHAR;
typedef std::string xpstring;
#  define XP_TEXT(x) x
#  define CONVERT_XP_CHAR_TO_UTF16(x) NS_ConvertUTF8toUTF16(x)
#  define CRASH_HELPER_FILENAME u"crashhelper"_ns
#  define CRASH_REPORTER_FILENAME u"crashreporter"_ns
#  define XP_PATH_SEPARATOR "/"
#  define XP_PATH_SEPARATOR_CHAR '/'
#  define XP_PATH_MAX PATH_MAX
#    define XP_STRLEN(x) strlen(x)
#    define XP_TTOA(time, buffer) sprintf(buffer, "%" PRIu64, uint64_t(time))
#    define XP_STOA(size, buffer) sprintf(buffer, "%zu", size_t(size))
#    define my_strlen strlen
#    define my_memchr memchr
#    define sys_close close
#    define sys_fork fork
#    define sys_open open
#    define sys_read read
#    define sys_write write

#if defined(__GNUC__)
#  define MAYBE_UNUSED __attribute__((unused))
#else
#  define MAYBE_UNUSED
#endif  // defined(__GNUC__)

static const XP_CHAR dumpFileExtension[] = XP_TEXT(".dmp");

static const XP_CHAR extraFileExtension[] = XP_TEXT(".extra");
static const XP_CHAR memoryReportExtension[] = XP_TEXT(".memory.json.gz");
constinit static std::optional<xpstring> defaultMemoryReportPath;

static const char kCrashMainID[] = "crash.main.3\n";

static StaticMutex gCrashHelperClientMutex;
static CrashHelperClient* gCrashHelperClient
    MOZ_GUARDED_BY(gCrashHelperClientMutex) = nullptr;
static google_breakpad::ExceptionHandler* gExceptionHandler = nullptr;
static mozilla::Atomic<bool> gEncounteredChildException(false);
constinit static nsCString gServerURL;

static MOZ_GLIBCXX_CONSTINIT xpstring pendingDirectory;
static MOZ_GLIBCXX_CONSTINIT xpstring crashReporterPath;
static MOZ_GLIBCXX_CONSTINIT xpstring crashHelperPath;
static MOZ_GLIBCXX_CONSTINIT xpstring memoryReportPath;

// Where crash events should go.
static MOZ_GLIBCXX_CONSTINIT xpstring eventsDirectory;

// If this is false, we don't launch the crash reporter
static bool doReport = true;

// if this is true, we pass the exception on to the OS crash reporter
static bool showOSCrashReporter = false;

// The time of the last recorded crash, as a time_t value.
static time_t lastCrashTime = 0;
// The pathname of a file to store the crash time in
static XP_CHAR lastCrashTimeFilename[XP_PATH_MAX] = {0};
// The UUID to use for the CrashEventID if a main process crash occurs.
// We should not generate a UUID in the crash handler because GenerateUUID is
// not async-signal-safe without rewriting the logic. See bug 2081887.
static nsID mainProcessCrashEventID;


// this holds additional data sent via the API
static Mutex* notesFieldLock;
static nsCString* notesField = nullptr;
static bool isGarbageCollecting;
static uint32_t eventloopNestingLevel = 0;
static time_t inactiveStateStart = 0;
static uint64_t inactiveStateDuration = 0;

// Avoid a race during application termination.
static Mutex* dumpSafetyLock;
static bool isSafeToDump = false;

// Whether to include heap regions of the crash context.
static bool sIncludeContextHeap = false;

static std::terminate_handler oldTerminateHandler = nullptr;

MOZ_GLIBCXX_CONSTINIT static nsCString childCrashNotifyPipe;




static void ReserveResources() {
}

static void ReleaseResources() {
}


static void CreateFileFromPath(const xpstring& path, nsIFile** file) {
  (void)NS_NewPathStringLocalFile(
      DependentPathString(path.c_str(), path.size()), file);
}

nsresult GetNativePathFromFile(nsIFile* aFile, PathString& aPathString) {
  return aFile->GetNativePath(aPathString);
}

[[nodiscard]]
static std::optional<xpstring> CreatePathFromFile(nsIFile* file) {
  AutoPathString path;
  nsresult rv = GetNativePathFromFile(file, path);
  if (NS_FAILED(rv)) {
    return {};
  }
  return xpstring(static_cast<xpstring::const_pointer>(path.get()),
                  path.Length());
}

static time_t GetCurrentTimeForCrashTime() {
  return time(nullptr);
}

static XP_CHAR* Concat(XP_CHAR* str, const XP_CHAR* toAppend, size_t* size) {
  size_t appendLen = XP_STRLEN(toAppend);
  if (appendLen >= *size) {
    appendLen = *size - 1;
  }

  memcpy(str, toAppend, appendLen * sizeof(XP_CHAR));
  str += appendLen;
  *str = '\0';
  *size -= appendLen;

  return str;
}

void AnnotateOOMAllocationSize(size_t size) { gOOMAllocationSize = size; }

static size_t gTexturesSize = 0;

void AnnotateTexturesSize(size_t size) { gTexturesSize = size; }

// Like Windows CopyFile for *nix
//
// This function is not declared static even though it's not used outside of
// this file because of an issue in Fennec which prevents breakpad's exception
// handler from invoking the MinidumpCallback function. See bug 1424304.
bool copy_file(const char* from, const char* to) {
  const int kBufSize = 4096;
  int fdfrom = sys_open(from, O_RDONLY, 0);
  if (fdfrom < 0) {
    return false;
  }

  bool ok = false;

  int fdto = sys_open(to, O_WRONLY | O_CREAT, 0666);
  if (fdto < 0) {
    sys_close(fdfrom);
    return false;
  }

  char buf[kBufSize];
  while (true) {
    int r = sys_read(fdfrom, buf, kBufSize);
    if (r == 0) {
      ok = true;
      break;
    }
    if (r < 0) {
      break;
    }
    char* wbuf = buf;
    while (r) {
      int w = sys_write(fdto, wbuf, r);
      if (w > 0) {
        r -= w;
        wbuf += w;
      } else if (errno != EINTR) {
        break;
      }
    }
    if (r) {
      break;
    }
  }

  sys_close(fdfrom);
  sys_close(fdto);

  return ok;
}

/**
 * The PlatformWriter class provides a tool to create and write to a file that
 * is safe to call from within an exception handler. To use it this way the
 * file path needs to be provided as a bare C string.
 */
class PlatformWriter {
 public:
  PlatformWriter() : mBuffer{}, mPos(0), mFD(kInvalidFileHandle) {}
  explicit PlatformWriter(const XP_CHAR* aPath) : PlatformWriter() {
    Open(aPath);
  }

  ~PlatformWriter() {
    if (Valid()) {
      Flush();
      sys_close(mFD);
    }
  }

  void Open(const XP_CHAR* aPath) {
    mFD = sys_open(aPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  }

  void OpenHandle(FileHandle aFD) { mFD = aFD; }
  bool Valid() { return mFD != kInvalidFileHandle; }

  void WriteBuffer(const char* aBuffer, size_t aLen) {
    if (!Valid()) {
      return;
    }

    while (aLen-- > 0) {
      WriteChar(*aBuffer++);
    }
  }

  void WriteString(const char* aStr) { WriteBuffer(aStr, my_strlen(aStr)); }

  template <int N>
  void WriteLiteral(const char (&aStr)[N]) {
    WriteBuffer(aStr, N - 1);
  }

  FileHandle FileDesc() { return mFD; }

  PlatformWriter(const PlatformWriter&) = delete;
  const PlatformWriter& operator=(const PlatformWriter&) = delete;

 private:
  void WriteChar(char aChar) {
    if (mPos == kBufferSize) {
      Flush();
    }

    mBuffer[mPos++] = aChar;
  }

  void Flush() {
    if (mPos > 0) {
      char* buffer = mBuffer;
      size_t length = mPos;
      while (length > 0) {
        ssize_t written_bytes = sys_write(mFD, buffer, length);
        if (written_bytes < 0) {
          if (errno == EAGAIN) {
            continue;
          }

          break;
        }
        buffer += written_bytes;
        length -= written_bytes;
      }

      mPos = 0;
    }
  }

  static const size_t kBufferSize = 512;

  char mBuffer[kBufferSize];
  size_t mPos;
  FileHandle mFD;
};

class JSONAnnotationWriter : public AnnotationWriter {
 public:
  explicit JSONAnnotationWriter(PlatformWriter& aPlatformWriter)
      : mWriter(aPlatformWriter), mEmpty(true) {
    mWriter.WriteBuffer("{", 1);
  }

  ~JSONAnnotationWriter() { mWriter.WriteBuffer("}", 1); }

  void Write(Annotation aAnnotation, const char* aValue,
             size_t aLen = 0) override {
    size_t len = aLen ? aLen : my_strlen(aValue);
    const char* annotationStr = AnnotationToString(aAnnotation);

    if (len && CrashReporter::ShouldIncludeAnnotation(aAnnotation, aValue)) {
      WritePrefix();
      mWriter.WriteBuffer(annotationStr, my_strlen(annotationStr));
      WriteSeparator();
      WriteEscapedString(aValue, len);
      WriteSuffix();
    }
  };

  void Write(Annotation aAnnotation, bool aValue) override {
    Write(aAnnotation, aValue ? "1" : "0", 1);
  };

  void Write(Annotation aAnnotation, uint64_t aValue) override {
    char buffer[32] = {};
    XP_STOA(aValue, buffer);
    Write(aAnnotation, buffer);
  };

 private:
  void WritePrefix() {
    if (mEmpty) {
      mWriter.WriteBuffer("\"", 1);
      mEmpty = false;
    } else {
      mWriter.WriteBuffer(",\"", 2);
    }
  }

  void WriteSeparator() { mWriter.WriteBuffer("\":\"", 3); }
  void WriteSuffix() { mWriter.WriteBuffer("\"", 1); }
  void WriteEscapedString(const char* aStr, size_t aLen) {
    for (size_t i = 0; i < aLen; i++) {
      uint8_t c = aStr[i];
      if (c <= 0x1f || c == '\\' || c == '\"') {
        mWriter.WriteBuffer("\\u00", 4);
        WriteHexDigitAsAsciiChar((c & 0x00f0) >> 4);
        WriteHexDigitAsAsciiChar(c & 0x000f);
      } else {
        mWriter.WriteBuffer(aStr + i, 1);
      }
    }
  }

  void WriteHexDigitAsAsciiChar(uint8_t u) {
    char buf[1];
    buf[0] = static_cast<unsigned>((u < 10) ? '0' + u : 'a' + (u - 10));
    mWriter.WriteBuffer(buf, 1);
  }

  PlatformWriter& mWriter;
  bool mEmpty;
};

class BinaryAnnotationWriter : public AnnotationWriter {
 public:
  explicit BinaryAnnotationWriter(PlatformWriter& aPlatformWriter)
      : mPlatformWriter(aPlatformWriter) {}

  void Write(Annotation aAnnotation, const char* aValue,
             size_t aLen = 0) override {
    uint64_t len = aLen ? aLen : my_strlen(aValue);
    mPlatformWriter.WriteBuffer((const char*)&aAnnotation, sizeof(aAnnotation));
    mPlatformWriter.WriteBuffer((const char*)&len, sizeof(len));
    mPlatformWriter.WriteBuffer(aValue, len);
  };

  void Write(Annotation aAnnotation, uint64_t aValue) override {
    char buffer[32] = {};
    XP_STOA(aValue, buffer);
    Write(aAnnotation, buffer);
  };

 private:
  PlatformWriter& mPlatformWriter;
};

#ifdef MOZ_PHC

// 21 is the max length of a 64-bit decimal address entry, including the
// trailing comma or '\0'. And then we add another 32 just to be safe.
const size_t phcStringifiedAnnotationSize =
    (mozilla::phc::StackTrace::kMaxFrames * 21) + 32;

static void PHCStackTraceToString(char* aBuffer, size_t aBufferLen,
                                  const phc::StackTrace& aStack) {
  char addrString[32];
  *aBuffer = 0;
  for (size_t i = 0; i < aStack.mLength; i++) {
    if (i != 0) {
      strcat(aBuffer, ",");
    }
    XP_STOA(uintptr_t(aStack.mPcs[i]), addrString);
    strncat(aBuffer, addrString, aBufferLen - 1);
  }
}

// The stack traces are encoded as a comma-separated list of decimal
// (not hexadecimal!) addresses, e.g. "12345678,12345679,12345680".
static void WritePHCStackTrace(AnnotationWriter& aWriter,
                               const Annotation aName,
                               const Maybe<phc::StackTrace>& aStack) {
  if (aStack.isNothing()) {
    return;
  }

  // 21 is the max length of a 64-bit decimal address entry, including the
  // trailing comma or '\0'. And then we add another 32 just to be safe.
  char addrsString[phcStringifiedAnnotationSize];
  PHCStackTraceToString(addrsString, sizeof(addrsString), *aStack);
  aWriter.Write(aName, addrsString);
}

static void WritePHCAddrInfo(AnnotationWriter& writer,
                             const phc::AddrInfo* aAddrInfo) {
  // Is this a PHC allocation needing special treatment?
  if (aAddrInfo && aAddrInfo->mKind != phc::AddrInfo::Kind::Unknown) {
    const char* kindString;
    switch (aAddrInfo->mKind) {
      case phc::AddrInfo::Kind::Unknown:
        kindString = "Unknown(?!)";
        break;
      case phc::AddrInfo::Kind::NeverAllocatedPage:
        kindString = "NeverAllocatedPage";
        break;
      case phc::AddrInfo::Kind::InUsePage:
        kindString = "InUsePage(?!)";
        break;
      case phc::AddrInfo::Kind::FreedPage:
        kindString = "FreedPage";
        break;
      case phc::AddrInfo::Kind::GuardPage:
        kindString = "GuardPage";
        break;
      default:
        kindString = "Unmatched(?!)";
        break;
    }
    writer.Write(Annotation::PHCKind, kindString);
    writer.Write(Annotation::PHCBaseAddress,
                 reinterpret_cast<uint64_t>(aAddrInfo->mBaseAddr));
    writer.Write(Annotation::PHCUsableSize,
                 static_cast<uint64_t>(aAddrInfo->mUsableSize));

    WritePHCStackTrace(writer, Annotation::PHCAllocStack,
                       aAddrInfo->mAllocStack);
    WritePHCStackTrace(writer, Annotation::PHCFreeStack, aAddrInfo->mFreeStack);
  }
}

#endif

/**
 * If minidump_id is null, we assume that dump_path contains the full
 * dump file path.
 */
static void OpenAPIData(PlatformWriter& aWriter, const XP_CHAR* dump_path,
                        const XP_CHAR* minidump_id = nullptr) {
  static XP_CHAR extraDataPath[XP_PATH_MAX];
  size_t size = XP_PATH_MAX;
  XP_CHAR* p;
  if (minidump_id) {
    p = Concat(extraDataPath, dump_path, &size);
    p = Concat(p, XP_PATH_SEPARATOR, &size);
    p = Concat(p, minidump_id, &size);
  } else {
    p = Concat(extraDataPath, dump_path, &size);
    // Skip back past the .dmp extension, if any.
    if (*(p - 4) == XP_TEXT('.')) {
      p -= 4;
      size += 4;
    }
  }
  Concat(p, extraFileExtension, &size);
  aWriter.Open(extraDataPath);
}

// Extract the total physical memory of the system.
static void WritePhysicalMemoryStatus(AnnotationWriter& aWriter) {
  uint64_t physicalMemoryByteSize = 0;
  const size_t NAME_LEN = 2;
  int name[NAME_LEN] = {/* Hardware */ CTL_HW,
                        /* 64-bit physical memory size */ HW_MEMSIZE};
  size_t infoByteSize = sizeof(physicalMemoryByteSize);
  if (sysctl(name, NAME_LEN, &physicalMemoryByteSize, &infoByteSize,
             /* We do not replace data */ nullptr,
             /* We do not replace data */ 0) != -1) {
    aWriter.Write(Annotation::TotalPhysicalMemory, physicalMemoryByteSize);
  }
}

// Extract available and purgeable physical memory.
static void WriteAvailableMemoryStatus(AnnotationWriter& aWriter) {
  auto host = mach_host_self();
  vm_statistics64_data_t stats;
  unsigned int count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&stats, &count) ==
      KERN_SUCCESS) {
    aWriter.Write(Annotation::AvailablePhysicalMemory,
                  static_cast<uint64_t>(stats.free_count * vm_page_size));
    aWriter.Write(Annotation::PurgeablePhysicalMemory,
                  static_cast<uint64_t>(stats.purgeable_count * vm_page_size));
  }
}

// Extract the status of the swap.
static void WriteSwapFileStatus(AnnotationWriter& aWriter) {
  const size_t NAME_LEN = 2;
  int name[] = {/* Hardware */ CTL_VM,
                /* 64-bit physical memory size */ VM_SWAPUSAGE};
  struct xsw_usage swapUsage;
  size_t infoByteSize = sizeof(swapUsage);
  if (sysctl(name, NAME_LEN, &swapUsage, &infoByteSize,
             /* We do not replace data */ nullptr,
             /* We do not replace data */ 0) != -1) {
    aWriter.Write(Annotation::AvailableSwapMemory, swapUsage.xsu_avail);
  }
}
static void AnnotateMemoryStatus(AnnotationWriter& aWriter) {
  WritePhysicalMemoryStatus(aWriter);
  WriteAvailableMemoryStatus(aWriter);
  WriteSwapFileStatus(aWriter);
}



/**
 * Launches the program specified in aProgramPath with aMinidumpPath as its
 * sole argument.
 *
 * @param aProgramPath The path of the program to be launched
 * @param aMinidumpPath The path of the minidump file, passed as an argument
 *        to the launched program
 */
static bool LaunchProgram(const XP_CHAR* aProgramPath,
                          const XP_CHAR* aMinidumpPath) {
  pid_t pid = 0;
  char* const my_argv[] = {const_cast<char*>(aProgramPath),
                           const_cast<char*>(aMinidumpPath), nullptr};

  char** env = nullptr;
  char*** nsEnv = _NSGetEnviron();
  if (nsEnv) {
    env = *nsEnv;
  }

  int rv = posix_spawnp(&pid, my_argv[0], nullptr, nullptr, my_argv, env);

  if (rv != 0) {
    return false;
  }

  return true;
}


nsresult RecordPlatformAnnotations() {
  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // CPU architecture values corresponding to `system_info.cpu_arch` values in
  // https://github.com/rust-minidump/rust-minidump/blob/main/minidump-processor/json-schema.md,
  // which is the format expected by Socorro.
  MOZ_TRY(RecordAnnotationCString(Annotation::CPUArchitecture,
#if defined(__i386__) || defined(_M_IX86)
                                  "x86"
#elif defined(__x86_64__) || defined(_M_X64)
                                  "amd64"
#elif defined(__powerpc__) || defined(__POWERPC__) || defined(__ppc__) || \
    defined(__PPC__)
                                  "ppc"
#elif defined(__powerpc64__) || defined(__PPC64__) || defined(__ppc64__)
                                  "ppc64"
#elif defined(__sparc__) || defined(__sparc)
                                  "sparc"
#elif defined(__arm__)
                                  "arm"
#elif defined(__aarch64__) || defined(_M_ARM64)
                                  "arm64"
#else
                                  "unknown"
#endif
                                  ));

  // OS values corresponding to `system_info.os` values in
  // https://github.com/rust-minidump/rust-minidump/blob/main/minidump-processor/json-schema.md,
  // which is the format expected by Socorro.
  MOZ_TRY(RecordAnnotationCString(Annotation::OS,
                                  "Mac OS X"
                                  ));

  // These values are read straight from NSPR and LSBUtils rather than through
  // @mozilla.org/system-info;1, so that recording them neither instantiates
  // that service nor requires XPCOM to be initialized.
  // nsSystemInfo reports the Android SDK version rather than the release here,
  // and lib-crash records OSVersion for Android itself.
  char sysRelease[SYS_INFO_BUFFER_LENGTH];
  if (PR_GetSystemInfo(PR_SI_RELEASE, sysRelease, sizeof(sysRelease)) ==
      PR_SUCCESS) {
    nsAutoCString osVersion(sysRelease);
    char sysBuild[SYS_INFO_BUFFER_LENGTH];
    if (PR_GetSystemInfo(PR_SI_RELEASE_BUILD, sysBuild, sizeof(sysBuild)) ==
        PR_SUCCESS) {
      osVersion.Append(
          ' '
      );
      osVersion.Append(sysBuild);
    }
    MOZ_TRY(RecordAnnotationNSCString(Annotation::OSVersion, osVersion));
  }


  return NS_OK;
}

static nsresult RecordCPUInfoAnnotation() {
  // Run in a background task so we don't block the main thread with
  // CollectProcessInfo().
  return NS_DispatchBackgroundTask(
      NS_NewRunnableFunction("CPUInfoAnnotation", [] {
        ProcessInfo procinfo = {};
        if (NS_SUCCEEDED(CollectProcessInfo(procinfo))) {
          nsCString cpuInfo(fmt::format("family {} model {} stepping {}",
                                        procinfo.cpuFamily, procinfo.cpuModel,
                                        procinfo.cpuStepping));
          RecordAnnotationNSCString(Annotation::CPUInfo, std::move(cpuInfo));
        }
      }));
}

nsresult RecordXPCOMPlatformAnnotations() {
  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  return RecordCPUInfoAnnotation();
}

static void WriteAnnotations(AnnotationWriter& aWriter,
                             const AnnotationTable& aAnnotations) {
  for (auto key : MakeEnumeratedRange(Annotation::Count)) {
    const nsCString& value = aAnnotations[key];
    if (!value.IsEmpty() && ShouldIncludeAnnotation(key, value.get())) {
      aWriter.Write(key, value.get(), value.Length());
    }
  }
}

static void WriteSynthesizedAnnotations(AnnotationWriter& aWriter) {
  AnnotateMemoryStatus(aWriter);
}

// WARNING: This function is called from within the exception handler, and must
// not do things that are unsafe from an exception handler (like allocating
// memory).
static void WriteAnnotationsForMainProcessCrash(PlatformWriter& pw,
                                                const phc::AddrInfo* addrInfo,
                                                time_t crashTime) {
  JSONAnnotationWriter writer(pw);

  for (auto key : MakeEnumeratedRange(Annotation::Count)) {
    uint32_t contents = 0;
    size_t len = 0;
    size_t address =
        mozannotation_get_contents(static_cast<uint32_t>(key), &contents, &len);
    if (address != 0) {
      switch (TypeOfAnnotation(key)) {
        case AnnotationType::String:
          switch (contents) {
            case ANNOTATION_CONTENTS_NSCSTRINGPOINTER: {
              const nsCString* string =
                  reinterpret_cast<const nsCString*>(address);
              writer.Write(key, string->Data(), string->Length());
            } break;
            case ANNOTATION_CONTENTS_CSTRINGPOINTER:
              address = *(reinterpret_cast<size_t*>(address));
              if (address == 0) {
                break;
              }
              // FALLTHROUGH
            case ANNOTATION_CONTENTS_CSTRING: {
              writer.Write(key, reinterpret_cast<const char*>(address));
            } break;
            case ANNOTATION_CONTENTS_BYTEBUFFER:
              writer.Write(key, reinterpret_cast<const char*>(address), len);
              break;
            case ANNOTATION_CONTENTS_OWNEDBYTEBUFFER:
              writer.Write(key, reinterpret_cast<const char*>(address), len);
              break;
            case ANNOTATION_CONTENTS_EMPTY:
              break;
          }
          break;
        case AnnotationType::Boolean:
          writer.Write(key, *reinterpret_cast<const bool*>(address));
          break;
        case AnnotationType::U32:
          writer.Write(key, static_cast<uint64_t>(
                                *reinterpret_cast<uint32_t*>(address)));
          break;
        case AnnotationType::U64:
          writer.Write(key, *reinterpret_cast<uint64_t*>(address));
          break;
        case AnnotationType::USize:
          writer.Write(
              key, static_cast<uint64_t>(*reinterpret_cast<size_t*>(address)));
          break;
        case AnnotationType::Object:
          // Object annotations are only produced later by minidump-analyzer.
          break;
      }
    }
  }

  WriteSynthesizedAnnotations(writer);
  writer.Write(Annotation::CrashTime, uint64_t(crashTime));
  // Add a unique identifier for this main process crash event (generated in
  // SetExceptionHandler). We cannot have reached this point of the exception
  // handler without this value having already been set (and in the worst-case
  // scenario, it will be the nil UUID from static zero-initialization).
  {
    NSID_TrimBracketsASCII uuidString(mainProcessCrashEventID);
    writer.Write(Annotation::CrashEventID, uuidString.Data(),
                 uuidString.Length());
  }

  // ToSeconds preserves the full precision of the TimeDuration. It is assumed
  // that visualizations of this value will format/truncate it to their needs.
  double uptimeTS =
      (TimeStamp::NowLoRes() - TimeStamp::ProcessCreation()).ToSeconds();
  char uptimeTSString[64] = {};
  SimpleNoCLibDtoA(uptimeTS, uptimeTSString, sizeof(uptimeTSString));
  writer.Write(Annotation::UptimeTS, uptimeTSString);

  // calculate time since last crash (if possible).
  if (lastCrashTime != 0) {
    uint64_t timeSinceLastCrash = crashTime - lastCrashTime;

    writer.Write(Annotation::SecondsSinceLastCrash, timeSinceLastCrash);
  }


#ifdef MOZ_PHC
  WritePHCAddrInfo(writer, addrInfo);
#endif
}

static void WriteCrashEventFile(time_t crashTime, const char* crashTimeString,
                                const phc::AddrInfo* addrInfo,
                                const XP_CHAR* minidump_id
) {
#ifdef MOZ_BACKGROUNDTASKS
  if (BackgroundTasks::IsBackgroundTaskMode()) {
    // Do not generate a crash event file if the main process was running a
    // background task, as the crash won't be visible to the user.
    return;
  }
#endif

  // Minidump IDs are UUIDs (36) + NULL.
  static char id_ascii[37] = {};
  MOZ_ASSERT(XP_STRLEN(minidump_id) == 36);
  for (uint32_t i = 0; i < 36; i++) {
    id_ascii[i] = *((char*)(minidump_id + i));
  }

  PlatformWriter eventFile;

  if (!eventsDirectory.empty()) {
    static XP_CHAR crashEventPath[XP_PATH_MAX];
    size_t size = XP_PATH_MAX;
    XP_CHAR* p;
    p = Concat(crashEventPath, eventsDirectory.c_str(), &size);
    p = Concat(p, XP_PATH_SEPARATOR, &size);
    Concat(p, minidump_id, &size);

    eventFile.Open(crashEventPath);
    eventFile.WriteLiteral(kCrashMainID);
    eventFile.WriteString(crashTimeString);
    eventFile.WriteLiteral("\n");
    eventFile.WriteString(id_ascii);
    eventFile.WriteLiteral("\n");
    WriteAnnotationsForMainProcessCrash(eventFile, addrInfo, crashTime);
  }
}

void SetUpMemtestEnv() {
  if (StaticPrefs::browser_crashReporter_memtest()) {
    const char* env = "MOZ_CRASHREPORTER_RUN_MEMTEST=1";
    PR_SetEnv(env);

    auto memtestKindsLock = StaticPrefs::browser_crashReporter_memtestKinds();
    if (!memtestKindsLock->IsEmpty()) {
      char* env = strdup(
          ("MOZ_CRASHREPORTER_MEMTEST_KINDS=" + ToString(*memtestKindsLock))
              .c_str());
      PR_SetEnv(env);
    }
  }
}

// Callback invoked from breakpad's exception handler, this writes out the
// last annotations after a crash occurs and launches the crash reporter client.
//
// This function is not declared static even though it's not used outside of
// this file because of an issue in Fennec which prevents breakpad's exception
// handler from invoking it. See bug 1424304.
bool MinidumpCallback(
    const XP_CHAR* dump_path, const XP_CHAR* minidump_id,
    void* context,
    const phc::AddrInfo* addrInfo, bool succeeded) {
  bool returnValue = showOSCrashReporter ? false : succeeded;

  static XP_CHAR minidumpPath[XP_PATH_MAX];
  size_t size = XP_PATH_MAX;
  XP_CHAR* p;
  p = Concat(minidumpPath, dump_path, &size);
  p = Concat(p, XP_PATH_SEPARATOR, &size);
  p = Concat(p, minidump_id, &size);
  Concat(p, dumpFileExtension, &size);

  static XP_CHAR memoryReportLocalPath[XP_PATH_MAX];
  size = XP_PATH_MAX;
  p = Concat(memoryReportLocalPath, dump_path, &size);
  p = Concat(p, XP_PATH_SEPARATOR, &size);
  p = Concat(p, minidump_id, &size);
  Concat(p, memoryReportExtension, &size);

  if (!memoryReportPath.empty()) {
    copy_file(memoryReportPath.c_str(), memoryReportLocalPath);
  }

  time_t crashTime = GetCurrentTimeForCrashTime();
  char crashTimeString[32];
  XP_TTOA(crashTime, crashTimeString);

  // write crash time to file
  if (lastCrashTimeFilename[0] != 0) {
    PlatformWriter lastCrashFile(lastCrashTimeFilename);
    lastCrashFile.WriteString(crashTimeString);
  }

  WriteCrashEventFile(crashTime, crashTimeString, addrInfo,
                      minidump_id
  );

  {
    PlatformWriter apiData;
    OpenAPIData(apiData, dump_path, minidump_id);
    WriteAnnotationsForMainProcessCrash(apiData, addrInfo, crashTime);
  }

  SetUpMemtestEnv();

  bool isBackgroundTaskMode = false;
#ifdef MOZ_BACKGROUNDTASKS
  isBackgroundTaskMode = BackgroundTasks::IsBackgroundTaskMode();
#endif
  if (doReport && isSafeToDump && !isBackgroundTaskMode) {
    // We launch the crash reporter client/dialog only if we've been explicitly
    // asked to report crashes and if we weren't already trying to unset the
    // exception handler (which is indicated by isSafeToDump being false).
    returnValue = LaunchProgram(crashReporterPath.c_str(), minidumpPath);
  }


  return returnValue;
}

static size_t EnsureTrailingSlash(XP_CHAR* aBuf, size_t aBufLen) {
  size_t len = XP_STRLEN(aBuf);
  if ((len + 1) < aBufLen && len > 0 &&
      aBuf[len - 1] != XP_PATH_SEPARATOR_CHAR) {
    aBuf[len] = XP_PATH_SEPARATOR_CHAR;
    ++len;
    aBuf[len] = 0;
  }
  return len;
}


static size_t BuildTempPath(char* aBuf, size_t aBufLen) {
  if (aBufLen < PATH_MAX) {
    return 0;
  }

  FSRef fsRef;
  OSErr err =
      FSFindFolder(kUserDomain, kTemporaryFolderType, kCreateFolder, &fsRef);
  if (err != noErr) {
    return 0;
  }

  OSStatus status = FSRefMakePath(&fsRef, (UInt8*)aBuf, PATH_MAX);
  if (status != noErr) {
    return 0;
  }

  return EnsureTrailingSlash(aBuf, aBufLen);
}


template <typename PathStringT>
static bool BuildTempPath(PathStringT& aResult) {
  if (!aResult.IsEmpty()) {
    return true;
  }

  aResult.SetLength(XP_PATH_MAX);
  size_t actualLen = BuildTempPath(aResult.BeginWriting(), XP_PATH_MAX);
  if (!actualLen) {
    return false;
  }
  aResult.SetLength(actualLen);
  return true;
}


// Prepares annotations that require runtime processing at the point of crash.
// This runs in an exception handler and should be moved out to the crash
// helper when possible.
static void PrepareCrashTimeAnnotations() {
  if (inactiveStateStart) {
    inactiveStateDuration = GetCurrentTimeForCrashTime() - inactiveStateStart;
  }
}

// Do various actions to prepare the process for minidump generation.
// This includes disabling the I/O interposer and DLL blocklist which both
// would get in the way. We also free the resources we have reserved, such as
// address space on 32-bit Windows builds, so that they're available to the
// minidump generation code.
static void PrepareForMinidump(bool isChildProcess = true) {
  mozilla::IOInterposer::Disable();
  ReleaseResources();
  PrepareCrashTimeAnnotations();

  if (isChildProcess) {
    crash_helper_wait_for_rendezvous();
  }
}


static bool Filter(void* context) {
  PrepareForMinidump(/* isChildProcess */ false);
  return true;
}

static bool ChildFilter(void* context) {
  if (gEncounteredChildException.exchange(true)) {
    return false;
  }

  PrepareForMinidump();
  return true;
}


static bool ShouldReport() {
  // this environment variable prevents us from launching
  // the crash reporter client
  const char* envvar = PR_GetEnv("MOZ_CRASHREPORTER_NO_REPORT");
  if (envvar && *envvar) {
    return false;
  }

  envvar = PR_GetEnv("MOZ_CRASHREPORTER_FULLDUMP");
  if (envvar && *envvar) {
    return false;
  }

  return true;
}

static void TerminateHandler() { MOZ_CRASH("Unhandled exception"); }


// Locate the specified executable and store its path as a native string in
// the |aPath| so we can later invoke it from within the exception handler.
static nsresult LocateExecutable(nsIFile* aXREDirectory, const nsAString& aName,
                                 PathString& aPath) {
  nsCOMPtr<nsIFile> exePath;
  nsresult rv = aXREDirectory->Clone(getter_AddRefs(exePath));
  NS_ENSURE_SUCCESS(rv, rv);

  if (aName.Equals(CRASH_REPORTER_FILENAME)) {
    exePath->SetNativeLeafName("MacOS"_ns);
    exePath->Append(u"crashreporter.app"_ns);
    exePath->Append(u"Contents"_ns);
    exePath->Append(u"MacOS"_ns);
  }

  exePath->Append(aName);
  aPath = exePath->NativePath();
  return NS_OK;
}


static void InitializeAppNotes() {
  notesFieldLock = new Mutex("notesFieldLock");
  notesField = new nsCString();
}

// Register crash annotations that are present in both main and child processes
static void RegisterAnnotations() {
  mozannotation_register_cstring_ptr(
      static_cast<uint32_t>(Annotation::MozCrashReason), &gMozCrashReason);
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  mozannotation_register_cstring(
      static_cast<uint32_t>(Annotation::MainThreadRunnableName),
      &nsThread::sMainThreadRunnableName[0]);
#endif
  mozannotation_register_bytebuffer(
      static_cast<uint32_t>(Annotation::EventLoopNestingLevel),
      &eventloopNestingLevel, sizeof(uint32_t));
  mozannotation_register_nscstring(static_cast<uint32_t>(Annotation::Notes),
                                   notesField);
  mozannotation_register_bytebuffer(
      static_cast<uint32_t>(Annotation::OOMAllocationSize), &gOOMAllocationSize,
      sizeof(size_t));
  mozannotation_register_bytebuffer(
      static_cast<uint32_t>(Annotation::IsGarbageCollecting),
      &isGarbageCollecting, sizeof(bool));
  mozannotation_register_nscstring(static_cast<uint32_t>(Annotation::ServerURL),
                                   &gServerURL);
  mozannotation_register_bytebuffer(
      static_cast<uint32_t>(Annotation::TextureUsage), &gTexturesSize,
      sizeof(size_t));
  mozannotation_register_bytebuffer(
      static_cast<uint32_t>(Annotation::LastInteractionDuration),
      &inactiveStateDuration, sizeof(uint64_t));
}

static void TeardownAppNotes() {
  delete notesFieldLock;
  notesFieldLock = nullptr;

  delete notesField;
  notesField = nullptr;
}

nsresult SetExceptionHandler(nsIFile* aXREDirectory, bool force /*=false*/) {
  if (gExceptionHandler) return NS_ERROR_ALREADY_INITIALIZED;

  if (!CrashReporterIsEnabled(force)) {
    return NS_OK;
  }

  // this environment variable prevents us from launching
  // the crash reporter client
  doReport = ShouldReport();

  RegisterRuntimeExceptionModule();
  InitializeAppNotes();
  RegisterAnnotations();

  // Locate the crash reporter executable
  PathString crashReporterPath_temp;
  nsresult rv = LocateExecutable(aXREDirectory, CRASH_REPORTER_FILENAME,
                                 crashReporterPath_temp);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  crashReporterPath = crashReporterPath_temp.get();

  // get temp path to use for minidump path
  PathString tempPath;
  if (!BuildTempPath(tempPath)) {
    return NS_ERROR_FAILURE;
  }

  ReserveResources();



  // Initialize the flag and mutex used to avoid dump processing
  // once browser termination has begun.
  NS_ASSERTION(!dumpSafetyLock, "Shouldn't have a lock yet");
  // Do not deallocate this lock while it is still possible for
  // isSafeToDump to be tested on another thread.
  dumpSafetyLock = new Mutex("dumpSafetyLock");
  MutexAutoLock lock(*dumpSafetyLock);
  isSafeToDump = true;

  // Set the CrashEventID to use in the event of a main process crash
  mainProcessCrashEventID = nsID::GenerateUUID();

  // now set the exception handler


  gExceptionHandler = new google_breakpad::ExceptionHandler(
                     tempPath.get(),

      Filter, MinidumpCallback, nullptr,
      true
      ,
      nullptr
  );

  if (!gExceptionHandler) return NS_ERROR_OUT_OF_MEMORY;


  // store application start time
  RecordAnnotationU64(Annotation::StartupTime,
                      static_cast<uint64_t>(time(nullptr)));

  // On OS X, many testers like to see the OS crash reporting dialog
  // since it offers immediate stack traces.  We allow them to set
  // a default to pass exceptions to the OS handler.
  Boolean keyExistsAndHasValidFormat = false;
  Boolean prefValue = ::CFPreferencesGetAppBooleanValue(
      CFSTR("OSCrashReporter"), kCFPreferencesCurrentApplication,
      &keyExistsAndHasValidFormat);
  if (keyExistsAndHasValidFormat) showOSCrashReporter = prefValue;

  oldTerminateHandler = std::set_terminate(&TerminateHandler);

  return NS_OK;
}

bool GetEnabled() { return gExceptionHandler != nullptr; }

bool GetMinidumpPath(nsAString& aPath) {
  if (!gExceptionHandler) return false;

  aPath = CONVERT_XP_CHAR_TO_UTF16(gExceptionHandler->dump_path().c_str());
  return true;
}

nsresult SetMinidumpPath(const nsAString& aPath) {
  if (!gExceptionHandler) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  AutoPathString path;
  path = NS_ConvertUTF16toUTF8(aPath);

  // Set the path for the in-process exception handler
  gExceptionHandler->set_dump_path(path.get());

  // Set the path used by the crash helper for out-of-process crash generation
  StaticMutexAutoLock lock(gCrashHelperClientMutex);
  if (gCrashHelperClient) {
    set_crash_report_path(
        gCrashHelperClient,
        mozilla::BitwiseCast<const BreakpadChar*>(path.get()));
  }

  return NS_OK;
}

static nsresult WriteDataToFile(nsIFile* aFile, const nsACString& data) {
  PRFileDesc* fd;
  nsresult rv = aFile->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE, 00600, &fd);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_OK;
  if (PR_Write(fd, data.Data(), data.Length()) == -1) {
    rv = NS_ERROR_FAILURE;
  }
  PR_Close(fd);
  return rv;
}

static nsresult GetFileContents(nsIFile* aFile, nsACString& data) {
  PRFileDesc* fd;
  nsresult rv = aFile->OpenNSPRFileDesc(PR_RDONLY, 0, &fd);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_OK;
  int32_t filesize = PR_Available(fd);
  if (filesize <= 0) {
    rv = NS_ERROR_FILE_NOT_FOUND;
  } else {
    data.SetLength(filesize);
    if (PR_Read(fd, data.BeginWriting(), filesize) == -1) {
      rv = NS_ERROR_FAILURE;
    }
  }
  PR_Close(fd);
  return rv;
}

// Function typedef for initializing a piece of data that we
// don't already have.
typedef nsresult (*InitDataFunc)(nsACString&);

// Attempt to read aFile's contents into aContents, if aFile
// does not exist, create it and initialize its contents
// by calling aInitFunc for the data.
static nsresult GetOrInit(nsIFile* aDir, const nsACString& filename,
                          nsACString& aContents, InitDataFunc aInitFunc) {
  bool exists;

  nsCOMPtr<nsIFile> dataFile;
  nsresult rv = aDir->Clone(getter_AddRefs(dataFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dataFile->AppendNative(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dataFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists) {
    if (aInitFunc) {
      // get the initial value and write it to the file
      rv = aInitFunc(aContents);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = WriteDataToFile(dataFile, aContents);
    } else {
      // didn't pass in an init func
      rv = NS_ERROR_FAILURE;
    }
  } else {
    // just get the file's contents
    rv = GetFileContents(dataFile, aContents);
  }

  return rv;
}

// Ensure a directory exists and create it if missing.
static nsresult EnsureDirectoryExists(nsIFile* dir) {
  nsresult rv = dir->Create(nsIFile::DIRECTORY_TYPE, 0700);

  if (NS_WARN_IF(NS_FAILED(rv) && rv != NS_ERROR_FILE_ALREADY_EXISTS)) {
    return rv;
  }

  return NS_OK;
}

// Creates a directory that will be accessible by the crash reporter. The
// directory will live under Firefox default data directory and will use the
// specified name. The directory path will be passed to the crashreporter via
// the specified environment variable.
static nsresult SetupCrashReporterDirectory(nsIFile* aAppDataDirectory,
                                            const char* aDirName,
                                            const XP_CHAR* aEnvVarName,
                                            nsIFile** aDirectory = nullptr) {
  nsCOMPtr<nsIFile> directory;
  nsresult rv = aAppDataDirectory->Clone(getter_AddRefs(directory));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = directory->AppendNative(nsDependentCString(aDirName));
  NS_ENSURE_SUCCESS(rv, rv);

  EnsureDirectoryExists(directory);
  std::optional<xpstring> directoryPath = CreatePathFromFile(directory);

  if (!directoryPath) {
    return NS_ERROR_FAILURE;
  }

  setenv(aEnvVarName, directoryPath->c_str(), /* overwrite */ 1);

  if (aDirectory) {
    directory.forget(aDirectory);
  }

  return NS_OK;
}

// Annotate the crash report with a Unique User ID and time
// since install.  Also do some prep work for recording
// time since last crash, which must be calculated at
// crash time.
// If any piece of data doesn't exist, initialize it first.
nsresult SetupExtraData(nsIFile* aAppDataDirectory, nsIFile* aXreDirectory) {
  nsCOMPtr<nsIFile> dataDirectory;
  nsresult rv =
      SetupCrashReporterDirectory(aAppDataDirectory, "Crash Reports",
                                  XP_TEXT("MOZ_CRASHREPORTER_DATA_DIRECTORY"),
                                  getter_AddRefs(dataDirectory));

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = SetupCrashReporterDirectory(aAppDataDirectory, "Pending Pings",
                                   XP_TEXT("MOZ_CRASHREPORTER_PING_DIRECTORY"));

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsAutoString xreDirPath;

  uint64_t install_time = get_install_time(
      xreDirPath.IsEmpty()
          ? nullptr
          : mozilla::BitwiseCast<const BreakpadChar*>(xreDirPath.get()));
  if (install_time != 0) {
    RecordAnnotationU64(Annotation::InstallTime, install_time);
  }

  // this is a little different, since we can't init it with anything,
  // since it's stored at crash time, and we can't annotate the
  // crash report with the stored value, since we really want
  // (now - LastCrash), so we just get a value if it exists,
  // and store it in a time_t value.
  nsAutoCString last_crash_time;
  if (NS_SUCCEEDED(
          GetOrInit(dataDirectory, "LastCrash"_ns, last_crash_time, nullptr))) {
    lastCrashTime = (time_t)atol(last_crash_time.get());
  }

  // not really the best place to init this, but I have the path I need here
  nsCOMPtr<nsIFile> lastCrashFile;
  rv = dataDirectory->Clone(getter_AddRefs(lastCrashFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = lastCrashFile->AppendNative("LastCrash"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  memset(lastCrashTimeFilename, 0, sizeof(lastCrashTimeFilename));

  PathString filename;
  rv = GetNativePathFromFile(lastCrashFile, filename);
  NS_ENSURE_SUCCESS(rv, rv);

  if (filename.Length() < XP_PATH_MAX) {
    strncpy(lastCrashTimeFilename, filename.get(), filename.Length());
  }

  return NS_OK;
}

nsresult UnsetExceptionHandler() {
  if (isSafeToDump) {
    MutexAutoLock lock(*dumpSafetyLock);
    isSafeToDump = false;
  }


  delete gExceptionHandler;

  gServerURL = "";
  TeardownAppNotes();

  if (!gExceptionHandler) return NS_ERROR_NOT_INITIALIZED;

  gExceptionHandler = nullptr;

  delete dumpSafetyLock;
  dumpSafetyLock = nullptr;

  std::set_terminate(oldTerminateHandler);

  return NS_OK;
}

const bool* RegisterAnnotationBool(Annotation aKey, const bool* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::Boolean,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return nullptr;
  }

  return reinterpret_cast<const bool*>(mozannotation_register_bytebuffer(
      static_cast<uint32_t>(aKey), aData, sizeof(bool)));
}

const uint32_t* RegisterAnnotationU32(Annotation aKey, const uint32_t* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::U32,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return nullptr;
  }

  return reinterpret_cast<const uint32_t*>(mozannotation_register_bytebuffer(
      static_cast<uint32_t>(aKey), aData, sizeof(uint32_t)));
}

const uint64_t* RegisterAnnotationU64(Annotation aKey, const uint64_t* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::U64,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return nullptr;
  }

  return reinterpret_cast<const uint64_t*>(mozannotation_register_bytebuffer(
      static_cast<uint32_t>(aKey), aData, sizeof(uint64_t)));
}

const size_t* RegisterAnnotationUSize(Annotation aKey, const size_t* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::USize,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return nullptr;
  }

  return reinterpret_cast<const size_t*>(mozannotation_register_bytebuffer(
      static_cast<uint32_t>(aKey), aData, sizeof(size_t)));
}

const char* RegisterAnnotationCString(Annotation aKey, const char* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::String,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return nullptr;
  }

  return mozannotation_register_cstring(static_cast<uint32_t>(aKey), aData);
}

const nsCString* RegisterAnnotationNSCString(Annotation aKey,
                                             const nsCString* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::String,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return nullptr;
  }

  return mozannotation_register_nscstring(static_cast<uint32_t>(aKey), aData);
}

nsresult RecordAnnotationBool(Annotation aKey, bool aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::Boolean,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mozannotation_record_bytebuffer(static_cast<uint32_t>(aKey), &aData,
                                  sizeof(bool));
  return NS_OK;
}

nsresult RecordAnnotationU32(Annotation aKey, uint32_t aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::U32,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mozannotation_record_bytebuffer(static_cast<uint32_t>(aKey), &aData,
                                  sizeof(uint32_t));
  return NS_OK;
}

nsresult RecordAnnotationU64(Annotation aKey, uint64_t aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::U64,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mozannotation_record_bytebuffer(static_cast<uint64_t>(aKey), &aData,
                                  sizeof(uint64_t));
  return NS_OK;
}

nsresult RecordAnnotationUSize(Annotation aKey, size_t aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::USize,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mozannotation_record_bytebuffer(static_cast<size_t>(aKey), &aData,
                                  sizeof(size_t));
  return NS_OK;
}

nsresult RecordAnnotationCString(Annotation aKey, const char* aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::String,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  mozannotation_record_cstring(static_cast<uint32_t>(aKey), aData);
  return NS_OK;
}

nsresult RecordAnnotationNSCString(Annotation aKey, const nsACString& aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::String,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mozannotation_record_nscstring_from_raw_parts(
      static_cast<uint32_t>(aKey),
      reinterpret_cast<const uint8_t*>(aData.Data()), aData.Length());
  return NS_OK;
}

nsresult RecordAnnotationNSString(Annotation aKey, const nsAString& aData) {
  MOZ_ASSERT(TypeOfAnnotation(aKey) == AnnotationType::String,
             "Wrong annotation type");

  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsAutoCString data = NS_ConvertUTF16toUTF8(aData);
  RecordAnnotationNSCString(aKey, data);
  return NS_OK;
}

nsresult UnrecordAnnotation(Annotation aKey) {
  if (!GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mozannotation_unregister(static_cast<uint32_t>(aKey));
  return NS_OK;
}

// TODO: The first three methods here should be migrated to use native
// types instead of turning the values into strings. They're not currently
// used in mozilla-central so it doesn't really matter in the short term.
AutoRecordAnnotation::AutoRecordAnnotation(Annotation key, bool data)
    : AutoRecordAnnotation(key, data ? "1"_ns : "0"_ns) {}

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key, int data)
    : AutoRecordAnnotation(key, nsPrintfCString("%d", data)) {}

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key, unsigned data)
    : AutoRecordAnnotation(key, nsPrintfCString("%u", data)) {}

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key,
                                           const nsACString& data)
    : mKey(key), mCurrent(data) {
  if (GetEnabled()) {
    mPrevious =
        mozannotation_register_nscstring(static_cast<uint32_t>(key), &mCurrent);
  }
}

AutoRecordAnnotation::~AutoRecordAnnotation() {
  if (GetEnabled()) {
    (void)mozannotation_register_nscstring(static_cast<uint32_t>(mKey),
                                           mPrevious);
  }
}

void MergeCrashAnnotations(AnnotationTable& aDst, const AnnotationTable& aSrc) {
  for (auto key : MakeEnumeratedRange(Annotation::Count)) {
    const nsCString& value = aSrc[key];
    if (!value.IsEmpty()) {
      aDst[key] = value;
    }
  }
}

// Adds the uptime annotations
static void AddCommonAnnotations(AnnotationTable& aAnnotations) {
  // ToSeconds preserves the full precision of the TimeDuration. It is assumed
  // that visualizations of this value will format/truncate it to their needs.
  double uptimeTS =
      (TimeStamp::NowLoRes() - TimeStamp::ProcessCreation()).ToSeconds();
  nsAutoCString uptimeStr;
  uptimeStr.AppendFloat(uptimeTS);
  aAnnotations[Annotation::UptimeTS] = std::move(uptimeStr);
}

nsresult SetGarbageCollecting(bool collecting) {
  if (!GetEnabled()) return NS_ERROR_NOT_INITIALIZED;

  isGarbageCollecting = collecting;

  return NS_OK;
}

void SetEventloopNestingLevel(uint32_t level) { eventloopNestingLevel = level; }

void ClearInactiveStateStart() { inactiveStateStart = 0; }
void SetInactiveStateStart() {
  if (!inactiveStateStart) {
    inactiveStateStart = GetCurrentTimeForCrashTime();
  }
}

void SetMinidumpAnalysisAllThreads() {
  char* env = strdup("MOZ_CRASHREPORTER_DUMP_ALL_THREADS=1");
  PR_SetEnv(env);
}

nsresult AppendAppNotesToCrashReport(const nsACString& data) {
  if (!GetEnabled()) return NS_ERROR_NOT_INITIALIZED;

  MutexAutoLock lock(*notesFieldLock);
  notesField->Append(data);
  return NS_OK;
}

void SetIncludeContextHeap(bool aValue) {
  sIncludeContextHeap = aValue;

}

bool GetServerURL(nsACString& aServerURL) {
  if (!gExceptionHandler) {
    return false;
  }

  aServerURL = gServerURL;
  return true;
}

nsresult SetServerURL(const nsACString& aServerURL) {
  // Store the server URL as an annotation, the crash reporter client knows how
  // to handle this specially.
  gServerURL = aServerURL;
  return NS_OK;
}

nsresult SetRestartArgs(int argc, char** argv) {
  if (!gExceptionHandler) return NS_OK;

  int i;
  nsAutoCString envVar;
  char* env;
  for (i = 1; i < argc; i++) {
    envVar = "MOZ_CRASHREPORTER_RESTART_ARG_";
    envVar.AppendInt(i);
    envVar += "=";
    envVar += argv[i];

    // PR_SetEnv() wants the string to be available for the lifetime
    // of the app, so dup it here. This conversion is not lossy.
    env = ToNewCString(envVar, mozilla::fallible);
    if (!env) return NS_ERROR_OUT_OF_MEMORY;

    PR_SetEnv(env);
  }

  // make sure the arg list is terminated
  envVar = "MOZ_CRASHREPORTER_RESTART_ARG_";
  envVar.AppendInt(i);
  envVar += "=";

  // PR_SetEnv() wants the string to be available for the lifetime
  // of the app, so dup it here. This conversion is not lossy.
  env = ToNewCString(envVar, mozilla::fallible);
  if (!env) return NS_ERROR_OUT_OF_MEMORY;

  PR_SetEnv(env);

  // make sure we save the info in XUL_APP_FILE for the reporter
  const char* appfile = PR_GetEnv("XUL_APP_FILE");
  if (appfile && *appfile) {
    envVar = "MOZ_CRASHREPORTER_RESTART_XUL_APP_FILE=";
    envVar += appfile;

    // PR_SetEnv() wants the string to be available for the lifetime
    // of the app, so dup it here. This conversion is not lossy.
    env = ToNewCString(envVar);
    PR_SetEnv(env);
  }

  return NS_OK;
}



nsresult AppendObjCExceptionInfoToAppNotes(void* inException) {
  nsAutoCString excString;
  GetObjCExceptionInfo(inException, excString);
  AppendAppNotesToCrashReport(excString);
  return NS_OK;
}

/*
 * Combined code to get/set the crash reporter submission pref on
 * different platforms.
 */
static nsresult PrefSubmitReports(bool* aSubmitReports, bool writePref) {
  nsresult rv;
  /*
   * NOTE! This needs to stay in sync with the code in
   * toolkit/crashreporter/client/app/src/{logic,settings}.rs
   */
  nsCOMPtr<nsIFile> reporterSettings;
  rv = NS_GetSpecialDirectory(XRE_USER_APP_DATA_DIR,
                              getter_AddRefs(reporterSettings));
  NS_ENSURE_SUCCESS(rv, rv);
  reporterSettings->AppendNative("Crash Reports"_ns);
  reporterSettings->AppendNative("crashreporter_settings.json"_ns);

  // On e.g. Linux, std::filesystem requires sometimes linking libstdc++fs,
  // and we don't do that yet, so limit the use of std::filesystem::path where
  // it's really needed, which is Windows, because implicit conversions from
  // wstring in fstream constructors are not supported as of
  // https://cplusplus.github.io/LWG/issue3430.
  std::optional<xpstring> file_path = CreatePathFromFile(reporterSettings);

  if (!file_path) {
    return NS_ERROR_FAILURE;
  }

  Json::Value root;

  bool exists;
  rv = reporterSettings->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!exists) {
    if (!writePref) {
      // If reading the pref, default to true if the settings file doesn't
      // exist.
      *aSubmitReports = true;
      return NS_OK;
    }
    // Create the file so the JSON processor can write to it.
    rv = reporterSettings->Create(nsIFile::NORMAL_FILE_TYPE, 0600);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // Read the root value
    std::ifstream file(*file_path);
    file >> root;
  }

  if (writePref) {
    root["submit_report"] = *aSubmitReports;
    std::ofstream file(*file_path);
    file << root;
  } else if (root["submit_report"].isBool()) {
    *aSubmitReports = root["submit_report"].asBool();
  } else {
    // Default to "true" if the pref can't be found.
    *aSubmitReports = true;
  }

  return NS_OK;
}

nsresult GetSubmitReports(bool* aSubmitReports) {
  return PrefSubmitReports(aSubmitReports, false);
}

nsresult SetSubmitReports(bool aSubmitReports) {
  nsresult rv;

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  if (!obsServ) {
    return NS_ERROR_FAILURE;
  }

  rv = PrefSubmitReports(&aSubmitReports, true);
  if (NS_FAILED(rv)) {
    return rv;
  }

  obsServ->NotifyObservers(nullptr, "submit-reports-pref-changed", nullptr);
  return NS_OK;
}

static void SetCrashEventsDir(nsIFile* aDir) {
  static const XP_CHAR eventsDirectoryEnv[] =
      XP_TEXT("MOZ_CRASHREPORTER_EVENTS_DIRECTORY");

  nsCOMPtr<nsIFile> eventsDir;

  const char* env = PR_GetEnv("CRASHES_EVENTS_DIR");
  if (env && *env) {
    if (NS_SUCCEEDED(NS_NewNativeLocalFile(nsDependentCString(env),
                                           getter_AddRefs(eventsDir)))) {
      EnsureDirectoryExists(eventsDir);
    }
  }

  if (!eventsDir) {
    eventsDir = aDir;
  }

  std::optional<xpstring> path = CreatePathFromFile(eventsDir);
  if (!path) {
    return;  // There's no clean failure from this
  }

  eventsDirectory = *path;
  setenv(eventsDirectoryEnv, path->c_str(), /* overwrite */ 1);
}

void SetProfileDirectory(nsIFile* aDir) {
  // Record the profile directory for use by the crash reporter client.
  {
    nsAutoString path;
    aDir->GetPath(path);
    RecordAnnotationNSString(Annotation::ProfileDirectory, path);
  }

  nsCOMPtr<nsIFile> dir;
  aDir->Clone(getter_AddRefs(dir));

  dir->Append(u"crashes"_ns);
  EnsureDirectoryExists(dir);
  dir->Append(u"events"_ns);
  EnsureDirectoryExists(dir);
  SetCrashEventsDir(dir);
}

static void PopulatePendingDir(nsIFile* aUserAppDataDir) {
  if (!pendingDirectory.empty()) {
    return;
  }

  nsCOMPtr<nsIFile> pendingDir;
  aUserAppDataDir->Clone(getter_AddRefs(pendingDir));
  pendingDir->Append(u"Crash Reports"_ns);
  pendingDir->Append(u"pending"_ns);

  PathString path;
  if (NS_SUCCEEDED(GetNativePathFromFile(pendingDir, path))) {
    pendingDirectory = xpstring(path.get());
  }
}

void SetUserAppDataDirectory(nsIFile* aDir) {
  nsCOMPtr<nsIFile> eventsDir;
  aDir->Clone(getter_AddRefs(eventsDir));

  eventsDir->Append(u"Crash Reports"_ns);
  EnsureDirectoryExists(eventsDir);
  eventsDir->Append(u"events"_ns);
  EnsureDirectoryExists(eventsDir);
  SetCrashEventsDir(eventsDir);

  PopulatePendingDir(aDir);
}

void UpdateCrashEventsDir() {
  const char* env = PR_GetEnv("CRASHES_EVENTS_DIR");
  if (env && *env) {
    SetCrashEventsDir(nullptr);
  }

  nsCOMPtr<nsIFile> eventsDir;
  nsresult rv = NS_GetSpecialDirectory("ProfD", getter_AddRefs(eventsDir));
  if (NS_SUCCEEDED(rv)) {
    SetProfileDirectory(eventsDir);
    return;
  }

  rv = NS_GetSpecialDirectory(XRE_USER_APP_DATA_DIR, getter_AddRefs(eventsDir));
  if (NS_SUCCEEDED(rv)) {
    SetUserAppDataDirectory(eventsDir);
    return;
  }

  NS_WARNING(
      "Couldn't get the user appdata directory. Crash events may not be "
      "produced.");
}

bool GetCrashEventsDir(nsAString& aPath) {
  if (eventsDirectory.empty()) {
    return false;
  }
  aPath = CONVERT_XP_CHAR_TO_UTF16(eventsDirectory.c_str());
  return true;
}

void SetMemoryReportFile(nsIFile* aFile) {
  if (!gExceptionHandler) {
    return;
  }

  PathString path;
  if (NS_SUCCEEDED(GetNativePathFromFile(aFile, path))) {
    memoryReportPath = xpstring(path.get());
  }
}

nsresult GetDefaultMemoryReportFile(nsIFile** aFile) {
  nsCOMPtr<nsIFile> defaultMemoryReportFile;
  if (!defaultMemoryReportPath) {
    nsresult rv = NS_GetSpecialDirectory(
        NS_APP_PROFILE_DIR_STARTUP, getter_AddRefs(defaultMemoryReportFile));
    if (NS_FAILED(rv)) {
      return rv;
    }
    defaultMemoryReportFile->AppendNative("memory-report.json.gz"_ns);
    defaultMemoryReportPath = CreatePathFromFile(defaultMemoryReportFile);
    if (!defaultMemoryReportPath) {
      return NS_ERROR_FAILURE;
    }
  } else {
    CreateFileFromPath(*defaultMemoryReportPath,
                       getter_AddRefs(defaultMemoryReportFile));
    if (!defaultMemoryReportFile) {
      return NS_ERROR_FAILURE;
    }
  }
  defaultMemoryReportFile.forget(aFile);
  return NS_OK;
}

// The "pending" dir is Crash Reports/pending, from which minidumps
// can be submitted. Because this method may be called off the main thread,
// we store the pending directory as a path.
static bool GetPendingDir(nsIFile** dir) {
  if (pendingDirectory.empty()) {
    return false;
  }

  MOZ_ASSERT(!*dir);
  return NS_SUCCEEDED(NS_NewPathStringLocalFile(
      DependentPathString(pendingDirectory.c_str(), pendingDirectory.size()),
      dir));
}

// The "limbo" dir is where minidumps go to wait for something else to
// use them.  If we're |ShouldReport()|, then the "something else" is
// a minidump submitter, and they're coming from the
// Crash Reports/pending/ dir.  Otherwise, we don't know what the
// "somthing else" is, but the minidumps stay in [profile]/minidumps/
// limbo.
static bool GetMinidumpLimboDir(nsIFile** dir) {
  if (ShouldReport()) {
    return GetPendingDir(dir);
  } else {
    CreateFileFromPath(gExceptionHandler->dump_path(), dir);
    return nullptr != *dir;
  }
}

void DeleteMinidumpFilesForID(const nsAString& aId,
                              const Maybe<nsString>& aAdditionalMinidump) {
  nsCOMPtr<nsIFile> minidumpFile;
  if (GetMinidumpForID(aId, getter_AddRefs(minidumpFile))) {
    minidumpFile->Remove(false);
  }

  nsCOMPtr<nsIFile> extraFile;
  if (GetExtraFileForID(aId, getter_AddRefs(extraFile))) {
    extraFile->Remove(false);
  }

  if (aAdditionalMinidump && GetMinidumpForID(aId, getter_AddRefs(minidumpFile),
                                              aAdditionalMinidump)) {
    minidumpFile->Remove(false);
  }
}

bool GetMinidumpForID(const nsAString& id, nsIFile** minidump,
                      const Maybe<nsString>& aAdditionalMinidump) {
  if (!GetMinidumpLimboDir(minidump)) {
    return false;
  }

  nsAutoString fileName(id);

  if (aAdditionalMinidump) {
    fileName.Append('-');
    fileName.Append(*aAdditionalMinidump);
  }

  fileName.Append(u".dmp"_ns);
  (*minidump)->Append(fileName);

  bool exists;
  if (NS_FAILED((*minidump)->Exists(&exists)) || !exists) {
    return false;
  }

  return true;
}

bool GetIDFromMinidump(nsIFile* minidump, nsAString& id) {
  if (minidump && NS_SUCCEEDED(minidump->GetLeafName(id))) {
    id.ReplaceLiteral(id.Length() - 4, 4, u"");
    return true;
  }
  return false;
}

bool GetExtraFileForID(const nsAString& id, nsIFile** extraFile) {
  if (!GetMinidumpLimboDir(extraFile)) {
    return false;
  }

  (*extraFile)->Append(id + u".extra"_ns);

  bool exists;
  if (NS_FAILED((*extraFile)->Exists(&exists)) || !exists) {
    return false;
  }

  return true;
}

bool GetExtraFileForMinidump(nsIFile* minidump, nsIFile** extraFile) {
  nsAutoString leafName;
  nsresult rv = minidump->GetLeafName(leafName);
  if (NS_FAILED(rv)) return false;

  nsCOMPtr<nsIFile> extraF;
  rv = minidump->Clone(getter_AddRefs(extraF));
  if (NS_FAILED(rv)) return false;

  leafName.Replace(leafName.Length() - 3, 3, u"extra"_ns);
  rv = extraF->SetLeafName(leafName);
  if (NS_FAILED(rv)) return false;

  *extraFile = nullptr;
  extraF.swap(*extraFile);
  return true;
}

static nsresult ReadExtraFile(nsCOMPtr<nsIFile>& aFile,
                              AnnotationTable& aAnnotations) {
  const int64_t kExtraFileMaxSize = 1024 * 1024 * 1024;
  int64_t fileSize;

  nsresult rv = aFile->GetFileSize(&fileSize);
  NS_ENSURE_SUCCESS(rv, rv);
  // Reject humongous extra files, Socorro will discard them anyway
  NS_ENSURE_TRUE((fileSize > 0) && (fileSize < kExtraFileMaxSize),
                 NS_ERROR_OUT_OF_MEMORY);
  nsTArray<uint8_t> buffer((size_t)rv);

  nsCOMPtr<nsIInputStream> stream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(stream), aFile);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString json;
  rv = NS_ReadInputStreamToString(stream, json, fileSize);
  NS_ENSURE_SUCCESS(rv, rv);
  auto annotations = ExtraFileParser::Parse(json);

  if (!annotations) {
    return NS_ERROR_FAILURE;
  }

  aAnnotations = *annotations;
  return NS_OK;
}

static bool WriteExtraFile(PlatformWriter& pw,
                           const AnnotationTable& aAnnotations) {
  if (!pw.Valid()) {
    return false;
  }

  JSONAnnotationWriter writer(pw);
  WriteAnnotations(writer, aAnnotations);
  WriteSynthesizedAnnotations(writer);

  return true;
}

bool WriteExtraFile(const nsAString& id, const AnnotationTable& annotations) {
  nsCOMPtr<nsIFile> extra;
  if (!GetMinidumpLimboDir(getter_AddRefs(extra))) {
    return false;
  }

  extra->Append(id + u".extra"_ns);
  PathString path;
  NS_ENSURE_SUCCESS(GetNativePathFromFile(extra, path), false);

  PlatformWriter pw(path.get());
  return WriteExtraFile(pw, annotations);
}

// It really only makes sense to call this function when
// ShouldReport() is true.
// Uses dumpFile's filename to generate memoryReport's filename (same name
// with a different extension)
static bool MoveToPending(nsIFile* dumpFile, nsIFile* extraFile,
                          nsIFile* memoryReport) {
  nsCOMPtr<nsIFile> pendingDir;
  if (!GetPendingDir(getter_AddRefs(pendingDir))) {
    return false;
  }

  if (NS_FAILED(dumpFile->MoveTo(pendingDir, u""_ns))) {
    return false;
  }

  if (extraFile && NS_FAILED(extraFile->MoveTo(pendingDir, u""_ns))) {
    return false;
  }

  if (memoryReport) {
    nsAutoString leafName;
    nsresult rv = dumpFile->GetLeafName(leafName);
    if (NS_FAILED(rv)) {
      return false;
    }
    // Generate the correct memory report filename from the dumpFile's name
    leafName.Replace(
        leafName.Length() - 4, 4,
        static_cast<nsString>(CONVERT_XP_CHAR_TO_UTF16(memoryReportExtension)));
    if (NS_FAILED(memoryReport->MoveTo(pendingDir, leafName))) {
      return false;
    }
  }

  return true;
}

nsresult OOPInit(nsIFile* aXREDirectory, bool force /*=false*/) {
  // Android is exempt from early return because the helper has already been
  // started and is unconditionally expecting a rendezvous.
  if (!CrashReporterIsEnabled(force)) {
    return NS_OK;
  }

  {
    // It's already started, no work to do here!
    StaticMutexAutoLock lock(gCrashHelperClientMutex);
    if (gCrashHelperClient) {
      return NS_OK;
    }
  }

  CrashHelperClient* crashHelperClient;

  PathString tempPath;
  if (!BuildTempPath(tempPath)) {
    return NS_ERROR_FAILURE;
  }

  // Locate the crash helper executable
  PathString crashHelperPath_temp;
  nsresult rv = LocateExecutable(aXREDirectory, CRASH_HELPER_FILENAME,
                                 crashHelperPath_temp);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  crashHelperPath = crashHelperPath_temp.get();

  childCrashNotifyPipe = nsCString("gecko-crash-server-pipe.");
  childCrashNotifyPipe.AppendInt(static_cast<int>(getpid()));

  crashHelperClient = crash_helper_launch(
      crashHelperPath.c_str(), (BreakpadRawData)childCrashNotifyPipe.get(),
      tempPath.get(), gToolkitBuildID);
  if (!crashHelperClient) {
    return NS_ERROR_FAILURE;
  }

  StaticMutexAutoLock lock(gCrashHelperClientMutex);
  gCrashHelperClient = crashHelperClient;
  return NS_OK;
}

void OOPDeinit() {
  childCrashNotifyPipe = ""_ns;

  StaticMutexAutoLock lock(gCrashHelperClientMutex);
  if (gCrashHelperClient) {
    crash_helper_shutdown(gCrashHelperClient);
    gCrashHelperClient = nullptr;
  }
}

uint32_t GetCrashHelperPid() {
  StaticMutexAutoLock lock(gCrashHelperClientMutex);
  if (!gCrashHelperClient) {
    return 0;
  }
  return static_cast<uint32_t>(crash_helper_pid(gCrashHelperClient));
}

// Parent-side API for children

using CrashPipeType = const char*;

static CrashPipeType GetChildNotificationPipe() {
  return childCrashNotifyPipe.get();
}

bool RegisterChildIPCChannel(mozilla::geckoargs::ChildProcessArgs& aArgs,
                             GeckoChildID aID) {
  StaticMutexAutoLock lock(gCrashHelperClientMutex);
  if (gCrashHelperClient) {
    RawIPCConnector connector = {};
    if (!register_child_ipc_channel(gCrashHelperClient, aID, &connector)) {
      return false;
    }

    geckoargs::sCrashHelperPid.Put(crash_helper_pid(gCrashHelperClient), aArgs);

    UniqueMachSendRight send_right{connector.send};
    UniqueMachReceiveRight recv_right{connector.recv};

    if (!send_right || !recv_right) {
      return false;
    }

    geckoargs::sCrashHelperSend.Put(std::move(send_right), aArgs);
    geckoargs::sCrashHelperRecv.Put(std::move(recv_right), aArgs);

    auto childNotificationPipe = CrashReporter::GetChildNotificationPipe();
    geckoargs::sCrashReporter.Put(childNotificationPipe, aArgs);

    return true;
  }

  return false;
}


bool SetRemoteExceptionHandler(int& aArgc, char** aArgv) {
  MOZ_ASSERT(!gExceptionHandler, "crash client already init'd");
  auto crash_pipe = geckoargs::sCrashReporter.Get(aArgc, aArgv).extract();

  auto send_right = geckoargs::sCrashHelperSend.Get(aArgc, aArgv).extract();
  auto recv_right = geckoargs::sCrashHelperRecv.Get(aArgc, aArgv).extract();

  RawIPCConnector raw_connector = {
      .send = send_right.release(),
      .recv = recv_right.release(),
  };

  auto pid_arg = geckoargs::sCrashHelperPid.Get(aArgc, aArgv);
  Pid pid = static_cast<Pid>(pid_arg.valueOr(0));

  crash_helper_rendezvous(raw_connector, GetGeckoChildID(),
                          pid_arg.isSome() ? &pid : nullptr);
  RegisterRuntimeExceptionModule();
  InitializeAppNotes();
  RegisterAnnotations();

#ifdef MOZ_PHC
  // HACK: We're using the PHCBaseAddress annotation to point to the actual
  // PHC address information object. This is because we currently have no
  // difference between the internal representation of annotations and their
  // external representation. Once we remove the old annotation API this
  // will be properly addressed.
  mozannotation_register_bytebuffer(
      static_cast<uint32_t>(Annotation::PHCBaseAddress),
      &mozilla::phc::gAddrInfo, sizeof(mozilla::phc::gAddrInfo));
#endif
  gExceptionHandler =
      new google_breakpad::ExceptionHandler("", ChildFilter,
                                            nullptr,  // no callback
                                            nullptr,  // no callback context
                                            true,     // install signal handlers
                                            crash_pipe);

  oldTerminateHandler = std::set_terminate(&TerminateHandler);

  // If we didn't fail earlier because of a missing IPC channel then all of the
  // above should have succeeded.
  MOZ_ASSERT(gExceptionHandler->IsOutOfProcess(),
             "Should have been able to set remote exception handler");

  return gExceptionHandler->IsOutOfProcess();
}  // namespace CrashReporter

bool TakeMinidumpForChild(GeckoChildID aChildId, nsIFile** dump,
                          AnnotationTable& aAnnotations) {
  if (!GetEnabled()) {
    return false;
  }

  CrashReport* crash_report = nullptr;

  {
    StaticMutexAutoLock lock(gCrashHelperClientMutex);
    if (gCrashHelperClient) {
      crash_report = transfer_crash_report(gCrashHelperClient, aChildId);
    }
  }

  if (!crash_report) {
    return false;
  }

  CreateFileFromPath(xpstring((XP_CHAR*)crash_report->path), dump);
  nsCString error =
      crash_report->error ? nsCString(crash_report->error) : ""_ns;
  release_crash_report(crash_report);

  nsCOMPtr<nsIFile> extra = nullptr;
  NS_ENSURE_TRUE(GetExtraFileForMinidump(*dump, getter_AddRefs(extra)), false);

  if (ShouldReport()) {
    nsCOMPtr<nsIFile> memoryReport;
    if (!memoryReportPath.empty()) {
      CreateFileFromPath(memoryReportPath, getter_AddRefs(memoryReport));
      MOZ_ASSERT(memoryReport);
    }

    MoveToPending(*dump, extra, memoryReport);
  }

  nsresult rv = ReadExtraFile(extra, aAnnotations);

  // Unconditionally remove the temporary .extra file, it will be regenerated
  // later when we finalize the crash report.
  extra->Remove(false);

  if (rv != NS_OK) {
    // TODO: We failed to read the annotations, this will leave an orphaned
    // crash that we won't be able to submit. Clean everything up instead?
    return false;
  }

  AddCommonAnnotations(aAnnotations);

  if (error.Length() > 0) {
    aAnnotations[Annotation::DumperError] = std::move(error);
  }

  return true;
}

bool FinalizeOrphanedMinidump(GeckoChildID aChildId, GeckoProcessType aType,
                              nsString* aDumpId) {
  AnnotationTable annotations;
  nsCOMPtr<nsIFile> minidump;

  if (!TakeMinidumpForChild(aChildId, getter_AddRefs(minidump), annotations)) {
    return false;
  }

  nsAutoString id;
  if (!GetIDFromMinidump(minidump, id)) {
    return false;
  }

  if (aDumpId) {
    *aDumpId = id;
  }

  annotations[Annotation::ProcessType] =
      XRE_ChildProcessTypeToAnnotation(aType);

  return WriteExtraFile(id, annotations);
}

//-----------------------------------------------------------------------------
// CreateMinidumpsAndPair() and helpers
//

/*
 * Renames the stand alone dump file aDumpFile to:
 *  |aOwnerDumpFile-aDumpFileProcessType.dmp|
 * and moves it into the same directory as aOwnerDumpFile. Does not
 * modify aOwnerDumpFile in any way.
 *
 * @param aDumpFile - the dump file to associate with aOwnerDumpFile.
 * @param aOwnerDumpFile - the new owner of aDumpFile.
 * @param aDumpFileProcessType - process name associated with aDumpFile.
 */
static void RenameAdditionalHangMinidump(nsIFile* minidump,
                                         nsIFile* childMinidump,
                                         const nsACString& name) {
  nsCOMPtr<nsIFile> directory;
  childMinidump->GetParent(getter_AddRefs(directory));
  if (!directory) return;

  nsAutoCString leafName;
  childMinidump->GetNativeLeafName(leafName);

  // turn "<id>.dmp" into "<id>-<name>.dmp
  leafName.Insert("-"_ns + name, leafName.Length() - 4);

  if (NS_FAILED(minidump->MoveToNative(directory, leafName))) {
    NS_WARNING("RenameAdditionalHangMinidump failed to move minidump.");
  }
}

ThreadId CurrentThreadId() {
  // Note that this will leak the mach port unless it's explicitly closed or
  // assigned to a RAII type such as `UniqueMachSendRight`.
  return mach_thread_self();
}

bool CreateMinidumpsAndPair(GeckoChildID aId, ThreadId aTargetBlamedThread,
                            const nsACString& aIncomingPairName,
                            AnnotationTable& aTargetAnnotations,
                            nsIFile** aMainDumpOut) {
  if (!GetEnabled()) {
    return false;
  }

  AutoIOInterposerDisable disableIOInterposition;

  CrashReport* crash_report = nullptr;

  {
    StaticMutexAutoLock lock(gCrashHelperClientMutex);
    if (gCrashHelperClient) {
      // We need to make a copy of this right as the Rust code will take
      // ownership of it (and eventually dispose of the right).
      aTargetBlamedThread = RetainMachSendRight(aTargetBlamedThread).release();
      crash_report =
          generate_crash_report(gCrashHelperClient, aId, aTargetBlamedThread);
    }
  }

  if (!crash_report) {
    return false;
  }

  nsCOMPtr<nsIFile> targetMinidump;
  CreateFileFromPath(xpstring((XP_CHAR*)crash_report->path),
                     getter_AddRefs(targetMinidump));
  nsCString error =
      crash_report->error ? nsCString(crash_report->error) : ""_ns;
  release_crash_report(crash_report);
  MOZ_ASSERT(targetMinidump);

  nsCOMPtr<nsIFile> extra = nullptr;
  NS_ENSURE_TRUE(GetExtraFileForMinidump(targetMinidump, getter_AddRefs(extra)),
                 false);

  // Create a dump of the main process.
  {
    StaticMutexAutoLock lock(gCrashHelperClientMutex);
    crash_report =
        generate_crash_report(gCrashHelperClient, 0, CurrentThreadId());
  }

  if (!crash_report) {
    // We're leaving behind a minidump, clean it up?
    return false;
  }

  nsCOMPtr<nsIFile> incomingDump;
  CreateFileFromPath(xpstring((XP_CHAR*)crash_report->path),
                     getter_AddRefs(incomingDump));
  release_crash_report(crash_report);
  MOZ_ASSERT(incomingDump);

  // We're ignoring the errors we might have encountered while dumping the
  // parent, they're not really important in this context and we don't need
  // the crash annotations either.
  nsCOMPtr<nsIFile> incomingExtra = nullptr;
  if (GetExtraFileForMinidump(incomingDump, getter_AddRefs(incomingExtra))) {
    incomingExtra->Remove(false);
  }

  RenameAdditionalHangMinidump(incomingDump, targetMinidump, aIncomingPairName);

  if (ShouldReport()) {
    MoveToPending(targetMinidump, nullptr, nullptr);
    MoveToPending(incomingDump, nullptr, nullptr);
  }

  nsresult rv = ReadExtraFile(extra, aTargetAnnotations);

  // Unconditionally remove the temporary .extra file, it will be regenerated
  // later when we finalize the crash report.
  extra->Remove(false);

  if (rv != NS_OK) {
    // TODO: We failed to read the annotations, this will leave an orphaned
    // crash that we won't be able to submit. Clean everything up instead?
    return false;
  }

  AddCommonAnnotations(aTargetAnnotations);

  if (error.Length() > 0) {
    aTargetAnnotations[Annotation::DumperError] = std::move(error);
  }

  targetMinidump.forget(aMainDumpOut);

  return true;
}

bool UnsetRemoteExceptionHandler(bool wasSet) {
  // On Linux we don't unset breakpad's exception handler if the sandbox is
  // enabled because it requires invoking `sigaltstack` and we don't want to
  // allow that syscall in the sandbox. See bug 1622452.
  if (wasSet) {
    std::set_terminate(oldTerminateHandler);
    delete gExceptionHandler;
    gExceptionHandler = nullptr;
  }
  gServerURL = "";
  TeardownAppNotes();

  return true;
}


}  // namespace CrashReporter
