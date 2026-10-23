// Copyright 2015, ARM Limited
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of ARM Limited nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "jit/arm64/vixl/Cpu-vixl.h"
#if defined(JS_SIMULATOR_ARM64)
#  include "jit/arm64/vixl/Simulator-vixl.h"
#endif
#include "jit/arm64/vixl/Utils-vixl.h"
#include "util/WindowsWrapper.h"

#  include <libkern/OSCacheControl.h>

namespace vixl {

// Currently computes I and D cache line size.
void CPU::SetUp() {
  uint32_t cache_type_register = GetCacheType();

  // The cache type register holds information about the caches, including I
  // D caches line size.
  static const int kDCacheLineSizeShift = 16;
  static const int kICacheLineSizeShift = 0;
  static const uint32_t kDCacheLineSizeMask = 0xf << kDCacheLineSizeShift;
  static const uint32_t kICacheLineSizeMask = 0xf << kICacheLineSizeShift;

  // The cache type register holds the size of the I and D caches in words as
  // a power of two.
  uint32_t dcache_line_size_power_of_two =
      (cache_type_register & kDCacheLineSizeMask) >> kDCacheLineSizeShift;
  uint32_t icache_line_size_power_of_two =
      (cache_type_register & kICacheLineSizeMask) >> kICacheLineSizeShift;

  dcache_line_size_ = 4 << dcache_line_size_power_of_two;
  icache_line_size_ = 4 << icache_line_size_power_of_two;

  // Bug 1521158 suggests that having CPU with different cache line sizes could
  // cause issues as we would only invalidate half of the cache line of we
  // invalidate every 128 bytes, but other little cores have a different stride
  // such as 64 bytes. To be conservative, we will try reducing the stride to 32
  // bytes, which should be smaller than any known cache line.
  const uint32_t conservative_line_size = 32;
  dcache_line_size_ = std::min(dcache_line_size_, conservative_line_size);
  icache_line_size_ = std::min(icache_line_size_, conservative_line_size);
}


uint32_t CPU::GetCacheType() {
  // This will lead to a cache with 1 byte long lines, which is fine since
  // neither EnsureIAndDCacheCoherency nor the simulator will need this
  // information.
  return 0;
}

void CPU::EnsureIAndDCacheCoherency(void* address, size_t length) {
#if defined(JS_SIMULATOR_ARM64) && defined(JS_CACHE_SIMULATOR_ARM64)
  // This code attempts to emulate what the following assembly sequence is
  // doing, which is sending the information to all cores that some cache line
  // have to be invalidated and invalidating them only on the current core.
  //
  // This is done by recording the current range to be flushed to all
  // simulators, then if there is a simulator associated with the current
  // thread, applying all flushed ranges as the "isb" instruction would do.
  //
  // As we have no control over the CPU cores used by the code generator and the
  // execution threads, this code assumes that each thread runs on its own core.
  //
  // See Bug 1529933 for more detailed explanation of this issue.
  using js::jit::SimulatorProcess;
  js::jit::AutoLockSimulatorCache alsc;
  if (length > 0) {
    SimulatorProcess::recordICacheFlush(address, length);
  }
  Simulator* sim = vixl::Simulator::Current();
  if (sim) {
    sim->FlushICache();
  }
#else
  sys_icache_invalidate(address, length);
#endif
}

void CPU::FlushExecutionContext() {
#if defined(JS_SIMULATOR_ARM64) && defined(JS_CACHE_SIMULATOR_ARM64)
  // Performing an 'isb' will ensure the current core instruction pipeline is
  // synchronized with an icache flush executed by another core.
  using js::jit::SimulatorProcess;
  js::jit::AutoLockSimulatorCache alsc;
  Simulator* sim = vixl::Simulator::Current();
  if (sim) {
    sim->FlushICache();
  }
#elif defined(__aarch64__)
  // Ensure that any instructions already in the pipeline are discarded and
  // reloaded from the icache.
  __asm__ __volatile__("isb\n" : : : "memory");
#endif
}

}  // namespace vixl
