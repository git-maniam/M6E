/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EnterpriseRoots.h"

#include "PKCS11ModuleDB.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Logging.h"
#include "mozpkix/Result.h"
#include "nsCRT.h"
#include "nsThreadUtils.h"


#  include <Security/Security.h>

#  include "KeychainSecret.h"


extern mozilla::LazyLogModule gPIPNSSLog;

using namespace mozilla;
using namespace psm;

void EnterpriseCert::CopyBytes(nsTArray<uint8_t>& dest) const {
  dest.Assign(mDER);
}

pkix::Result EnterpriseCert::GetInput(pkix::Input& input) const {
  return input.Init(mDER.Elements(), mDER.Length());
}

bool EnterpriseCert::GetIsRoot() const { return mIsRoot; }

bool EnterpriseCert::IsKnownRoot(UniqueSECMODModule& rootsModule) {
  if (!rootsModule) {
    return false;
  }

  SECItem certItem = {siBuffer, mDER.Elements(),
                      static_cast<unsigned int>(mDER.Length())};
  AutoSECMODListReadLock lock;
  for (int i = 0; i < rootsModule->slotCount; i++) {
    PK11SlotInfo* slot = rootsModule->slots[i];
    if (PK11_FindEncodedCertInSlot(slot, &certItem, nullptr) !=
        CK_INVALID_HANDLE) {
      return true;
    }
  }
  return false;
}


enum class CertificateTrustResult {
  CanUseAsIntermediate,
  CanUseAsTrustAnchor,
  DoNotUse,
};

ScopedCFType<CFArrayRef> GetCertificateTrustSettingsInDomain(
    const SecCertificateRef certificate, SecTrustSettingsDomain domain) {
  CFArrayRef trustSettingsRaw;
  OSStatus rv =
      SecTrustSettingsCopyTrustSettings(certificate, domain, &trustSettingsRaw);
  if (rv != errSecSuccess || !trustSettingsRaw) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("  SecTrustSettingsCopyTrustSettings failed (or not found) for "
             "domain %" PRIu32,
             domain));
    return nullptr;
  }
  ScopedCFType<CFArrayRef> trustSettings(trustSettingsRaw);
  return trustSettings;
}

// This function processes trust settings returned by
// SecTrustSettingsCopyTrustSettings. See the documentation at
// https://developer.apple.com/documentation/security/1400261-sectrustsettingscopytrustsetting
// `trustSettings` is an array of CFDictionaryRef. Each dictionary may impose
// a constraint.
CertificateTrustResult ProcessCertificateTrustSettings(
    ScopedCFType<CFArrayRef>& trustSettings) {
  // If the array is empty, the certificate is a trust anchor.
  const CFIndex numTrustDictionaries = CFArrayGetCount(trustSettings.get());
  if (numTrustDictionaries == 0) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("  empty trust settings -> trust anchor"));
    return CertificateTrustResult::CanUseAsTrustAnchor;
  }
  CertificateTrustResult currentTrustSettings =
      CertificateTrustResult::CanUseAsIntermediate;
  for (CFIndex i = 0; i < numTrustDictionaries; i++) {
    CFDictionaryRef trustDictionary = reinterpret_cast<CFDictionaryRef>(
        CFArrayGetValueAtIndex(trustSettings.get(), i));
    // kSecTrustSettingsApplication specifies an external application that
    // determines the certificate's trust settings.
    // kSecTrustSettingsPolicyString appears to be a mechanism like name
    // constraints.
    // These are not supported, so conservatively assume this certificate is
    // distrusted if either are present.
    if (CFDictionaryContainsKey(trustDictionary,
                                kSecTrustSettingsApplication) ||
        CFDictionaryContainsKey(trustDictionary,
                                kSecTrustSettingsPolicyString)) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("  found unsupported policy -> assuming distrusted"));
      return CertificateTrustResult::DoNotUse;
    }

    // kSecTrustSettingsKeyUsage seems to be essentially the equivalent of the
    // x509 keyUsage extension. For parity, we allow
    // kSecTrustSettingsKeyUseSignature, kSecTrustSettingsKeyUseSignCert, and
    // kSecTrustSettingsKeyUseAny.
    if (CFDictionaryContainsKey(trustDictionary, kSecTrustSettingsKeyUsage)) {
      CFNumberRef keyUsage = (CFNumberRef)CFDictionaryGetValue(
          trustDictionary, kSecTrustSettingsKeyUsage);
      int32_t keyUsageValue;
      if (!keyUsage ||
          CFNumberGetValue(keyUsage, kCFNumberSInt32Type, &keyUsageValue) ||
          keyUsageValue < 0) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                ("  no trust settings key usage or couldn't get value"));
        return CertificateTrustResult::DoNotUse;
      }
      switch ((uint64_t)keyUsageValue) {
        case kSecTrustSettingsKeyUseSignature:  // fall-through
        case kSecTrustSettingsKeyUseSignCert:   // fall-through
        case kSecTrustSettingsKeyUseAny:
          break;
        default:
          return CertificateTrustResult::DoNotUse;
      }
    }

    // If there is a specific policy, ensure that it's for the
    // 'kSecPolicyAppleSSL' policy, which is the TLS server auth policy (i.e.
    // x509 + domain name checking).
    if (CFDictionaryContainsKey(trustDictionary, kSecTrustSettingsPolicy)) {
      SecPolicyRef policy = (SecPolicyRef)CFDictionaryGetValue(
          trustDictionary, kSecTrustSettingsPolicy);
      if (!policy) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                ("  kSecTrustSettingsPolicy present, but null?"));
        continue;
      }
      ScopedCFType<CFDictionaryRef> policyProperties(
          SecPolicyCopyProperties(policy));
      CFStringRef policyOid = (CFStringRef)CFDictionaryGetValue(
          policyProperties.get(), kSecPolicyOid);
      if (!CFEqual(policyOid, kSecPolicyAppleSSL)) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("  policy doesn't match"));
        continue;
      }
    }

    // By default, the trust setting result value is
    // kSecTrustSettingsResultTrustRoot.
    int32_t trustSettingsValue = kSecTrustSettingsResultTrustRoot;
    if (CFDictionaryContainsKey(trustDictionary, kSecTrustSettingsResult)) {
      CFNumberRef trustSetting = (CFNumberRef)CFDictionaryGetValue(
          trustDictionary, kSecTrustSettingsResult);
      if (!trustSetting || !CFNumberGetValue(trustSetting, kCFNumberSInt32Type,
                                             &trustSettingsValue)) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                ("  no trust settings result or couldn't get value"));
        continue;
      }
    }
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("  trust setting: %d", trustSettingsValue));
    if (trustSettingsValue == kSecTrustSettingsResultDeny) {
      return CertificateTrustResult::DoNotUse;
    }
    if (trustSettingsValue == kSecTrustSettingsResultTrustRoot ||
        trustSettingsValue == kSecTrustSettingsResultTrustAsRoot) {
      currentTrustSettings = CertificateTrustResult::CanUseAsTrustAnchor;
    }
  }
  return currentTrustSettings;
}

CertificateTrustResult GetCertificateTrustResult(
    const SecCertificateRef certificate) {
  ScopedCFType<CFStringRef> subject(
      SecCertificateCopySubjectSummary(certificate));
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("determining trust for '%s'",
           CFStringGetCStringPtr(subject.get(), kCFStringEncodingUTF8)));
  // There are three trust settings domains: kSecTrustSettingsDomainUser,
  // kSecTrustSettingsDomainAdmin, and kSecTrustSettingsDomainSystem. User
  // overrides admin and admin overrides system. However, if the given
  // certificate has trust settings in the system domain, it shipped with the
  // OS, so we don't want to use it.
  ScopedCFType<CFArrayRef> systemTrustSettings(
      GetCertificateTrustSettingsInDomain(certificate,
                                          kSecTrustSettingsDomainSystem));
  if (systemTrustSettings) {
    return CertificateTrustResult::DoNotUse;
  }

  // At this point, if there is no trust information regarding this
  // certificate, it can be used as an intermediate.
  CertificateTrustResult certificateTrustResult =
      CertificateTrustResult::CanUseAsIntermediate;

  // Process trust information in the user domain, if any.
  ScopedCFType<CFArrayRef> userTrustSettings(
      GetCertificateTrustSettingsInDomain(certificate,
                                          kSecTrustSettingsDomainUser));
  if (userTrustSettings) {
    certificateTrustResult = ProcessCertificateTrustSettings(userTrustSettings);
    // If there is definite information one way or another (either indicating
    // this is a trusted root or a distrusted certificate), use that
    // information.
    if (certificateTrustResult !=
        CertificateTrustResult::CanUseAsIntermediate) {
      return certificateTrustResult;
    }
  }

  // Process trust information in the admin domain, if any.
  ScopedCFType<CFArrayRef> adminTrustSettings(
      GetCertificateTrustSettingsInDomain(certificate,
                                          kSecTrustSettingsDomainAdmin));
  if (adminTrustSettings) {
    certificateTrustResult =
        ProcessCertificateTrustSettings(adminTrustSettings);
  }

  // Use whatever result we ended up with.
  return certificateTrustResult;
}

OSStatus GatherEnterpriseCertsMacOS(nsTArray<EnterpriseCert>& certs,
                                    UniqueSECMODModule& rootsModule) {
  // The following builds a search dictionary corresponding to:
  // { class: "certificate",
  //   match limit: "match all" }
  // This operates on items that have been added to the keychain and thus gives
  // us all 3rd party certificates. Unfortunately, if a root that shipped with
  // the OS has had its trust settings changed, it can also be returned from
  // this query. Further work (below) filters such certificates out.
  const CFStringRef keys[] = {kSecClass, kSecMatchLimit};
  const void* values[] = {kSecClassCertificate, kSecMatchLimitAll};
  static_assert(std::size(keys) == std::size(values),
                "mismatched SecItemCopyMatching key/value array sizes");
  // https://developer.apple.com/documentation/corefoundation/1516782-cfdictionarycreate
  ScopedCFType<CFDictionaryRef> searchDictionary(CFDictionaryCreate(
      nullptr, (const void**)&keys, (const void**)&values, std::size(keys),
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  CFTypeRef items;
  // https://developer.apple.com/documentation/security/1398306-secitemcopymatching
  OSStatus rv = SecItemCopyMatching(searchDictionary.get(), &items);
  if (rv != errSecSuccess) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("SecItemCopyMatching failed"));
    return rv;
  }
  // If given a match limit greater than 1 (which we did), SecItemCopyMatching
  // returns a CFArrayRef.
  ScopedCFType<CFArrayRef> arr(reinterpret_cast<CFArrayRef>(items));
  CFIndex count = CFArrayGetCount(arr.get());
  uint32_t numImported = 0;
  for (CFIndex i = 0; i < count; i++) {
    // Because we asked for certificates, each CFTypeRef in the array is really
    // a SecCertificateRef.
    const SecCertificateRef certificate =
        (const SecCertificateRef)CFArrayGetValueAtIndex(arr.get(), i);
    CertificateTrustResult certificateTrustResult =
        GetCertificateTrustResult(certificate);
    if (certificateTrustResult == CertificateTrustResult::DoNotUse) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("skipping distrusted cert"));
      continue;
    }
    ScopedCFType<CFDataRef> der(SecCertificateCopyData(certificate));
    if (!der) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("couldn't get bytes of certificate?"));
      continue;
    }
    bool isRoot =
        certificateTrustResult == CertificateTrustResult::CanUseAsTrustAnchor;
    EnterpriseCert enterpriseCert(CFDataGetBytePtr(der.get()),
                                  CFDataGetLength(der.get()), isRoot);
    if (enterpriseCert.GetIsRoot() ||
        !enterpriseCert.IsKnownRoot(rootsModule)) {
      certs.AppendElement(std::move(enterpriseCert));
      numImported++;
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("importing as %s", isRoot ? "root" : "intermediate"));
    } else {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("skipping intermediate that is a known root cert"));
    }
  }
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("imported %u certs", numImported));
  return errSecSuccess;
}


nsresult GatherEnterpriseCerts(nsTArray<EnterpriseCert>& certs) {
  MOZ_ASSERT(!NS_IsMainThread());
  if (NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  certs.Clear();
  UniqueSECMODModule rootsModule(SECMOD_FindModule(kRootModuleName.get()));
  OSStatus rv = GatherEnterpriseCertsMacOS(certs, rootsModule);
  if (rv != errSecSuccess) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}
