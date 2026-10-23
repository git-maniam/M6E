/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_ipc_glue_UtilityProcessImpl_h_
#define _include_ipc_glue_UtilityProcessImpl_h_
#include "mozilla/ipc/ProcessChild.h"


#include "mozilla/ipc/UtilityProcessChild.h"

namespace mozilla::ipc {

// This class owns the subprocess instance of a PUtilityProcess - which in this
// case, is a UtilityProcessParent. It is instantiated as a singleton in
// XRE_InitChildProcess.
class UtilityProcessImpl final : public ipc::ProcessChild {
 public:
  using ipc::ProcessChild::ProcessChild;
  ~UtilityProcessImpl();

  bool Init(int aArgc, char* aArgv[]) override;
  void CleanUp() override;


 private:
  RefPtr<UtilityProcessChild> mUtility = UtilityProcessChild::GetSingleton();

};

}  // namespace mozilla::ipc

#endif  // _include_ipc_glue_UtilityProcessImpl_h_
