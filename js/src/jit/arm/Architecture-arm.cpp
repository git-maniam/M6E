/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/arm/Architecture-arm.h"


#include <bit>
#include <fcntl.h>
#include <string_view>
#  include <unistd.h>


#include "jit/arm/Assembler-arm.h"
#include "jit/FlushICache.h"  // js::jit::FlushICache
#include "jit/RegisterSets.h"
#include "jit/Simulator.h"

// The Android NDK and B2G do not include the hwcap.h kernel header, and it is
// not defined when building the simulator, so inline the header defines we
// need.
#  define HWCAP_VFP (1 << 6)
#  define HWCAP_NEON (1 << 12)
#  define HWCAP_VFPv3 (1 << 13)
#  define HWCAP_VFPv3D16 (1 << 14) /* also set for VFPv4-D16 */
#  define HWCAP_VFPv4 (1 << 16)
#  define HWCAP_IDIVA (1 << 17)
#  define HWCAP_IDIVT (1 << 18)
#  define HWCAP_VFPD32 (1 << 19) /* set if VFP has 32 regs (not 16) */
#  define HWCAP_FPHP (1 << 22)
#  define AT_HWCAP 16

namespace js {
namespace jit {

// Parse the Linux kernel cpuinfo features. This is also used to parse the
// override features which has some extensions: 'armv7', 'align' and 'hardfp'.
static auto ParseARMCpuFeatures(const char* features, bool override = false) {
  ARMCapabilities capabilities{};

  // For ease of running tests we want it to be the default to fixup faults.
  bool fixupAlignmentFault = true;

  for (;;) {
    char ch = *features;
    if (!ch) {
      // End of string.
      break;
    }
    if (ch == ' ' || ch == ',') {
      // Skip separator characters.
      features++;
      continue;
    }
    // Find the end of the token.
    const char* end = features + 1;
    for (;; end++) {
      ch = *end;
      if (!ch || ch == ' ' || ch == ',') {
        break;
      }
    }
    size_t count = end - features;
    std::string_view name{features, count};
    if (name == "vfp") {
      capabilities += ARMCapability::VFP;
    } else if (name == "vfpv2") {
      capabilities += ARMCapability::VFP;  // vfpv2 is the same as vfp
    } else if (name == "neon") {
      capabilities += ARMCapability::Neon;
    } else if (name == "vfpv3") {
      capabilities += ARMCapability::VFPv3;
    } else if (name == "vfpv3d16") {
      capabilities += ARMCapability::VFPv3D16;
    } else if (name == "vfpv4") {
      capabilities += ARMCapability::VFPv4;
    } else if (name == "idiva") {
      capabilities += ARMCapability::IDivA;
    } else if (name == "vfpd32") {
      capabilities += ARMCapability::VFPD32;
    } else if (name == "fphp") {
      capabilities += ARMCapability::FPHP;
    } else if (name == "armv7") {
      capabilities += ARMCapability::ARMv7;
    } else if (name == "align") {
      capabilities +=
          {ARMCapability::AlignmentFault, ARMCapability::FixupFault};
#if defined(JS_SIMULATOR_ARM)
    } else if (name == "nofixup") {
      fixupAlignmentFault = false;
    } else if (name == "hardfp") {
      capabilities += ARMCapability::UseHardFpABI;
#endif
    } else if (override) {
      fprintf(stderr, "Warning: unexpected ARM feature at: %s\n", features);
    }
    features = end;
  }

  if (!fixupAlignmentFault) {
    capabilities -= ARMCapability::FixupFault;
  }

  return capabilities;
}

static auto CanonicalizeARMHwCapabilities(ARMCapabilities capabilities) {
  // Canonicalize the capabilities. These rules are also applied to the features
  // supplied for simulation.

  // VFPv3 is a subset of VFPv4, force this if the input string omits it.
  if (capabilities.contains(ARMCapability::VFPv4)) {
    capabilities += ARMCapability::VFPv3;
  }

  // The VFPv3 feature is expected when the VFPv3D16 is reported, but add it
  // just in case of a kernel difference in feature reporting.
  if (capabilities.contains(ARMCapability::VFPv3D16)) {
    capabilities += ARMCapability::VFPv3;
  }

  // VFPv2 is a subset of VFPv3, force this if the input string omits it.  VFPv2
  // is just an alias for VFP.
  if (capabilities.contains(ARMCapability::VFPv3)) {
    capabilities += ARMCapability::VFP;
  }

  // If we have Neon we have floating point.
  if (capabilities.contains(ARMCapability::Neon)) {
    capabilities += ARMCapability::VFP;
  }

  // If VFPv3 or Neon is supported then this must be an ARMv7.
  if (capabilities.contains(ARMCapability::VFPv3) ||
      capabilities.contains(ARMCapability::Neon)) {
    capabilities += ARMCapability::ARMv7;
  }

  // Some old kernels report VFP and not VFPv3, but if ARMv7 then it must be
  // VFPv3.
  if (capabilities.contains(ARMCapability::VFP) &&
      capabilities.contains(ARMCapability::ARMv7)) {
    capabilities += ARMCapability::VFPv3;
  }

  // Older kernels do not implement the HWCAP_VFPD32 flag.
  if (capabilities.contains(ARMCapability::VFPv3) &&
      !capabilities.contains(ARMCapability::VFPv3D16)) {
    capabilities += ARMCapability::VFPD32;
  }

  // If VFPv4 is supported, then half-precision floating point is supported.
  if (capabilities.contains(ARMCapability::VFPv4)) {
    capabilities += ARMCapability::FPHP;
  }

  return capabilities;
}


bool CPUFlagsHaveBeenComputed() { return ARMFlags::IsInitialized(); }

static const char* gArmHwCapString = nullptr;

void SetARMHwCapFlagsString(const char* armHwCap) {
  MOZ_ASSERT(!CPUFlagsHaveBeenComputed());
  gArmHwCapString = armHwCap;
}

static auto ParseARMHwCapFlags(const char* armHwCap) {
  MOZ_ASSERT(armHwCap);

  if (strstr(armHwCap, "help")) {
    fflush(NULL);
    printf(
        "\n"
        "usage: ARMHWCAP=option,option,option,... where options can be:\n"
        "\n"
        "  vfp      \n"
        "  neon     \n"
        "  vfpv3    \n"
        "  vfpv3d16 \n"
        "  vfpv4    \n"
        "  idiva    \n"
        "  idivt    \n"
        "  vfpd32   \n"
        "  fphp     \n"
        "  armv7    \n"
        "  align    - unaligned accesses will trap and be emulated\n"
#ifdef JS_SIMULATOR_ARM
        "  nofixup  - disable emulation of unaligned accesses\n"
        "  hardfp   \n"
#endif
        "\n");
    exit(0);
    /*NOTREACHED*/
  }

  return ParseARMCpuFeatures(armHwCap, /* override = */ true);
}

#ifndef JS_SIMULATOR_ARM
#endif

static auto ReadARMHwCapFlags() {
  ARMCapabilities capabilities{};

#ifdef JS_SIMULATOR_ARM
  // ARMCapability::FixupFault is on by default even if
  // ARMCapability::AlignmentFault is not on by default, because some memory
  // access instructions always fault. Notably, this is true for floating point
  // accesses.
  capabilities += {
      ARMCapability::ARMv7,      ARMCapability::VFP,  ARMCapability::VFPv3,
      ARMCapability::VFPv4,      ARMCapability::Neon, ARMCapability::IDivA,
      ARMCapability::FixupFault,
  };
#else


  // If compiled to use specialized features then these features can be
  // assumed to be present otherwise the compiler would fail to run.

#  if defined(__VFP_FP__) && !defined(__SOFTFP__)
  // Compiled to use VFP instructions so assume VFP support.
  capabilities += ARMCapability::VFP;
#  endif

#  if defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__)
  // Compiled to use ARMv7 instructions so assume the ARMv7 arch.
  capabilities += ARMCapability::ARMv7;
#  endif

#    if defined(__ARM_NEON__)
  capabilities += ARMCapability::Neon;
#    endif
#    if defined(__ARMVFPV3__)
  capabilities += {ARMCapability::VFPv3, ARMCapability::VFPD32};
#    endif

#endif  // JS_SIMULATOR_ARM

  return capabilities;
}

void ARMFlags::Init() {
  MOZ_RELEASE_ASSERT(!IsInitialized());

  ARMCapabilities capFlags;
  if (const char* env = getenv("ARMHWCAP")) {
    capFlags = ParseARMHwCapFlags(env);
  } else if (gArmHwCapString) {
    capFlags = ParseARMHwCapFlags(gArmHwCapString);
  } else {
    capFlags = ReadARMHwCapFlags();
  }

  capFlags = CanonicalizeARMHwCapabilities(capFlags);

  MOZ_ASSERT(!capFlags.contains(ARMCapability::Initialized));
  capFlags += ARMCapability::Initialized;

  capabilities = capFlags;

  JitSpew(JitSpew_Codegen, "ARM HWCAP: 0x%x\n", capFlags.serialize());
}

Registers::Code Registers::FromName(const char* name) {
  // Check for some register aliases first.
  if (strcmp(name, "ip") == 0) {
    return ip;
  }
  if (strcmp(name, "r13") == 0) {
    return r13;
  }
  if (strcmp(name, "lr") == 0) {
    return lr;
  }
  if (strcmp(name, "r15") == 0) {
    return r15;
  }

  for (size_t i = 0; i < Total; i++) {
    if (strcmp(GetName(i), name) == 0) {
      return Code(i);
    }
  }

  return Invalid;
}

FloatRegisters::Code FloatRegisters::FromName(const char* name) {
  for (size_t i = 0; i < TotalSingle; ++i) {
    if (strcmp(GetSingleName(Encoding(i)), name) == 0) {
      return VFPRegister(i, VFPRegister::Single).code();
    }
  }
  for (size_t i = 0; i < TotalDouble; ++i) {
    if (strcmp(GetDoubleName(Encoding(i)), name) == 0) {
      return VFPRegister(i, VFPRegister::Double).code();
    }
  }

  return Invalid;
}

FloatRegisterSet VFPRegister::ReduceSetForPush(const FloatRegisterSet& s) {
#ifdef ENABLE_JIT_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  LiveFloatRegisterSet mod;
  for (FloatRegisterIterator iter(s); iter.more(); ++iter) {
    FloatRegister reg = *iter;
    if (reg.isSingle() && s.hasRegisterIndex(reg.doubleOverlay())) {
      // If a single register overlays a double, we don't have to push the
      // single register separately.
      continue;
    }
    mod.addUnchecked(*iter);
  }
  return mod.set();
}

uint32_t VFPRegister::GetPushSizeInBytes(const FloatRegisterSet& s) {
#ifdef ENABLE_JIT_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  FloatRegisterSet ss = s.reduceSetForPush();
  uint64_t bits = ss.bits();
  uint32_t ret = std::popcount(bits & 0xffffffff) * sizeof(float);
  ret += std::popcount(bits >> 32) * sizeof(double);
  return ret;
}
uint32_t VFPRegister::getRegisterDumpOffsetInBytes() {
#ifdef ENABLE_JIT_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  if (isSingle()) {
    return id() * sizeof(float);
  }
  if (isDouble()) {
    return id() * sizeof(double);
  }
  MOZ_CRASH("not Single or Double");
}

uint32_t FloatRegisters::ActualTotalPhys() {
  if (ARMFlags::Has32DP()) {
    return 32;
  }
  return 16;
}

void FlushICache(void* code, size_t size) {
#if defined(JS_SIMULATOR_ARM)
  js::jit::SimulatorProcess::FlushICache(code, size);

#else
#  error "Unexpected platform"
#endif
}

void FlushExecutionContext() {
#ifndef JS_SIMULATOR_ARM
  // Ensure that any instructions already in the pipeline are discarded and
  // reloaded from the icache.
  asm volatile("isb\n" : : : "memory");
#else
  // We assume the icache flushing routines on other platforms take care of this
#endif
}

}  // namespace jit
}  // namespace js
