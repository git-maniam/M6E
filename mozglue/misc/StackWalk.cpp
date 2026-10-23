/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* API for getting a stack trace of the C/C++ stack on the current thread */

#include "mozilla/Array.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/StackWalk.h"
#  include <unistd.h>
#include "mozilla/Sprintf.h"

#include <string.h>


using namespace mozilla;

// for _Unwind_Backtrace from libcxxrt or libunwind
// cxxabi.h from libcxxrt implicitly includes unwind.h first
#if defined(HAVE__UNWIND_BACKTRACE) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#  include <dlfcn.h>

#if (defined(XP_DARWIN) && \
     (defined(__i386) || defined(__ppc__) || defined(HAVE__UNWIND_BACKTRACE)))
#  define MOZ_STACKWALK_SUPPORTS_MACOSX 1
#else
#  define MOZ_STACKWALK_SUPPORTS_MACOSX 0
#endif

#  define HAVE___LIBC_STACK_END 0

#if (defined(linux) &&                                            \
     ((defined(__GNUC__) && (defined(__i386) || defined(PPC))) || \
      defined(HAVE__UNWIND_BACKTRACE)) &&                         \
     (HAVE___LIBC_STACK_END || ANDROID))
#  define MOZ_STACKWALK_SUPPORTS_LINUX 1
#else
#  define MOZ_STACKWALK_SUPPORTS_LINUX 0
#endif

#if HAVE___LIBC_STACK_END
extern MOZ_EXPORT void* __libc_stack_end;  // from ld-linux.so
#  ifdef __aarch64__
static Atomic<uintptr_t> ldso_base;
#  endif
#endif


class FrameSkipper {
 public:
  constexpr FrameSkipper() : mSkipUntilAddr(0) {}
  static uintptr_t AddressFromPC(const void* aPC) {
#ifdef __arm__
    // On 32-bit ARM, mask off the thumb bit to get the instruction address.
    return uintptr_t(aPC) & ~1;
#else
    return uintptr_t(aPC);
#endif
  }
  bool ShouldSkipPC(void* aPC) {
    // Skip frames until we encounter the one we were initialized with,
    // and then never skip again.
    uintptr_t instructionAddress = AddressFromPC(aPC);
    if (mSkipUntilAddr != 0) {
      if (mSkipUntilAddr != instructionAddress) {
        return true;
      }
      mSkipUntilAddr = 0;
    }
    return false;
  }
  explicit FrameSkipper(const void* aPC) : mSkipUntilAddr(AddressFromPC(aPC)) {}

 private:
  uintptr_t mSkipUntilAddr;
};

#if   HAVE_DLADDR &&                                           \
    (HAVE__UNWIND_BACKTRACE || MOZ_STACKWALK_SUPPORTS_LINUX || \
     MOZ_STACKWALK_SUPPORTS_MACOSX)

#  include <stdlib.h>
#  include <stdio.h>

// On glibc 2.1, the Dl_info api defined in <dlfcn.h> is only exposed
// if __USE_GNU is defined.  I suppose its some kind of standards
// adherence thing.
//
#  if (__GLIBC_MINOR__ >= 1) && !defined(__USE_GNU)
#    define __USE_GNU
#  endif

// This thing is exported by libstdc++
// Yes, this is a gcc only hack
#  if defined(MOZ_DEMANGLE_SYMBOLS)
#    include <cxxabi.h>
#  endif  // MOZ_DEMANGLE_SYMBOLS

namespace mozilla {

void DemangleSymbol(const char* aSymbol, char* aBuffer, int aBufLen) {
  aBuffer[0] = '\0';

#  if defined(MOZ_DEMANGLE_SYMBOLS)
  /* See demangle.h in the gcc source for the voodoo */
  char* demangled = abi::__cxa_demangle(aSymbol, nullptr, nullptr, nullptr);

  if (demangled) {
    strncpy(aBuffer, demangled, aBufLen);
    aBuffer[aBufLen - 1] = '\0';
    free(demangled);
  }
#  endif  // MOZ_DEMANGLE_SYMBOLS
}

}  // namespace mozilla

// {x86, ppc} x {Linux, Mac} stackwalking code.
//
// Changes to to OS/Architecture support here should be reflected in
// build/moz.configure/memory.configure
#  if ((defined(__i386) || defined(PPC) || defined(__ppc__)) && \
       (MOZ_STACKWALK_SUPPORTS_MACOSX || MOZ_STACKWALK_SUPPORTS_LINUX))

static void DoFramePointerStackWalk(MozWalkStackCallback aCallback,
                                    const void* aFirstFramePC,
                                    uint32_t aMaxFrames, void* aClosure,
                                    void** aBp, void* aStackEnd);

MFBT_API void MozStackWalk(MozWalkStackCallback aCallback,
                           const void* aFirstFramePC, uint32_t aMaxFrames,
                           void* aClosure) {
  // Get the frame pointer
  void** bp = (void**)__builtin_frame_address(0);

  void* stackEnd;
#    if HAVE___LIBC_STACK_END
  stackEnd = __libc_stack_end;
#    else
  stackEnd = pthread_get_stackaddr_np(pthread_self());
#    endif
  DoFramePointerStackWalk(aCallback, aFirstFramePC, aMaxFrames, aClosure, bp,
                          stackEnd);
}

#  elif defined(HAVE__UNWIND_BACKTRACE)

// libgcc_s.so symbols _Unwind_Backtrace@@GCC_3.3 and _Unwind_GetIP@@GCC_3.0
#    include <unwind.h>

struct unwind_info {
  MozWalkStackCallback callback;
  FrameSkipper skipper;
  int maxFrames;
  int numFrames;
  void* closure;
};

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context* context,
                                           void* closure) {
  _Unwind_Reason_Code ret = _URC_NO_REASON;
  unwind_info* info = static_cast<unwind_info*>(closure);
  void* pc = reinterpret_cast<void*>(_Unwind_GetIP(context));
#    if HAVE___LIBC_STACK_END && defined(__aarch64__)
  // Work around https://sourceware.org/bugzilla/show_bug.cgi?id=32612
  // The _dl_tlsdesc_dynamic function can't be unwound through with
  // _Unwind_Backtrace when glibc is built with aarch64 PAC (that leads
  // to a crash).
  // Unfortunately, we can't get the address of that specific function, so
  // we just disallow all of ld-linux-aarch64.so.1: when we hit an address
  // in there, we make _Unwind_Backtrace stop.
  // In the case of _dl_tlsdesc_dynamic, this would stop the stackwalk at
  // tls_get_addr_tail, which is enough information to know the stack comes
  // from ld.so, and we even get inlining info giving us malloc,
  // allocate_dtv_entry and allocate_and_init, which is plenty enough and
  // better than nothing^Hcrashing.
  // To figure out whether the frame falls into ld-linux-aarch64.so.1, we
  // use __libc_stack_end (which lives there and is .data) as upper bound
  // (assuming .data comes after .text), and get the base address of the
  // library via dladdr.
  if (!ldso_base) {
    Dl_info info;
    dladdr(&__libc_stack_end, &info);
    ldso_base = (uintptr_t)info.dli_fbase;
  }
  if (ldso_base && ((uintptr_t)pc > ldso_base) &&
      (uintptr_t)pc < (uintptr_t)&__libc_stack_end) {
    // Any error code will do, we just want to stop the walk even when
    // we haven't reached the limit.
    ret = _URC_FOREIGN_EXCEPTION_CAUGHT;
  }
#    endif
  // TODO Use something like '_Unwind_GetGR()' to get the stack pointer.
  if (!info->skipper.ShouldSkipPC(pc)) {
    info->numFrames++;
    (*info->callback)(info->numFrames, pc, nullptr, info->closure);
    if (info->maxFrames != 0 && info->numFrames == info->maxFrames) {
      // Again, any error code that stops the walk will do.
      return _URC_FOREIGN_EXCEPTION_CAUGHT;
    }
  }
  return ret;
}

MFBT_API void MozStackWalk(MozWalkStackCallback aCallback,
                           const void* aFirstFramePC, uint32_t aMaxFrames,
                           void* aClosure) {
  unwind_info info;
  info.callback = aCallback;
  info.skipper = FrameSkipper(aFirstFramePC ? aFirstFramePC : CallerPC());
  info.maxFrames = aMaxFrames;
  info.numFrames = 0;
  info.closure = aClosure;

  // We ignore the return value from _Unwind_Backtrace. There are three main
  // reasons for this.
  // - On ARM/Android bionic's _Unwind_Backtrace usually (always?) returns
  //   _URC_FAILURE.  See
  //   https://bugzilla.mozilla.org/show_bug.cgi?id=717853#c110.
  // - If aMaxFrames != 0, we want to stop early, and the only way to do that
  //   is to make unwind_callback return something other than _URC_NO_REASON,
  //   which causes _Unwind_Backtrace to return a non-success code.
  // - MozStackWalk doesn't have a return value anyway.
  (void)_Unwind_Backtrace(unwind_callback, &info);
}

#  endif

bool MFBT_API MozDescribeCodeAddress(void* aPC,
                                     MozCodeAddressDetails* aDetails) {
  aDetails->library[0] = '\0';
  aDetails->loffset = 0;
  aDetails->filename[0] = '\0';
  aDetails->lineno = 0;
  aDetails->function[0] = '\0';
  aDetails->foffset = 0;

  Dl_info info;

  int ok = dladdr(aPC, &info);

  if (!ok) {
    return true;
  }

  strncpy(aDetails->library, info.dli_fname, sizeof(aDetails->library));
  aDetails->library[std::size(aDetails->library) - 1] = '\0';
  aDetails->loffset = (char*)aPC - (char*)info.dli_fbase;

  // On FreeBSD, dli_sname is unusably bad, it often returns things like
  // 'gtk_xtbin_new' or 'XRE_GetBootstrap' instead of long C++ symbols. Just let
  // GetFunction do the lookup directly in the ELF image.

  const char* symbol = info.dli_sname;
  if (!symbol || symbol[0] == '\0') {
    return true;
  }

  DemangleSymbol(symbol, aDetails->function, sizeof(aDetails->function));

  if (aDetails->function[0] == '\0') {
    // Just use the mangled symbol if demangling failed.
    strncpy(aDetails->function, symbol, sizeof(aDetails->function));
    aDetails->function[std::size(aDetails->function) - 1] = '\0';
  }

  aDetails->foffset = (char*)aPC - (char*)info.dli_saddr;

  return true;
}

#else  // unsupported platform.

MFBT_API void MozStackWalk(MozWalkStackCallback aCallback,
                           const void* aFirstFramePC, uint32_t aMaxFrames,
                           void* aClosure) {}

MFBT_API bool MozDescribeCodeAddress(void* aPC,
                                     MozCodeAddressDetails* aDetails) {
  aDetails->library[0] = '\0';
  aDetails->loffset = 0;
  aDetails->filename[0] = '\0';
  aDetails->lineno = 0;
  aDetails->function[0] = '\0';
  aDetails->foffset = 0;
  return false;
}

#endif


#  if defined(XP_MACOSX) && defined(__aarch64__)
// On macOS arm64, system libraries are arm64e binaries, and arm64e can do
// pointer authentication: The low bits of the pointer are the actual pointer
// value, and the high bits are an encrypted hash. During stackwalking, we need
// to strip off this hash. In theory, ptrauth_strip would be the right function
// to call for this. However, that function is a no-op unless it's called from
// code which also builds as arm64e - which we do not. So we cannot use it. So
// for now, we hardcode a mask that seems to work today: 40 bits for the pointer
// and 24 bits for the hash seems to do the trick. We can worry about
// dynamically computing the correct mask if this ever stops working.
const uintptr_t kPointerMask =
    (uintptr_t(1) << 40) - 1;  // 40 bits pointer, 24 bit PAC
#  else
const uintptr_t kPointerMask = ~uintptr_t(0);
#  endif

MOZ_ASAN_IGNORE
static void DoFramePointerStackWalk(MozWalkStackCallback aCallback,
                                    const void* aFirstFramePC,
                                    uint32_t aMaxFrames, void* aClosure,
                                    void** aBp, void* aStackEnd) {
  // Stack walking code courtesy Kipp's "leaky".

  FrameSkipper skipper(aFirstFramePC);
  uint32_t numFrames = 0;

  // Sanitize the given aBp. Assume that something reasonably close to
  // but before the stack end is going be a valid frame pointer. Also
  // check that it is an aligned address. This increases the chances
  // that if the pointer is not valid (which might happen if the caller
  // called __builtin_frame_address(1) and its frame is busted for some
  // reason), we won't read it, leading to a crash. Because the calling
  // code is not using frame pointers when returning, it might actually
  // recover just fine.
  static const uintptr_t kMaxStackSize = 8 * 1024 * 1024;
  if (uintptr_t(aBp) < uintptr_t(aStackEnd) -
                           std::min(kMaxStackSize, uintptr_t(aStackEnd)) ||
      aBp >= aStackEnd || (uintptr_t(aBp) & (sizeof(void*) - 1))) {
    return;
  }

  while (aBp) {
    void** next = (void**)*aBp;
    // aBp may not be a valid frame pointer if code was compiled with
    // -fomit-frame-pointer, so do some sanity checks.
    // We don't need to check against the beginning of the stack because
    // we can assume that aBp > sp
    if (next <= aBp || next >= aStackEnd ||
        (uintptr_t(next) & (sizeof(void*) - 1))) {
      break;
    }
#  if (defined(__ppc__) && defined(XP_MACOSX)) || defined(__powerpc64__)
    // ppc mac or powerpc64 linux
    void* pc = *(aBp + 2);
    aBp += 3;
#  else  // i386, x86_64, aarch64, powerpc32, etc.
    void* pc = *(aBp + 1);
    aBp += 2;
#  endif

    // Strip off pointer authentication hash, if present. For now, it looks
    // like only return addresses require stripping, and stack pointers do
    // not. This might change in the future.
    pc = (void*)((uintptr_t)pc & kPointerMask);

    if (!skipper.ShouldSkipPC(pc)) {
      // Assume that the SP points to the BP of the function
      // it called. We can't know the exact location of the SP
      // but this should be sufficient for our use the SP
      // to order elements on the stack.
      numFrames++;
      (*aCallback)(numFrames, pc, aBp, aClosure);
      if (aMaxFrames != 0 && numFrames == aMaxFrames) {
        break;
      }
    }
    aBp = next;
  }
}

namespace mozilla {

MFBT_API void FramePointerStackWalk(MozWalkStackCallback aCallback,
                                    uint32_t aMaxFrames, void* aClosure,
                                    void** aBp, void* aStackEnd) {
  // We don't pass a aFirstFramePC because we start walking the stack from the
  // frame at aBp.
  DoFramePointerStackWalk(aCallback, nullptr, aMaxFrames, aClosure, aBp,
                          aStackEnd);
}

}  // namespace mozilla


MFBT_API int MozFormatCodeAddressDetails(
    char* aBuffer, uint32_t aBufferSize, uint32_t aFrameNumber, void* aPC,
    const MozCodeAddressDetails* aDetails) {
  return MozFormatCodeAddress(aBuffer, aBufferSize, aFrameNumber, aPC,
                              aDetails->function, aDetails->library,
                              aDetails->loffset, aDetails->filename,
                              aDetails->lineno);
}

MFBT_API int MozFormatCodeAddress(char* aBuffer, uint32_t aBufferSize,
                                  uint32_t aFrameNumber, const void* aPC,
                                  const char* aFunction, const char* aLibrary,
                                  ptrdiff_t aLOffset, const char* aFileName,
                                  uint32_t aLineNo) {
  const char* function = aFunction && aFunction[0] ? aFunction : "???";
  if (aFileName && aFileName[0]) {
    // We have a filename and (presumably) a line number. Use them.
    return SprintfBuf(aBuffer, aBufferSize, "#%02u: %s (%s:%u)", aFrameNumber,
                      function, aFileName, aLineNo);
  } else if (aLibrary && aLibrary[0]) {
    // We have no filename, but we do have a library name. Use it and the
    // library offset, and print them in a way that `fix_stacks.py` can
    // post-process.
    return SprintfBuf(aBuffer, aBufferSize, "#%02u: %s[%s +0x%" PRIxPTR "]",
                      aFrameNumber, function, aLibrary,
                      static_cast<uintptr_t>(aLOffset));
  } else {
    // We have nothing useful to go on. (The format string is split because
    // '??)' is a trigraph and causes a warning, sigh.)
    return SprintfBuf(aBuffer, aBufferSize,
                      "#%02u: ??? (???:???"
                      ")",
                      aFrameNumber);
  }
}

static void EnsureWrite(FILE* aStream, const char* aBuf, size_t aLen) {
  int fd = fileno(aStream);
  while (aLen > 0) {
    auto written = write(fd, aBuf, aLen);
    if (written <= 0 || size_t(written) > aLen) {
      break;
    }
    aBuf += written;
    aLen -= written;
  }
}

template <int N>
static int PrintStackFrameBuf(char (&aBuf)[N], uint32_t aFrameNumber, void* aPC,
                              void* aSP) {
  MozCodeAddressDetails details;
  MozDescribeCodeAddress(aPC, &details);
  int len =
      MozFormatCodeAddressDetails(aBuf, N - 1, aFrameNumber, aPC, &details);
  len = std::min(len, N - 2);
  aBuf[len++] = '\n';
  aBuf[len] = '\0';
  return len;
}

static void PrintStackFrame(uint32_t aFrameNumber, void* aPC, void* aSP,
                            void* aClosure) {
  FILE* stream = (FILE*)aClosure;
  char buf[1025];  // 1024 + 1 for trailing '\n'
  int len = PrintStackFrameBuf(buf, aFrameNumber, aPC, aSP);
  fflush(stream);
  EnsureWrite(stream, buf, len);
}

static bool WalkTheStackEnabled() {
  static bool result = [] {
    char* value = getenv("MOZ_DISABLE_WALKTHESTACK");
    return !(value && value[0]);
  }();
  return result;
}

MFBT_API void MozWalkTheStack(FILE* aStream, const void* aFirstFramePC,
                              uint32_t aMaxFrames) {
  if (WalkTheStackEnabled()) {
    MozStackWalk(PrintStackFrame, aFirstFramePC ? aFirstFramePC : CallerPC(),
                 aMaxFrames, aStream);
  }
}

static void WriteStackFrame(uint32_t aFrameNumber, void* aPC, void* aSP,
                            void* aClosure) {
  auto writer = (void (*)(const char*))aClosure;
  char buf[1024];
  PrintStackFrameBuf(buf, aFrameNumber, aPC, aSP);
  writer(buf);
}

MFBT_API void MozWalkTheStackWithWriter(void (*aWriter)(const char*),
                                        const void* aFirstFramePC,
                                        uint32_t aMaxFrames) {
  if (WalkTheStackEnabled()) {
    MozStackWalk(WriteStackFrame, aFirstFramePC ? aFirstFramePC : CallerPC(),
                 aMaxFrames, (void*)aWriter);
  }
}
