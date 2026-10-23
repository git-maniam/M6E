/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "mozilla/ProcessType.h"

#  include <mach/mach_types.h>
#  include <unistd.h>
#  include "mac/crash_generation/client_info.h"
#  include "mac/crash_generation/crash_generation_server.h"
#  include "mac/handler/exception_handler.h"
using breakpad_char = char;
using breakpad_string = std::string;
using breakpad_init_type = const char*;
using breakpad_pid = pid_t;
using ExtraCrashData = void;

#ifdef MOZ_PHC

#  include "PHC.h"

namespace mozilla::phc {

// HACK: The breakpad code expects this global variable even though we don't
// use it in the wrapper.
constinit mozilla::phc::AddrInfo gAddrInfo;

}  // namespace mozilla::phc

#endif  // defined(MOZ_PHC)

using google_breakpad::ClientInfo;
using google_breakpad::CrashGenerationServer;
using google_breakpad::ExceptionHandler;

// These structs and the callback below must be kept in sync with the
// corresponding Rust code in crash_helper_server/src/crash_generation.rs.
struct BreakpadProcessId {
  breakpad_pid pid;
  task_t task;
};

using RustDumpCallback = void (*)(const void*, BreakpadProcessId,
                                  const ExtraCrashData*, const breakpad_char*);

struct BreakpadContext {
  RustDumpCallback callback;
  const void* generator;
};


void onClientDumpRequestCallback(void* context, const ClientInfo& client_info,
                                 const breakpad_string& file_path) {
  BreakpadContext* breakpad_context = static_cast<BreakpadContext*>(context);
  RustDumpCallback callback = breakpad_context->callback;
  BreakpadProcessId process_id = {
      .pid = client_info.pid(),
      .task = client_info.task(),
  };
  const ExtraCrashData* extra_data = nullptr;

  callback(breakpad_context->generator, process_id, extra_data,
           file_path.c_str());
}



extern "C" void* CrashGenerationServer_init(breakpad_init_type aBreakpadData,
                                            const breakpad_char* aMinidumpPath,
                                            BreakpadContext* aContext) {
  breakpad_string minidumpPath(aMinidumpPath);
  breakpad_init_type breakpadData = aBreakpadData;

  CrashGenerationServer* server = new CrashGenerationServer(
      breakpadData,
      /* filter */ nullptr,
      /* filter_context */ nullptr, onClientDumpRequestCallback,
      reinterpret_cast<void*>(aContext),
      /* exit_callback */ nullptr,
      /* exit_context */ nullptr,
      /* generate_dumps */ true, minidumpPath);

  if (!server->Start()) {
    delete server;
    return nullptr;
  }

  return server;
}


extern "C" void CrashGenerationServer_shutdown(void* aServer) {
  CrashGenerationServer* server = static_cast<CrashGenerationServer*>(aServer);
  delete server;
}

extern "C" void CrashGenerationServer_set_path(
    void* aServer, const breakpad_char* aMinidumpPath) {
  CrashGenerationServer* server = static_cast<CrashGenerationServer*>(aServer);
  server->SetPath(aMinidumpPath);
}

struct MinidumpCallbackResult {
  breakpad_char* path;
  const size_t len;
};


bool WriteMinidumpCallback(const char* aDumpDir, const char* aMinidumpId,
                           void* aContext,
                           const mozilla::phc::AddrInfo* aAddrInfo,
                           bool aSucceeded) {
  if (aSucceeded) {
    MinidumpCallbackResult* result =
        reinterpret_cast<MinidumpCallbackResult*>(aContext);

    snprintf(result->path, result->len, "%s/%s.dmp", aDumpDir, aMinidumpId);
  }

  return aSucceeded;
}

extern "C" bool WriteMinidumpForProcess(GeckoChildID aID, mach_port_t aProcess,
                                        mach_port_t aThread,
                                        const breakpad_char* aDumpPath,
                                        breakpad_char* aResultPath,
                                        size_t aResultPathLen) {
  MinidumpCallbackResult result{aResultPath, aResultPathLen};
  breakpad_string dump_path(aDumpPath);

  // Fully qualified to avoid conflicts with OS header `MachineExceptions.h`.
  bool res = google_breakpad::ExceptionHandler::WriteMinidumpForChild(
      aProcess, aThread, dump_path, WriteMinidumpCallback, &result);

  return res;
}

