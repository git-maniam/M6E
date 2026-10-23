/*
 * Copyright 2014 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmSignalHandlers.h"

#include "mozilla/Casting.h"
#include "mozilla/ThreadLocal.h"

#include "threading/Thread.h"
#include "vm/JitActivation.h"  // js::jit::JitActivation
#include "vm/Realm.h"
#include "vm/Runtime.h"
#include "wasm/WasmCode.h"
#include "wasm/WasmInstance.h"

#if   defined(XP_DARWIN)
#  include <mach/exc.h>
#  include <mach/mach.h>
#elif !defined(__wasi__)
#  include <signal.h>
#endif

using namespace js;
using namespace js::wasm;

#if !defined(JS_CODEGEN_NONE)

// =============================================================================
// This following pile of macros and includes defines the ToRegisterState() and
// the ContextTo{PC,FP,SP,LR}() functions from the (highly) platform-specific
// CONTEXT struct which is provided to the signal handler.
// =============================================================================

#  if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#    include <sys/ucontext.h>  // for ucontext_t, mcontext_t
#  endif

#  if defined(__x86_64__)
#    if   defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
        defined(__NetBSD__) || defined(__OpenBSD__)
#      include <machine/fpu.h>  // for struct savefpu/fxsave64
#    endif
#  endif

#  if   defined(__DragonFly__) || defined(__FreeBSD__) || \
      defined(__FreeBSD_kernel__)
#    define EIP_sig(p) ((p)->uc_mcontext.mc_eip)
#    define EBP_sig(p) ((p)->uc_mcontext.mc_ebp)
#    define ESP_sig(p) ((p)->uc_mcontext.mc_esp)
#    define RIP_sig(p) ((p)->uc_mcontext.mc_rip)
#    define RSP_sig(p) ((p)->uc_mcontext.mc_rsp)
#    define RBP_sig(p) ((p)->uc_mcontext.mc_rbp)
#      define R11_sig(p) ((p)->uc_mcontext.mc_r11)
#      define R13_sig(p) ((p)->uc_mcontext.mc_r13)
#      define R14_sig(p) ((p)->uc_mcontext.mc_r14)
#      define R15_sig(p) ((p)->uc_mcontext.mc_r15)
#    if defined(__FreeBSD__) && (defined(__ppc64__) || defined(__PPC64__) || \
                                 defined(__ppc64le__) || defined(__PPC64LE__))
#      define R01_sig(p) ((p)->uc_mcontext.mc_gpr[1])
#      define R32_sig(p) ((p)->uc_mcontext.mc_srr0)
#    endif
#  elif defined(XP_DARWIN)
#    define EIP_sig(p) ((p)->thread.uts.ts32.__eip)
#    define EBP_sig(p) ((p)->thread.uts.ts32.__ebp)
#    define ESP_sig(p) ((p)->thread.uts.ts32.__esp)
#    define RIP_sig(p) ((p)->thread.__rip)
#    define RBP_sig(p) ((p)->thread.__rbp)
#    define RSP_sig(p) ((p)->thread.__rsp)
#    define R11_sig(p) ((p)->thread.__r[11])
#    define R13_sig(p) ((p)->thread.__sp)
#    define R14_sig(p) ((p)->thread.__lr)
#    define R15_sig(p) ((p)->thread.__pc)
#    define EPC_sig(p) ((p)->thread.__pc)
#    define RFP_sig(p) ((p)->thread.__fp)
#    define R31_sig(p) ((p)->thread.__sp)
#    define RLR_sig(p) ((p)->thread.__lr)
#  else
#    error \
        "Don't know how to read/write to the thread state via the mcontext_t."
#  endif


#  if defined(XP_DARWIN)
#    if defined(__x86_64__)
struct macos_x64_context {
  x86_thread_state64_t thread;
  x86_float_state64_t float_;
};
#      define CONTEXT macos_x64_context
#    elif defined(__i386__)
struct macos_x86_context {
  x86_thread_state_t thread;
  x86_float_state_t float_;
};
#      define CONTEXT macos_x86_context
#    elif defined(__arm__)
struct macos_arm_context {
  arm_thread_state_t thread;
  arm_neon_state_t float_;
};
#      define CONTEXT macos_arm_context
#    elif defined(__aarch64__)
struct macos_aarch64_context {
  arm_thread_state64_t thread;
  arm_neon_state64_t float_;
};
#      define CONTEXT macos_aarch64_context
#    else
#      error Unsupported architecture
#    endif
#  else
#    define CONTEXT ucontext_t
#  endif

#  if defined(_M_X64) || defined(__x86_64__)
#    define PC_sig(p) RIP_sig(p)
#    define FP_sig(p) RBP_sig(p)
#    define SP_sig(p) RSP_sig(p)
#  elif defined(_M_IX86) || defined(__i386__)
#    define PC_sig(p) EIP_sig(p)
#    define FP_sig(p) EBP_sig(p)
#    define SP_sig(p) ESP_sig(p)
#  elif defined(__arm__)
#    define FP_sig(p) R11_sig(p)
#    define SP_sig(p) R13_sig(p)
#    define LR_sig(p) R14_sig(p)
#    define PC_sig(p) R15_sig(p)
#  elif defined(_M_ARM64) || defined(__aarch64__)
#    define PC_sig(p) EPC_sig(p)
#    define FP_sig(p) RFP_sig(p)
#    define SP_sig(p) R31_sig(p)
#    define LR_sig(p) RLR_sig(p)
#  elif defined(__mips__)
#    define PC_sig(p) EPC_sig(p)
#    define FP_sig(p) RFP_sig(p)
#    define SP_sig(p) RSP_sig(p)
#    define LR_sig(p) R31_sig(p)
#  elif defined(__ppc64__) || defined(__PPC64__) || defined(__ppc64le__) || \
      defined(__PPC64LE__)
#    define PC_sig(p) R32_sig(p)
#    define SP_sig(p) R01_sig(p)
#    define FP_sig(p) R01_sig(p)
#  elif defined(__loongarch__)
#    define PC_sig(p) EPC_sig(p)
#    define FP_sig(p) RFP_sig(p)
#    define SP_sig(p) R03_sig(p)
#    define LR_sig(p) RRA_sig(p)
#  elif defined(__riscv)
#    define PC_sig(p) RPC_sig(p)
#    define FP_sig(p) RFP_sig(p)
#    define SP_sig(p) R02_sig(p)
#    define LR_sig(p) RRA_sig(p)
#  endif

static void SetContextPC(CONTEXT* context, uint8_t* pc) {
#  ifdef PC_sig
  *mozilla::BitwiseCast<uint8_t**>(&PC_sig(context)) = pc;
#  else
  MOZ_CRASH();
#  endif
}

static uint8_t* ContextToPC(CONTEXT* context) {
#  ifdef PC_sig
  return mozilla::BitwiseCast<uint8_t*>(PC_sig(context));
#  else
  MOZ_CRASH();
#  endif
}

static uint8_t* ContextToFP(CONTEXT* context) {
#  ifdef FP_sig
  return mozilla::BitwiseCast<uint8_t*>(FP_sig(context));
#  else
  MOZ_CRASH();
#  endif
}

static uint8_t* ContextToSP(CONTEXT* context) {
#  ifdef SP_sig
  return mozilla::BitwiseCast<uint8_t*>(SP_sig(context));
#  else
  MOZ_CRASH();
#  endif
}

#  if defined(__arm__) || defined(__aarch64__) || defined(__mips__) || \
      defined(__loongarch__) || defined(__riscv)
static uint8_t* ContextToLR(CONTEXT* context) {
#    ifdef LR_sig
  return mozilla::BitwiseCast<uint8_t*>(LR_sig(context));
#    else
  MOZ_CRASH();
#    endif
}
#  endif

static JS::ProfilingFrameIterator::RegisterState ToRegisterState(
    CONTEXT* context) {
  JS::ProfilingFrameIterator::RegisterState state;
  state.fp = ContextToFP(context);
  state.pc = ContextToPC(context);
  state.sp = ContextToSP(context);
#  if defined(__arm__) || defined(__aarch64__) || defined(__mips__) || \
      defined(__loongarch__) || defined(__riscv)
  state.lr = ContextToLR(context);
#  else
  state.lr = (void*)UINTPTR_MAX;
#  endif
  return state;
}

// =============================================================================
// All signals/exceptions funnel down to this one trap-handling function which
// tests whether the pc is in a wasm module and, if so, whether there is
// actually a trap expected at this pc. These tests both avoid real bugs being
// silently converted to wasm traps and provides the trapping wasm bytecode
// offset we need to report in the error.
//
// Crashing inside wasm trap handling (due to a bug in trap handling or exposed
// during trap handling) must be reported like a normal crash, not cause the
// crash report to be lost. On Windows and non-Mach Unix, a crash during the
// handler reenters the handler, possibly repeatedly until exhausting the stack,
// and so we prevent recursion with the thread-local sAlreadyHandlingTrap. On
// Mach, the wasm exception handler has its own thread and is installed only on
// the thread-level debugging ports of JSRuntime threads, so a crash on
// exception handler thread will not recurse; it will bubble up to the
// process-level debugging ports (where Breakpad is installed).
// =============================================================================

static MOZ_THREAD_LOCAL(bool) sAlreadyHandlingTrap;

struct AutoHandlingTrap {
  AutoHandlingTrap() {
    MOZ_ASSERT(!sAlreadyHandlingTrap.get());
    sAlreadyHandlingTrap.set(true);
  }

  ~AutoHandlingTrap() {
    MOZ_ASSERT(sAlreadyHandlingTrap.get());
    sAlreadyHandlingTrap.set(false);
  }
};

[[nodiscard]] static bool HandleTrap(CONTEXT* context,
                                     uint8_t* faultAddr = nullptr,
                                     JSContext* assertCx = nullptr) {
  MOZ_ASSERT(sAlreadyHandlingTrap.get());

  uint8_t* pc = ContextToPC(context);
  const CodeBlock* codeBlock = LookupCodeBlock(pc);
  if (!codeBlock) {
    return false;
  }

  Trap trap;
  TrapSite trapSite;
  if (!codeBlock->lookupTrap(pc, &trap, &trapSite)) {
    return false;
  }

  // We have a safe, expected wasm trap, so fp is well-defined to be a Frame*.
  // For the first sanity check, the Trap::IndirectCallBadSig special case is
  // due to this trap occurring in the indirect call prologue, while fp points
  // to the caller's Frame which can be in a different Module. In any case,
  // though, the containing JSContext is the same.

  auto* frame = reinterpret_cast<Frame*>(ContextToFP(context));
  Instance* instance = GetNearestEffectiveInstance(frame);
  MOZ_RELEASE_ASSERT(&instance->code() == codeBlock->code ||
                     trap == Trap::IndirectCallBadSig);

  // For OutOfBounds triggered by a memory fault (faultAddr != nullptr), verify
  // the address is actually within a wasm memory's mapped region. This catches
  // bugs where an unrelated fault at an arbitrary address would otherwise be
  // silently swallowed as an OOB. When faultAddr is null (explicit trap
  // instruction, e.g. from an inlined bounds check), skip validation.
  uint32_t faultMemoryIndex = 0;
  uint64_t faultByteOffset = 0;
  if (trap == Trap::OutOfBounds && faultAddr) {
    if (!instance->memoryAccessInMappedRegion(faultAddr, &faultMemoryIndex,
                                              &faultByteOffset)) {
      return false;
    }
  }

  // Ensure the active FP has a valid instance pointer that the trap stub can
  // use.
  ((FrameWithInstances*)frame)->setCalleeInstance(instance);

  JSContext* cx =
      instance->realm()->runtimeFromAnyThread()->mainContextFromAnyThread();
  MOZ_RELEASE_ASSERT(!assertCx || cx == assertCx);

  // JitActivation::startWasmTrap() stores enough register state from the
  // point of the trap to allow stack unwinding or resumption, both of which
  // will call finishWasmTrap().
  jit::JitActivation* activation = cx->activation()->asJit();
  activation->startWasmTrap(trap, trapSite, ToRegisterState(context));
  if (trap == Trap::OutOfBounds && faultAddr) {
    activation->setWasmTrapFaultInfo(faultMemoryIndex, faultByteOffset);
  }
  SetContextPC(context, codeBlock->code->trapCode());
  return true;
}

// =============================================================================
// The following platform-specific handlers funnel all signals/exceptions into
// the shared HandleTrap() above.
// =============================================================================

#  if   defined(XP_DARWIN)
// On OSX we are forced to use the lower-level Mach exception mechanism instead
// of Unix signals because breakpad uses Mach exceptions and would otherwise
// report a crash before wasm gets a chance to handle the exception.

// This definition was generated by mig (the Mach Interface Generator) for the
// routine 'exception_raise' (exc.defs).
#    pragma pack(4)
typedef struct {
  mach_msg_header_t Head;
  /* start of the kernel processed data */
  mach_msg_body_t msgh_body;
  mach_msg_port_descriptor_t thread;
  mach_msg_port_descriptor_t task;
  /* end of the kernel processed data */
  NDR_record_t NDR;
  exception_type_t exception;
  mach_msg_type_number_t codeCnt;
  int64_t code[2];
} Request__mach_exception_raise_t;
#    pragma pack()

// The full Mach message also includes a trailer.
struct ExceptionRequest {
  Request__mach_exception_raise_t body;
  mach_msg_trailer_t trailer;
};

static bool HandleMachException(const ExceptionRequest& request) {
  // Get the port of the JSContext's thread from the message.
  mach_port_t cxThread = request.body.thread.name;

  // Read out the JSRuntime thread's register state.
  CONTEXT context;
#    if defined(__x86_64__)
  unsigned int thread_state_count = x86_THREAD_STATE64_COUNT;
  unsigned int float_state_count = x86_FLOAT_STATE64_COUNT;
  int thread_state = x86_THREAD_STATE64;
  int float_state = x86_FLOAT_STATE64;
#    elif defined(__i386__)
  unsigned int thread_state_count = x86_THREAD_STATE_COUNT;
  unsigned int float_state_count = x86_FLOAT_STATE_COUNT;
  int thread_state = x86_THREAD_STATE;
  int float_state = x86_FLOAT_STATE;
#    elif defined(__arm__)
  unsigned int thread_state_count = ARM_THREAD_STATE_COUNT;
  unsigned int float_state_count = ARM_NEON_STATE_COUNT;
  int thread_state = ARM_THREAD_STATE;
  int float_state = ARM_NEON_STATE;
#    elif defined(__aarch64__)
  unsigned int thread_state_count = ARM_THREAD_STATE64_COUNT;
  unsigned int float_state_count = ARM_NEON_STATE64_COUNT;
  int thread_state = ARM_THREAD_STATE64;
  int float_state = ARM_NEON_STATE64;
#    else
#      error Unsupported architecture
#    endif
  kern_return_t kret;
  kret = thread_get_state(cxThread, thread_state,
                          (thread_state_t)&context.thread, &thread_state_count);
  if (kret != KERN_SUCCESS) {
    return false;
  }
  kret = thread_get_state(cxThread, float_state,
                          (thread_state_t)&context.float_, &float_state_count);
  if (kret != KERN_SUCCESS) {
    return false;
  }

  if (request.body.exception != EXC_BAD_ACCESS &&
      request.body.exception != EXC_BAD_INSTRUCTION) {
    return false;
  }

  {
    AutoNoteSingleThreadedRegion anstr;
    AutoHandlingTrap aht;
    uint8_t* faultAddr = nullptr;
    if (request.body.exception == EXC_BAD_ACCESS) {
      faultAddr = (uint8_t*)request.body.code[1];
    }
    if (!HandleTrap(&context, faultAddr)) {
      return false;
    }
  }

  // Update the thread state with the new pc and register values.
  kret = thread_set_state(cxThread, float_state,
                          (thread_state_t)&context.float_, float_state_count);
  if (kret != KERN_SUCCESS) {
    return false;
  }
  kret = thread_set_state(cxThread, thread_state,
                          (thread_state_t)&context.thread, thread_state_count);
  if (kret != KERN_SUCCESS) {
    return false;
  }

  return true;
}

static mach_port_t sMachDebugPort = MACH_PORT_NULL;

static void MachExceptionHandlerThread() {
  ThisThread::SetName("JS Wasm MachExceptionHandler");

  // Taken from mach_exc in /usr/include/mach/mach_exc.defs.
  static const unsigned EXCEPTION_MSG_ID = 2405;

  while (true) {
    ExceptionRequest request;
    kern_return_t kret =
        mach_msg(&request.body.Head, MACH_RCV_MSG, 0, sizeof(request),
                 sMachDebugPort, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    // If we fail even receiving the message, we can't even send a reply!
    // Rather than hanging the faulting thread (hanging the browser), crash.
    if (kret != KERN_SUCCESS) {
      fprintf(stderr, "MachExceptionHandlerThread: mach_msg failed with %d\n",
              (int)kret);
      MOZ_CRASH();
    }

    if (request.body.Head.msgh_id != EXCEPTION_MSG_ID) {
      fprintf(stderr, "Unexpected msg header id %d\n",
              (int)request.body.Head.msgh_bits);
      MOZ_CRASH();
    }

    // Some thread just commited an EXC_BAD_ACCESS and has been suspended by
    // the kernel. The kernel is waiting for us to reply with instructions.
    // Our default is the "not handled" reply (by setting the RetCode field
    // of the reply to KERN_FAILURE) which tells the kernel to continue
    // searching at the process and system level.
    bool handled = HandleMachException(request);
    kern_return_t replyCode = handled ? KERN_SUCCESS : KERN_FAILURE;

    // This magic incantation to send a reply back to the kernel was
    // derived from the exc_server generated by
    // 'mig -v /usr/include/mach/mach_exc.defs'.
    __Reply__exception_raise_t reply;
    reply.Head.msgh_bits =
        MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(request.body.Head.msgh_bits), 0);
    reply.Head.msgh_size = sizeof(reply);
    reply.Head.msgh_remote_port = request.body.Head.msgh_remote_port;
    reply.Head.msgh_local_port = MACH_PORT_NULL;
    reply.Head.msgh_id = request.body.Head.msgh_id + 100;
    reply.NDR = NDR_record;
    reply.RetCode = replyCode;
    mach_msg(&reply.Head, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL,
             MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
  }
}

#  else  // If not Windows or Mac, assume Unix

#    if defined(__mips__) || defined(__loongarch__)
static const uint32_t kWasmTrapSignal = SIGFPE;
#    else
static const uint32_t kWasmTrapSignal = SIGILL;
#    endif

static struct sigaction sPrevSEGVHandler;
static struct sigaction sPrevSIGBUSHandler;
static struct sigaction sPrevWasmTrapHandler;

typedef void (*sa_sigaction_t)(int, siginfo_t*, void*);

// See the uses below for more context. We need to cast the SIG_DFL/SIG_IGN
// sentinel values to check to see if they're in the sa_sigaction field (which
// may or may not be in a union with the sa_handler field).
#    define SIG_ACTION_DFL ((sa_sigaction_t)SIG_DFL)
#    define SIG_ACTION_IGN ((sa_sigaction_t)SIG_IGN)

static void WasmTrapHandler(int signum, siginfo_t* info, void* context) {
  if (!sAlreadyHandlingTrap.get()) {
    AutoHandlingTrap aht;
    MOZ_RELEASE_ASSERT(signum == SIGSEGV || signum == SIGBUS ||
                       signum == kWasmTrapSignal);
    uint8_t* faultAddr = nullptr;
    if (signum == SIGSEGV || signum == SIGBUS) {
      faultAddr = (uint8_t*)info->si_addr;
    }
    JSContext* cx = TlsContext.get();  // Cold signal handling code
    if (HandleTrap((CONTEXT*)context, faultAddr, cx)) {
      return;
    }
  }

  struct sigaction* previousSignal = nullptr;
  switch (signum) {
    case SIGSEGV:
      previousSignal = &sPrevSEGVHandler;
      break;
    case SIGBUS:
      previousSignal = &sPrevSIGBUSHandler;
      break;
    case kWasmTrapSignal:
      previousSignal = &sPrevWasmTrapHandler;
      break;
  }
  MOZ_ASSERT(previousSignal);

  // This signal is not for any wasm code we expect, so we need to forward
  // the signal to the next handler. If there is no next handler (SIG_IGN or
  // SIG_DFL), then it's time to crash. To do this, we set the signal back to
  // its original disposition and return. This will cause the faulting op to
  // be re-executed which will crash in the normal way. The advantage of
  // doing this to calling _exit() is that we remove ourselves from the crash
  // stack which improves crash reports. If there is a next handler, call it.
  // It will either crash synchronously, fix up the instruction so that
  // execution can continue and return, or trigger a crash by returning the
  // signal to it's original disposition and returning.
  //
  // Note: the order of these tests matter.
  //
  // POSIX specifies that if SA_SIGINFO is set, then sa_sigaction should be
  // called instead of sa_handler. However, it appears that the flag can be set
  // even when sa_sigaction is not a valid function pointer but instead one of
  // the SIG_DFL/SIG_IGN sentinel values. In this case, we should not call the
  // function, but fallthrough.
  if ((previousSignal->sa_flags & SA_SIGINFO) &&
      previousSignal->sa_sigaction != SIG_ACTION_DFL &&
      previousSignal->sa_sigaction != SIG_ACTION_IGN) {
    previousSignal->sa_sigaction(signum, info, context);
  } else if (previousSignal->sa_handler == SIG_DFL ||
             previousSignal->sa_handler == SIG_IGN) {
    sigaction(signum, previousSignal, nullptr);
  } else {
    previousSignal->sa_handler(signum);
  }
}
#  endif  // XP_WIN || XP_DARWIN || assume unix

struct InstallState {
  bool tried;
  bool success;
  InstallState() : tried(false), success(false) {}
};

MOZ_RUNINIT static ExclusiveData<InstallState> sEagerInstallState(
    mutexid::WasmSignalInstallState);

#endif  // !(JS_CODEGEN_NONE)

void wasm::EnsureEagerProcessSignalHandlers() {
#ifdef JS_CODEGEN_NONE
  // If there is no JIT, then there should be no Wasm signal handlers.
  return;
#else
  auto eagerInstallState = sEagerInstallState.lock();
  if (eagerInstallState->tried) {
    return;
  }

  eagerInstallState->tried = true;
  MOZ_RELEASE_ASSERT(eagerInstallState->success == false);

  sAlreadyHandlingTrap.infallibleInit();

  // Install whatever exception/signal handler is appropriate for the OS.
#  if   defined(XP_DARWIN)
  // All the Mach setup in EnsureLazyProcessSignalHandlers.
#  else
  // SA_NODEFER allows us to reenter the signal handler if we crash while
  // handling the signal, and fall through to the Breakpad handler by testing
  // handlingSegFault.

  // Allow handling OOB with signals on all architectures
  struct sigaction faultHandler;
  faultHandler.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
  faultHandler.sa_sigaction = WasmTrapHandler;
  sigemptyset(&faultHandler.sa_mask);
  if (sigaction(SIGSEGV, &faultHandler, &sPrevSEGVHandler)) {
    MOZ_CRASH("unable to install segv handler");
  }

#    if defined(JS_CODEGEN_ARM)
  // On Arm Handle Unaligned Accesses
  struct sigaction busHandler;
  busHandler.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
  busHandler.sa_sigaction = WasmTrapHandler;
  sigemptyset(&busHandler.sa_mask);
  if (sigaction(SIGBUS, &busHandler, &sPrevSIGBUSHandler)) {
    MOZ_CRASH("unable to install sigbus handler");
  }
#    endif

  // Install a handler to handle the instructions that are emitted to implement
  // wasm traps.
  struct sigaction wasmTrapHandler;
  wasmTrapHandler.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
  wasmTrapHandler.sa_sigaction = WasmTrapHandler;
  sigemptyset(&wasmTrapHandler.sa_mask);
  if (sigaction(kWasmTrapSignal, &wasmTrapHandler, &sPrevWasmTrapHandler)) {
    MOZ_CRASH("unable to install wasm trap handler");
  }
#  endif

  eagerInstallState->success = true;
#endif
}

#ifndef JS_CODEGEN_NONE
MOZ_RUNINIT static ExclusiveData<InstallState> sLazyInstallState(
    mutexid::WasmSignalInstallState);

static bool EnsureLazyProcessSignalHandlers() {
  auto lazyInstallState = sLazyInstallState.lock();
  if (lazyInstallState->tried) {
    return lazyInstallState->success;
  }

  lazyInstallState->tried = true;
  MOZ_RELEASE_ASSERT(lazyInstallState->success == false);

#  ifdef XP_DARWIN
  // Create the port that all JSContext threads will redirect their traps to.
  kern_return_t kret;
  kret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                            &sMachDebugPort);
  if (kret != KERN_SUCCESS) {
    return false;
  }
  kret = mach_port_insert_right(mach_task_self(), sMachDebugPort,
                                sMachDebugPort, MACH_MSG_TYPE_MAKE_SEND);
  if (kret != KERN_SUCCESS) {
    return false;
  }

  // Create the thread that will wait on and service sMachDebugPort.
  // It's not useful to destroy this thread on process shutdown so
  // immediately detach on successful start.
  Thread handlerThread;
  if (!handlerThread.init(MachExceptionHandlerThread)) {
    return false;
  }
  handlerThread.detach();
#  endif

  lazyInstallState->success = true;
  return true;
}
#endif  // JS_CODEGEN_NONE

bool wasm::EnsureFullSignalHandlers(JSContext* cx) {
#ifdef JS_CODEGEN_NONE
  return false;
#else
  if (cx->wasm().triedToInstallSignalHandlers) {
    return cx->wasm().haveSignalHandlers;
  }

  cx->wasm().triedToInstallSignalHandlers = true;
  MOZ_RELEASE_ASSERT(!cx->wasm().haveSignalHandlers);

  {
    auto eagerInstallState = sEagerInstallState.lock();
    MOZ_RELEASE_ASSERT(eagerInstallState->tried);
    if (!eagerInstallState->success) {
      return false;
    }
  }

  if (!EnsureLazyProcessSignalHandlers()) {
    return false;
  }

#  ifdef XP_DARWIN
  // In addition to the process-wide signal handler setup, OSX needs each
  // thread configured to send its exceptions to sMachDebugPort. While there
  // are also task-level (i.e. process-level) exception ports, those are
  // "claimed" by breakpad and chaining Mach exceptions is dark magic that we
  // avoid by instead intercepting exceptions at the thread level before they
  // propagate to the process-level. This works because there are no other
  // uses of thread-level exception ports.
  MOZ_RELEASE_ASSERT(sMachDebugPort != MACH_PORT_NULL);
  thread_port_t thisThread = mach_thread_self();
  kern_return_t kret = thread_set_exception_ports(
      thisThread, EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION,
      sMachDebugPort, EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
      THREAD_STATE_NONE);
  mach_port_deallocate(mach_task_self(), thisThread);
  if (kret != KERN_SUCCESS) {
    return false;
  }
#  endif

  cx->wasm().haveSignalHandlers = true;
  return true;
#endif
}

bool wasm::MemoryAccessTraps(const RegisterState& regs, uint8_t* addr,
                             uint32_t numBytes, uint8_t** newPC) {
#ifdef JS_CODEGEN_NONE
  return false;
#else
  const wasm::CodeBlock* codeBlock = wasm::LookupCodeBlock(regs.pc);
  if (!codeBlock) {
    return false;
  }

  Trap trap;
  TrapSite trapSite;
  if (!codeBlock->code->lookupTrap(regs.pc, &trap, &trapSite)) {
    return false;
  }
  switch (trap) {
    case Trap::OutOfBounds:
      break;
    case Trap::NullPointerDereference:
    case Trap::BadCast:
      if ((uintptr_t)addr >= NullPtrGuardSize) {
        return false;
      }
      break;
#  ifdef WASM_HAS_HEAPREG
    case Trap::IndirectCallToNull:
      // Null pointer plus the appropriate offset.
      if (addr !=
          reinterpret_cast<uint8_t*>(wasm::Instance::offsetOfMemory0Base())) {
        return false;
      }
      break;
#  endif
    default:
      return false;
  }

  // This is a safe and expected wasm trap. This guarantees that FP is pointing
  // at a wasm frame.
  FrameWithInstances* frame = (FrameWithInstances*)(regs.fp);
  Instance& instance = *GetNearestEffectiveInstance(frame);
  MOZ_ASSERT(&instance.code() == codeBlock->code);

  // Ensure the active FP has a valid instance pointer that the trap stub can
  // use.
  frame->setCalleeInstance(&instance);

  uint32_t faultMemoryIndex = 0;
  uint64_t faultByteOffset = 0;
  switch (trap) {
    case Trap::OutOfBounds:
      if (!instance.memoryAccessInGuardRegion((uint8_t*)addr, numBytes)) {
        return false;
      }
      MOZ_ALWAYS_TRUE(instance.memoryAccessInMappedRegion(
          (uint8_t*)addr, &faultMemoryIndex, &faultByteOffset));
      break;
    case Trap::NullPointerDereference:
    case Trap::BadCast:
#  ifdef WASM_HAS_HEAPREG
    case Trap::IndirectCallToNull:
#  endif
      break;
    default:
      MOZ_CRASH("Should not happen");
  }

  JSContext* cx = TlsContext.get();  // Cold simulator helper function
  jit::JitActivation* activation = cx->activation()->asJit();
  activation->startWasmTrap(trap, trapSite, regs);
  if (trap == Trap::OutOfBounds) {
    activation->setWasmTrapFaultInfo(faultMemoryIndex, faultByteOffset);
  }
  *newPC = codeBlock->code->trapCode();
  return true;
#endif
}

bool wasm::HandleIllegalInstruction(const RegisterState& regs,
                                    uint8_t** newPC) {
#ifdef JS_CODEGEN_NONE
  return false;
#else
  const wasm::CodeBlock* codeBlock = wasm::LookupCodeBlock(regs.pc);
  if (!codeBlock) {
    return false;
  }

  Trap trap;
  TrapSite trapSite;
  if (!codeBlock->code->lookupTrap(regs.pc, &trap, &trapSite)) {
    return false;
  }

  // This is a safe and expected wasm trap. This guarantees that FP is pointing
  // at a wasm frame.
  FrameWithInstances* frame = (FrameWithInstances*)(regs.fp);
  Instance& instance = *GetNearestEffectiveInstance(frame);
  MOZ_ASSERT(&instance.code() == codeBlock->code);

  // Ensure the active FP has a valid instance pointer that the trap stub can
  // use.
  frame->setCalleeInstance(&instance);

  JSContext* cx = TlsContext.get();  // Cold simulator helper function
  jit::JitActivation* activation = cx->activation()->asJit();
  activation->startWasmTrap(trap, trapSite, regs);
  *newPC = codeBlock->code->trapCode();
  return true;
#endif
}
