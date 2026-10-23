// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROCESS_H_
#define BASE_PROCESS_H_

#include "base/basictypes.h"

#include <sys/types.h>

namespace base {

// ProcessHandle is a platform specific type which represents the underlying OS
// handle to a process.
// ProcessId is a number which identifies the process in the OS.
// On POSIX, our ProcessHandle will just be the PID.
typedef pid_t ProcessHandle;
typedef pid_t ProcessId;
// inttypes.h-like macro for ProcessId formatting.
#  define PRIPID "d"

const ProcessHandle kInvalidProcessHandle = -1;
const ProcessId kInvalidProcessId = -1;

}  // namespace base

#endif  // BASE_PROCESS_H_
