/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFileProtocolHandler.h"

#include "mozilla/net/NeckoCommon.h"
#include "nsFileChannel.h"
#include "nsIFile.h"
#include "nsIURIMutator.h"
#include "nsNetUtil.h"
#include "nsStandardURL.h"
#include "nsURLHelper.h"

// URL file handling, copied and modified from
// xpfe/components/bookmarks/src/nsBookmarksService.cpp

// URL file handling for freedesktop.org
#  include "nsINIParser.h"
#  define DESKTOP_ENTRY_SECTION "Desktop Entry"

//-----------------------------------------------------------------------------

nsresult nsFileProtocolHandler::Init() { return NS_OK; }

NS_IMPL_ISUPPORTS(nsFileProtocolHandler, nsIFileProtocolHandler,
                  nsIProtocolHandler, nsISupportsWeakReference)

//-----------------------------------------------------------------------------
// nsIProtocolHandler methods:

NS_IMETHODIMP
nsFileProtocolHandler::ReadURLFile(nsIFile* aFile, nsIURI** aURI) {
  // We only support desktop files that end in ".desktop" like the spec says:
  // http://standards.freedesktop.org/desktop-entry-spec/latest/ar01s02.html
  nsAutoCString leafName;
  nsresult rv = aFile->GetNativeLeafName(leafName);
  if (NS_FAILED(rv) || !StringEndsWith(leafName, ".desktop"_ns)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  bool isFile = false;
  rv = aFile->IsFile(&isFile);
  if (NS_FAILED(rv) || !isFile) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsINIParser parser;
  rv = parser.Init(aFile);
  if (NS_FAILED(rv)) return rv;

  nsAutoCString type;
  parser.GetString(DESKTOP_ENTRY_SECTION, "Type", type);
  if (!type.EqualsLiteral("Link")) return NS_ERROR_NOT_AVAILABLE;

  nsAutoCString url;
  rv = parser.GetString(DESKTOP_ENTRY_SECTION, "URL", url);
  if (NS_FAILED(rv) || url.IsEmpty()) return NS_ERROR_NOT_AVAILABLE;

  return NS_NewURI(aURI, url);
}


NS_IMETHODIMP
nsFileProtocolHandler::ReadShellLink(nsIFile* aFile, nsIURI** aURI) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsFileProtocolHandler::GetScheme(nsACString& result) {
  result.AssignLiteral("file");
  return NS_OK;
}

NS_IMETHODIMP
nsFileProtocolHandler::NewChannel(nsIURI* uri, nsILoadInfo* aLoadInfo,
                                  nsIChannel** result) {
  nsresult rv;

  RefPtr<nsFileChannel> chan = new nsFileChannel(uri);

  // file URLs with hostnames should be considered an error
  // Before bug 1507354 file URLs ignored the host part. So we intentionally
  // fail the channel if the file URL has a host to prevent any security issues
  // that might arise.
  nsAutoCString host;
  rv = uri->GetHost(host);
  NS_ENSURE_SUCCESS(rv, rv);
  if (NS_WARN_IF(!host.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  // set the loadInfo on the new channel ; must do this
  // before calling Init() on it, since it needs the load
  // info be already set.
  rv = chan->SetLoadInfo(aLoadInfo);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = chan->Init();
  if (NS_FAILED(rv)) {
    return rv;
  }

  *result = chan.forget().downcast<nsBaseChannel>().take();
  return NS_OK;
}

NS_IMETHODIMP
nsFileProtocolHandler::AllowPort(int32_t port, const char* scheme,
                                 bool* result) {
  // don't override anything.
  *result = false;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsIFileProtocolHandler methods:

NS_IMETHODIMP
nsFileProtocolHandler::NewFileURI(nsIFile* aFile, nsIURI** aResult) {
  NS_ENSURE_ARG_POINTER(aFile);

  RefPtr<nsIFile> file(aFile);
  // NOTE: the origin charset is assigned the value of the platform
  // charset by the SetFile method.
  return NS_MutateURI(new mozilla::net::nsStandardURL::Mutator())
      .Apply(&nsIFileURLMutator::SetFile, file)
      .Finalize(aResult);
}

NS_IMETHODIMP
nsFileProtocolHandler::NewFileURIMutator(nsIFile* aFile,
                                         nsIURIMutator** aResult) {
  NS_ENSURE_ARG_POINTER(aFile);
  nsresult rv;

  nsCOMPtr<nsIURIMutator> mutator = new mozilla::net::nsStandardURL::Mutator();
  nsCOMPtr<nsIFileURLMutator> fileMutator = do_QueryInterface(mutator, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // NOTE: the origin charset is assigned the value of the platform
  // charset by the SetFile method.
  rv = fileMutator->SetFile(aFile);
  if (NS_FAILED(rv)) {
    return rv;
  }

  mutator.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsFileProtocolHandler::GetURLSpecFromFile(nsIFile* file, nsACString& result) {
  NS_ENSURE_ARG_POINTER(file);
  return net_GetURLSpecFromFile(file, result);
}

NS_IMETHODIMP
nsFileProtocolHandler::GetURLSpecFromActualFile(nsIFile* file,
                                                nsACString& result) {
  NS_ENSURE_ARG_POINTER(file);
  return net_GetURLSpecFromActualFile(file, result);
}

NS_IMETHODIMP
nsFileProtocolHandler::GetURLSpecFromDir(nsIFile* file, nsACString& result) {
  NS_ENSURE_ARG_POINTER(file);
  return net_GetURLSpecFromDir(file, result);
}

NS_IMETHODIMP
nsFileProtocolHandler::GetFileFromURLSpec(const nsACString& spec,
                                          nsIFile** result) {
  return net_GetFileFromURLSpec(spec, result);
}
