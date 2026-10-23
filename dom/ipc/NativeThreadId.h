/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_NativeThreadId_h
#define mozilla_dom_NativeThreadId_h

#  include "mozilla/UniquePtrExtensions.h"

namespace mozilla::dom {
typedef mozilla::UniqueMachSendRight NativeThreadId;
}  // namespace mozilla::dom

#endif
