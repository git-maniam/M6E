/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module is supposed to abstract signal handling away from the other
 * platforms that do not support it.
 */

#include "nsSigHandlers.h"


#  include <signal.h>
#  include <stdio.h>
#  include <string.h>
#  include "prthread.h"
#  include "prenv.h"
#  include "nsDebug.h"
#  include "nsString.h"
#  include "nsXULAppAPI.h"
#  include "nsCharSeparatedTokenizer.h"

#  if defined(LINUX)
#    include <sys/time.h>
#    include <sys/resource.h>
#    include <unistd.h>
#    include <stdlib.h>  // atoi
#    include <sys/prctl.h>
#      include <ucontext.h>
#  endif

#  if defined(SOLARIS)
#    include <sys/resource.h>
#    include <ucontext.h>
#  endif



// Note: some tests manipulate this value.
unsigned int _gdb_sleep_duration = 300;

#  if defined(LINUX) && !defined(ANDROID) && defined(DEBUG) && \
      (defined(__i386) || defined(__x86_64) || defined(PPC))
#    define CRAWL_STACK_ON_SIGSEGV
#  endif

#  ifndef PR_SET_PTRACER
#    define PR_SET_PTRACER 0x59616d61
#  endif
#  ifndef PR_SET_PTRACER_ANY
#    define PR_SET_PTRACER_ANY ((unsigned long)-1)
#  endif

#  if defined(CRAWL_STACK_ON_SIGSEGV)

#    include <unistd.h>
#    include "nsISupportsUtils.h"
#    include "mozilla/Attributes.h"
#    include "mozilla/StackWalk.h"

static const char* gProgname = "huh?";

// NB: keep me up to date with the same variable in
// ipc/chromium/chrome/common/ipc_channel_posix.cc
static const int kClientChannelFd = 3;

extern "C" {

static void PrintStackFrame(uint32_t aFrameNumber, void* aPC, void* aSP,
                            void* aClosure) {
  char buf[1024];
  MozCodeAddressDetails details;

  MozDescribeCodeAddress(aPC, &details);
  MozFormatCodeAddressDetails(buf, sizeof(buf), aFrameNumber, aPC, &details);
  fprintf(stdout, "%s\n", buf);
  fflush(stdout);
}
}

void common_crap_handler(int signum, const void* aFirstFramePC) {
  printf("\nProgram %s (pid = %d) received signal %d.\n", gProgname, getpid(),
         signum);

  printf("Stack:\n");
  MozStackWalk(PrintStackFrame, aFirstFramePC, /* maxFrames */ 0, nullptr);

  printf("Sleeping for %d seconds.\n", _gdb_sleep_duration);
  printf("Type 'gdb %s %d' to attach your debugger to this thread.\n",
         gProgname, getpid());

  // Allow us to be ptraced by gdb on Linux with Yama restrictions enabled.
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);

  sleep(_gdb_sleep_duration);

  printf("Done sleeping...\n");

  _exit(signum);
}

MOZ_NEVER_INLINE void ah_crap_handler(int signum) {
  common_crap_handler(signum, CallerPC());
}

MOZ_NEVER_INLINE void child_ah_crap_handler(int signum) {
  if (!getenv("MOZ_DONT_UNBLOCK_PARENT_ON_CHILD_CRASH"))
    close(kClientChannelFd);
  common_crap_handler(signum, CallerPC());
}

#  endif  // CRAWL_STACK_ON_SIGSEGV



#  ifdef SA_SIGINFO
static void fpehandler(int signum, siginfo_t* si, void* context) {
  /* Integer divide by zero or integer overflow. */
  /* Note: FPE_INTOVF is ignored on Intel, PowerPC and SPARC systems. */
  if (si->si_code == FPE_INTDIV || si->si_code == FPE_INTOVF) {
    NS_DebugBreak(NS_DEBUG_ABORT, "Divide by zero", nullptr, __FILE__,
                  __LINE__);
  }

#      if defined(__i386__) || defined(__amd64__)
  ucontext_t* uc = (ucontext_t*)context;

  _STRUCT_FP_CONTROL* ctrl = &uc->uc_mcontext->__fs.__fpu_fcw;
  ctrl->__invalid = ctrl->__denorm = ctrl->__zdiv = ctrl->__ovrfl =
      ctrl->__undfl = ctrl->__precis = 1;

  _STRUCT_FP_STATUS* status = &uc->uc_mcontext->__fs.__fpu_fsw;
  status->__invalid = status->__denorm = status->__zdiv = status->__ovrfl =
      status->__undfl = status->__precis = status->__stkflt =
          status->__errsumm = 0;

  uint32_t* mxcsr = &uc->uc_mcontext->__fs.__fpu_mxcsr;
  *mxcsr |= SSE_EXCEPTION_MASK; /* disable all SSE exceptions */
  *mxcsr &= ~SSE_STATUS_FLAGS;  /* clear all pending SSE exceptions */
#      endif
#    if defined(LINUX) && !defined(ANDROID)

#      if defined(__i386__)
  ucontext_t* uc = (ucontext_t*)context;
  /*
   * It seems that we have no access to mxcsr on Linux. libc
   * seems to be translating cw/sw to mxcsr.
   */
  unsigned long int* cw = &uc->uc_mcontext.fpregs->cw;
  *cw |= FPU_EXCEPTION_MASK;

  unsigned long int* sw = &uc->uc_mcontext.fpregs->sw;
  *sw &= ~FPU_STATUS_FLAGS;
#      endif
#      if defined(__amd64__)
  ucontext_t* uc = (ucontext_t*)context;

  uint16_t* cw = &uc->uc_mcontext.fpregs->cwd;
  *cw |= FPU_EXCEPTION_MASK;

  uint16_t* sw = &uc->uc_mcontext.fpregs->swd;
  *sw &= ~FPU_STATUS_FLAGS;

  uint32_t* mxcsr = &uc->uc_mcontext.fpregs->mxcsr;
  *mxcsr |= SSE_EXCEPTION_MASK; /* disable all SSE exceptions */
  *mxcsr &= ~SSE_STATUS_FLAGS;  /* clear all pending SSE exceptions */
#      endif
#    endif
#    ifdef SOLARIS
  ucontext_t* uc = (ucontext_t*)context;

#      if defined(__i386)
  uint32_t* cw = &uc->uc_mcontext.fpregs.fp_reg_set.fpchip_state.state[0];
  *cw |= FPU_EXCEPTION_MASK;

  uint32_t* sw = &uc->uc_mcontext.fpregs.fp_reg_set.fpchip_state.state[1];
  *sw &= ~FPU_STATUS_FLAGS;

  /* address of the instruction that caused the exception */
  uint32_t* ip = &uc->uc_mcontext.fpregs.fp_reg_set.fpchip_state.state[3];
  uc->uc_mcontext.gregs[REG_PC] = *ip;
#      endif
#      if defined(__amd64__)
  uint16_t* cw = &uc->uc_mcontext.fpregs.fp_reg_set.fpchip_state.cw;
  *cw |= FPU_EXCEPTION_MASK;

  uint16_t* sw = &uc->uc_mcontext.fpregs.fp_reg_set.fpchip_state.sw;
  *sw &= ~FPU_STATUS_FLAGS;

  uint32_t* mxcsr = &uc->uc_mcontext.fpregs.fp_reg_set.fpchip_state.mxcsr;
  *mxcsr |= SSE_EXCEPTION_MASK; /* disable all SSE exceptions */
  *mxcsr &= ~SSE_STATUS_FLAGS;  /* clear all pending SSE exceptions */
#      endif
#    endif
}
#  endif

void InstallSignalHandlers(const char* aProgname) {
#  if defined(CRAWL_STACK_ON_SIGSEGV)
  if (aProgname) {
    const char* tmp = strdup(aProgname);
    if (tmp) {
      gProgname = tmp;
    }
  }
#  endif  // CRAWL_STACK_ON_SIGSEGV

  const char* gdbSleep = PR_GetEnv("MOZ_GDB_SLEEP");
  if (gdbSleep && *gdbSleep) {
    unsigned int s;
    if (1 == sscanf(gdbSleep, "%u", &s)) {
      _gdb_sleep_duration = s;
    }
  }

#  if defined(CRAWL_STACK_ON_SIGSEGV)
  if (!getenv("XRE_NO_WINDOWS_CRASH_DIALOG")) {
    void (*crap_handler)(int) = GeckoProcessType_Default != XRE_GetProcessType()
                                    ? child_ah_crap_handler
                                    : ah_crap_handler;
    signal(SIGSEGV, crap_handler);
    signal(SIGILL, crap_handler);
    signal(SIGABRT, crap_handler);
  }
#  endif  // CRAWL_STACK_ON_SIGSEGV

#  ifdef SA_SIGINFO
  /* Install a handler for floating point exceptions and disable them if they
   * occur. */
  struct sigaction sa, osa;
  sa.sa_flags = SA_ONSTACK | SA_RESTART | SA_SIGINFO;
  sa.sa_sigaction = fpehandler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGFPE, &sa, &osa);
#  endif

  if (!XRE_IsParentProcess()) {
    /*
     * If the user is debugging a Gecko parent process in gdb and hits ^C to
     * suspend, a SIGINT signal will be sent to the child. We ignore this signal
     * so the child isn't killed.
     */
    signal(SIGINT, SIG_IGN);
  }

#  if defined(DEBUG) && defined(LINUX)
  const char* memLimit = PR_GetEnv("MOZ_MEM_LIMIT");
  if (memLimit && *memLimit) {
    long m = atoi(memLimit);
    m *= (1024 * 1024);
    struct rlimit r;
    r.rlim_cur = m;
    r.rlim_max = m;
    setrlimit(RLIMIT_AS, &r);
  }
#  endif

}

