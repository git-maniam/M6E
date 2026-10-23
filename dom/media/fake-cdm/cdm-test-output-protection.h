/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_FAKE_CDM_CDM_TEST_OUTPUT_PROTECTION_H_
#define DOM_MEDIA_FAKE_CDM_CDM_TEST_OUTPUT_PROTECTION_H_


#include "cdm-test-decryptor.h"

namespace mozilla::cdmtest {


static void RunOutputProtectionAPITests() {
}

static void TestOuputProtectionAPIs() {
  RunOutputProtectionAPITests();
  FakeDecryptor::Message("OP tests completed");
}

}  // namespace mozilla::cdmtest

#endif  // DOM_MEDIA_FAKE_CDM_CDM_TEST_OUTPUT_PROTECTION_H_
