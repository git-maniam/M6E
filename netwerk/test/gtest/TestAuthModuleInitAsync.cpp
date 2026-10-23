/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Atomics.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "nsCOMPtr.h"
#include "nsIAuthModule.h"
#include "nsString.h"
#include "nsThreadUtils.h"

namespace {

class InitCallback final : public nsIAuthModuleInitCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHODIMP OnInitDone(nsresult aResult) override {
    mOnMainThread = NS_IsMainThread();
    mResult = aResult;
    // Written last so that a caller spinning on mDone sees the other two.
    mDone = true;
    return NS_OK;
  }

  mozilla::Atomic<bool> mDone{false};
  bool mOnMainThread = false;
  nsresult mResult = NS_ERROR_NOT_INITIALIZED;

 private:
  ~InitCallback() = default;
};

NS_IMPL_ISUPPORTS(InitCallback, nsIAuthModuleInitCallback)

nsresult CallInitAsync(nsIAuthModule* aModule, const nsACString& aServiceName,
                       InitCallback* aCallback) {
  return aModule->InitAsync(aServiceName, nsIAuthModule::REQ_DEFAULT, u""_ns,
                            u""_ns, u""_ns, aCallback);
}

}  // namespace

// NegotiateAuthState::InitAsync and GetNextTokenRunnable::Run both fall back to
// the synchronous nsIAuthModule::Init on NS_ERROR_NOT_IMPLEMENTED, so a module
// that grows a partial initAsync implementation without returning that would
// silently skip initialization.
TEST(TestAuthModuleInitAsync, ModulesWithoutAsyncInitReportNotImplemented)
{
  static const char* kTypes[] = {"kerb-gss", "negotiate-gss", "sasl-gssapi",
                                 "ntlm"};

  for (const char* type : kTypes) {
    nsCOMPtr<nsIAuthModule> module = nsIAuthModule::CreateInstance(type);
    if (!module) {
      // Not all module types can be instantiated in every configuration.
      continue;
    }

    RefPtr<InitCallback> callback = new InitCallback();
    nsresult rv = CallInitAsync(module, "HTTP@localhost"_ns, callback);
    EXPECT_EQ(rv, NS_ERROR_NOT_IMPLEMENTED) << "type: " << type;
    EXPECT_FALSE(callback->mDone) << "type: " << type;
  }
}

