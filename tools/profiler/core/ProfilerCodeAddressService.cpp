/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProfilerCodeAddressService.h"

#include "platform.h"
#include "mozilla/StackWalk.h"

using namespace mozilla;


bool ProfilerCodeAddressService::GetFunction(const void* aPc,
                                             nsACString& aResult) {
  Entry& entry = GetEntry(aPc);


  if (!entry.mFunction || entry.mFunction[0] == '\0') {
    return false;
  }

  aResult = nsDependentCString(entry.mFunction);
  return true;
}
