/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MLUtils.h"

#include "prsystem.h"
#include <sys/types.h>
#include "nsSystemInfo.h"

#  include <sys/sysctl.h>
#  include <mach/mach.h>
#include "mozilla/SSE.h"

namespace mozilla::ml {

NS_IMPL_ISUPPORTS(MLUtils, nsIMLUtils)

NS_IMETHODIMP MLUtils::GetTotalPhysicalMemory(uint64_t* _retval) {
  *_retval = PR_GetPhysicalMemorySize();
  return NS_OK;
}

NS_IMETHODIMP MLUtils::GetAvailablePhysicalMemory(uint64_t* _retval) {

  mach_port_t host_port = mach_host_self();
  vm_size_t page_size;
  vm_statistics64_data_t vm_stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

  if (host_page_size(host_port, &page_size) != KERN_SUCCESS ||
      host_statistics64(host_port, HOST_VM_INFO64,
                        reinterpret_cast<host_info64_t>(&vm_stats),
                        &count) != KERN_SUCCESS) {
    return NS_ERROR_FAILURE;
  }

  *_retval =
      static_cast<uint64_t>(vm_stats.free_count + vm_stats.inactive_count) *
      page_size;


  return NS_OK;
}

NS_IMETHODIMP MLUtils::GetOptimalCPUConcurrency(uint8_t* _retval) {
  ProcessInfo processInfo = {};
  if (!NS_SUCCEEDED(CollectProcessInfo(processInfo))) {
    return NS_ERROR_FAILURE;
  }

#  ifdef __aarch64__
  // On aarch64 (like macBooks) we want to avoid efficient cores and stick with
  // "big" cpus.
  uint8_t cpuCount = processInfo.cpuPCount;
#  else
  // on x86_64 we're always using the number of physical cores.
  uint8_t cpuCount = processInfo.cpuCores;
#  endif

  *_retval = cpuCount;

  return NS_OK;
}

NS_IMETHODIMP MLUtils::CanUseLlamaCpp(bool* _retval) {
#ifdef __x86_64__
  *_retval = mozilla::supports_avx2();
#elif defined(__aarch64__)
  *_retval = true;
#else
  *_retval = false;
#endif

  return NS_OK;
}

}  // namespace mozilla::ml
