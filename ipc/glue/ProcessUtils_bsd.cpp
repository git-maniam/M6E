/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProcessUtils.h"

#include <pthread.h>

#  include <pthread_np.h>

namespace mozilla {
namespace ipc {

void SetThisProcessName(const char* aName) {
  pthread_set_name_np(pthread_self(), aName);
}

}  // namespace ipc
}  // namespace mozilla
