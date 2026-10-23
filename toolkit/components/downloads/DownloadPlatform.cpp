/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DownloadPlatform.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsINestedURI.h"
#include "nsIProtocolHandler.h"
#include "nsIURI.h"
#include "nsIFile.h"
#include "xpcpublic.h"

#include "mozilla/dom/Promise.h"
#include "mozilla/Preferences.h"

#define PREF_BDM_ADDTORECENTDOCS "browser.download.manager.addToRecentDocs"


#  include <CoreFoundation/CoreFoundation.h>
#  include "../../../xpcom/io/CocoaFileUtils.h"


using namespace mozilla;
using dom::Promise;

DownloadPlatform* DownloadPlatform::gDownloadPlatformService = nullptr;

NS_IMPL_ISUPPORTS(DownloadPlatform, mozIDownloadPlatform);

DownloadPlatform* DownloadPlatform::GetDownloadPlatform() {
  if (!gDownloadPlatformService) {
    gDownloadPlatformService = new DownloadPlatform();
  }

  NS_ADDREF(gDownloadPlatformService);

  return gDownloadPlatformService;
}


// Caller is responsible for freeing any result (CF Create Rule)
CFURLRef CreateCFURLFromNSIURI(nsIURI* aURI) {
  nsAutoCString spec;
  if (aURI) {
    aURI->GetSpec(spec);
  }

  CFStringRef urlStr = ::CFStringCreateWithCString(
      kCFAllocatorDefault, spec.get(), kCFStringEncodingUTF8);
  if (!urlStr) {
    return nullptr;
  }

  CFURLRef url = ::CFURLCreateWithString(kCFAllocatorDefault, urlStr, nullptr);

  ::CFRelease(urlStr);

  return url;
}


nsresult DownloadPlatform::DownloadDone(nsIURI* aSource, nsIURI* aReferrer,
                                        nsIFile* aTarget,
                                        const nsACString& aContentType,
                                        bool aIsPrivate, JSContext* aCx,
                                        Promise** aPromise) {
  nsIGlobalObject* globalObject =
      xpc::NativeGlobal(JS::CurrentGlobalOrNull(aCx));

  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(globalObject, result);

  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  nsresult rv = NS_OK;
  bool pendingAsyncOperations = false;

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(MOZ_WIDGET_ANDROID) || \
    defined(MOZ_WIDGET_GTK)

  nsAutoString path;
  if (aTarget && NS_SUCCEEDED(aTarget->GetPath(path))) {

    // On OS X, make the downloads stack bounce.
    CFStringRef observedObject = ::CFStringCreateWithCString(
        kCFAllocatorDefault, NS_ConvertUTF16toUTF8(path).get(),
        kCFStringEncodingUTF8);
    CFNotificationCenterRef center =
        ::CFNotificationCenterGetDistributedCenter();
    ::CFNotificationCenterPostNotification(
        center, CFSTR("com.apple.DownloadFileFinished"), observedObject,
        nullptr, TRUE);
    ::CFRelease(observedObject);

    // Add OS X origin and referrer file metadata
    CFStringRef pathCFStr = nullptr;
    if (!path.IsEmpty()) {
      pathCFStr = ::CFStringCreateWithCharacters(
          kCFAllocatorDefault, (const UniChar*)path.get(), path.Length());
    }
    if (pathCFStr && !aIsPrivate) {
      bool isFromWeb = IsURLPossiblyFromWeb(aSource);
      nsCOMPtr<nsIURI> source(aSource);
      nsCOMPtr<nsIURI> referrer(aReferrer);

      rv = NS_DispatchBackgroundTask(
          NS_NewRunnableFunction(
              "DownloadPlatform::DownloadDone",
              [pathCFStr, isFromWeb, source, referrer, promise]() mutable {
                CFURLRef sourceCFURL = CreateCFURLFromNSIURI(source);
                CFURLRef referrerCFURL = CreateCFURLFromNSIURI(referrer);

                CocoaFileUtils::AddOriginMetadataToFile(pathCFStr, sourceCFURL,
                                                        referrerCFURL);
                CocoaFileUtils::AddQuarantineMetadataToFile(
                    pathCFStr, sourceCFURL, referrerCFURL, isFromWeb);
                ::CFRelease(pathCFStr);
                if (sourceCFURL) {
                  ::CFRelease(sourceCFURL);
                }
                if (referrerCFURL) {
                  ::CFRelease(referrerCFURL);
                }

                DebugOnly<nsresult> rv =
                    NS_DispatchToMainThread(NS_NewRunnableFunction(
                        "DownloadPlatform::DownloadDoneResolve",
                        [promise = std::move(promise)]() {
                          promise->MaybeResolveWithUndefined();
                        }));
                MOZ_ASSERT(NS_SUCCEEDED(rv));
                // In non-debug builds, if we've for some reason failed to
                // dispatch a runnable to the main thread to resolve the
                // Promise, then it's unlikely we can reject it either. At that
                // point, the Promise is going to remain in pending limbo until
                // its global goes away.
              }),
          NS_DISPATCH_EVENT_MAY_BLOCK);

      if (NS_SUCCEEDED(rv)) {
        pendingAsyncOperations = true;
      }
    }
  }

#endif

  if (!pendingAsyncOperations) {
    promise->MaybeResolveWithUndefined();
  }
  promise.forget(aPromise);
  return rv;
}

nsresult DownloadPlatform::MaybeWriteDownloadOriginInformation(
    nsIFile* aTargetFile, nsIURI* aSourceUrl, nsIReferrerInfo* aReferrerInfo,
    bool aIsPrivate, JSContext* aCx, dom::Promise** aPromise) {
  NS_ENSURE_ARG(aCx);
  NS_ENSURE_ARG(aPromise);

  nsIGlobalObject* globalObject =
      xpc::NativeGlobal(JS::CurrentGlobalOrNull(aCx));
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_FAILURE;
  }

  mozilla::ErrorResult result;
  RefPtr domPromise = dom::Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  // Not an error but nothing was written.
  domPromise->MaybeResolve(false);

  domPromise.forget(aPromise);
  return NS_OK;
}

// Check if a URI is likely to be web-based, by checking its URI flags.
// If in doubt (e.g. if anything fails during the check) claims things
// are from the web.
bool DownloadPlatform::IsURLPossiblyFromWeb(nsIURI* aURI) {
  nsCOMPtr<nsIIOService> ios = do_GetIOService();
  nsCOMPtr<nsIURI> uri = aURI;
  if (!ios) {
    return true;
  }

  while (uri) {
    // We're not using NS_URIChainHasFlags because we're checking for *any* of 3
    // flags to be present on *all* of the nested URIs, which it can't do.
    uint32_t flags;
    nsresult rv = ios->GetDynamicProtocolFlags(uri, &flags);
    if (NS_FAILED(rv)) {
      return true;
    }
    // If not dangerous to load, not a UI resource and not a local file,
    // assume this is from the web:
    if (!(flags & nsIProtocolHandler::URI_DANGEROUS_TO_LOAD) &&
        !(flags & nsIProtocolHandler::URI_IS_UI_RESOURCE) &&
        !(flags & nsIProtocolHandler::URI_IS_LOCAL_FILE)) {
      return true;
    }
    // Otherwise, check if the URI is nested, and if so go through
    // the loop again:
    nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(uri);
    uri = nullptr;
    if (nestedURI) {
      rv = nestedURI->GetInnerURI(getter_AddRefs(uri));
      if (NS_FAILED(rv)) {
        return true;
      }
    }
  }
  return false;
}
