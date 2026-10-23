/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSystemInfo.h"

#include "js/PropertyAndElement.h"  // JS_SetProperty
#include "jsapi.h"
#include "mozilla/Hal.h"
#include "mozilla/LazyIdleThread.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/SSE.h"
#include "mozilla/Sprintf.h"
#include "mozilla/Try.h"
#include "mozilla/Vector.h"
#include "mozilla/arm.h"
#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/glean/XpcomMetrics.h"
#include "nsAppRunner.h"
#include "prio.h"
#include "prsystem.h"


#  include "MacHelpers.h"




#  include <sys/sysctl.h>


// Slot for NS_InitXPCOM to pass information to nsSystemInfo::Init.
// Only set to nonzero (potentially) if XP_UNIX.  On such systems, the
// system call to discover the appropriate value is not thread-safe,
// so we must call it before going multithreaded, but nsSystemInfo::Init
// only happens well after that point.
uint32_t nsSystemInfo::gUserUmask = 0;

using namespace mozilla::dom;





static nsresult GetAppleModelId(nsAutoCString& aModelId) {
  size_t numChars = 0;
  size_t result = sysctlbyname("hw.model", nullptr, &numChars, nullptr, 0);
  if (result != 0 || !numChars) {
    return NS_ERROR_FAILURE;
  }
  aModelId.SetLength(numChars);
  result =
      sysctlbyname("hw.model", aModelId.BeginWriting(), &numChars, nullptr, 0);
  if (result != 0) {
    return NS_ERROR_FAILURE;
  }
  // numChars includes null terminator
  aModelId.Truncate(numChars - 1);
  return NS_OK;
}

static nsresult ProcessIsRosettaTranslated(bool& isRosetta) {
#  if defined(__aarch64__)
  // There is no need to call sysctlbyname() if we are running as arm64.
  isRosetta = false;
#  else
  int ret = 0;
  size_t size = sizeof(ret);
  if (sysctlbyname("sysctl.proc_translated", &ret, &size, nullptr, 0) == -1) {
    if (errno != ENOENT) {
      fprintf(stderr, "Failed to check for translation environment\n");
    }
    isRosetta = false;
  } else {
    isRosetta = (ret == 1);
  }
#  endif
  return NS_OK;
}



using namespace mozilla;

nsSystemInfo::nsSystemInfo() = default;

nsSystemInfo::~nsSystemInfo() = default;

// CPU-specific information.
static const struct PropItems {
  const char* name;
  bool (*propfun)(void);
} cpuPropItems[] = {
    // x86-specific bits.
    {"hasMMX", mozilla::supports_mmx},
    {"hasSSE", mozilla::supports_sse},
    {"hasSSE2", mozilla::supports_sse2},
    {"hasSSE3", mozilla::supports_sse3},
    {"hasSSSE3", mozilla::supports_ssse3},
    {"hasSSE4A", mozilla::supports_sse4a},
    {"hasSSE4_1", mozilla::supports_sse4_1},
    {"hasSSE4_2", mozilla::supports_sse4_2},
    {"hasAVX", mozilla::supports_avx},
    {"hasAVX2", mozilla::supports_avx2},
    {"hasAES", mozilla::supports_aes},
    // ARM-specific bits.
    {"hasEDSP", mozilla::supports_edsp},
    {"hasARMv6", mozilla::supports_armv6},
    {"hasARMv7", mozilla::supports_armv7},
    {"hasNEON", mozilla::supports_neon}};

nsresult CollectProcessInfo(ProcessInfo& info) {
  nsAutoCString cpuVendor;
  nsAutoCString cpuName;
  int cpuSpeed = -1;
  int cpuFamily = -1;
  int cpuModel = -1;
  int cpuStepping = -1;
  int logicalCPUs = -1;
  int physicalCPUs = -1;
  int cacheSizeL2 = -1;
  int cacheSizeL3 = -1;

  // CPU speed
  uint64_t sysctlValue64 = 0;
  uint32_t sysctlValue32 = 0;
  size_t len = 0;
  len = sizeof(sysctlValue64);
  if (!sysctlbyname("hw.cpufrequency_max", &sysctlValue64, &len, nullptr, 0)) {
    cpuSpeed = static_cast<int>(sysctlValue64 / 1000000);
  }
  MOZ_ASSERT(sizeof(sysctlValue64) == len);

  len = sizeof(sysctlValue32);
  if (!sysctlbyname("hw.physicalcpu_max", &sysctlValue32, &len, nullptr, 0)) {
    physicalCPUs = static_cast<int>(sysctlValue32);
  }
  MOZ_ASSERT(sizeof(sysctlValue32) == len);

  len = sizeof(sysctlValue32);
  if (!sysctlbyname("hw.logicalcpu_max", &sysctlValue32, &len, nullptr, 0)) {
    logicalCPUs = static_cast<int>(sysctlValue32);
  }
  MOZ_ASSERT(sizeof(sysctlValue32) == len);

  len = sizeof(sysctlValue64);
  if (!sysctlbyname("hw.l2cachesize", &sysctlValue64, &len, nullptr, 0)) {
    cacheSizeL2 = static_cast<int>(sysctlValue64 / 1024);
  }
  MOZ_ASSERT(sizeof(sysctlValue64) == len);

  len = sizeof(sysctlValue64);
  if (!sysctlbyname("hw.l3cachesize", &sysctlValue64, &len, nullptr, 0)) {
    cacheSizeL3 = static_cast<int>(sysctlValue64 / 1024);
  }
  MOZ_ASSERT(sizeof(sysctlValue64) == len);

  if (!sysctlbyname("machdep.cpu.vendor", nullptr, &len, nullptr, 0)) {
    char* cpuVendorStr = new char[len];
    if (!sysctlbyname("machdep.cpu.vendor", cpuVendorStr, &len, nullptr, 0)) {
      cpuVendor = cpuVendorStr;
    }
    delete[] cpuVendorStr;
  }

  if (!sysctlbyname("machdep.cpu.brand_string", nullptr, &len, nullptr, 0)) {
    char* cpuNameStr = new char[len];
    if (!sysctlbyname("machdep.cpu.brand_string", cpuNameStr, &len, nullptr,
                      0)) {
      cpuName = cpuNameStr;
    }
    delete[] cpuNameStr;
  }

  len = sizeof(sysctlValue32);
  if (!sysctlbyname("machdep.cpu.family", &sysctlValue32, &len, nullptr, 0)) {
    cpuFamily = static_cast<int>(sysctlValue32);
  }
  MOZ_ASSERT(sizeof(sysctlValue32) == len);

  len = sizeof(sysctlValue32);
  if (!sysctlbyname("machdep.cpu.model", &sysctlValue32, &len, nullptr, 0)) {
    cpuModel = static_cast<int>(sysctlValue32);
  }
  MOZ_ASSERT(sizeof(sysctlValue32) == len);

  len = sizeof(sysctlValue32);
  if (!sysctlbyname("machdep.cpu.stepping", &sysctlValue32, &len, nullptr, 0)) {
    cpuStepping = static_cast<int>(sysctlValue32);
  }
  MOZ_ASSERT(sizeof(sysctlValue32) == len);

  if (const Maybe<hal::HeterogeneousCpuInfo>& hetCpuInfo =
          hal::GetHeterogeneousCpuInfo()) {
    info.cpuPCount = int32_t(hetCpuInfo->mBigCpus.Count());
    info.cpuMCount = int32_t(hetCpuInfo->mMediumCpus.Count());
    info.cpuECount = int32_t(hetCpuInfo->mLittleCpus.Count());
    if (XRE_IsParentProcess()) {
      glean::system_cpu::big_cores.Set(info.cpuPCount);
      glean::system_cpu::medium_cores.Set(info.cpuMCount);
      glean::system_cpu::little_cores.Set(info.cpuECount);
    }
  } else {
    info.cpuPCount = physicalCPUs;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::big_cores.Set(physicalCPUs);
    }
    info.cpuMCount = 0;
    info.cpuECount = 0;
  }

  if (cpuSpeed >= 0) {
    info.cpuSpeed = cpuSpeed;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::speed.Set(cpuSpeed);
    }
  } else {
    info.cpuSpeed = 0;
  }
  if (!cpuVendor.IsEmpty()) {
    info.cpuVendor = cpuVendor;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::vendor.Set(cpuVendor);
    }
  }
  if (!cpuName.IsEmpty()) {
    info.cpuName = cpuName;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::name.Set(cpuName);
    }
  }
  if (cpuFamily >= 0) {
    info.cpuFamily = cpuFamily;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::family.Set(cpuFamily);
    }
  }
  if (cpuModel >= 0) {
    info.cpuModel = cpuModel;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::model.Set(cpuModel);
    }
  }
  if (cpuStepping >= 0) {
    info.cpuStepping = cpuStepping;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::stepping.Set(cpuStepping);
    }
  }

  if (logicalCPUs >= 0) {
    info.cpuCount = logicalCPUs;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::logical_cores.Set(logicalCPUs);
    }
  }
  if (physicalCPUs >= 0) {
    info.cpuCores = physicalCPUs;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::physical_cores.Set(physicalCPUs);
    }
  }

  if (cacheSizeL2 >= 0) {
    info.l2cacheKB = cacheSizeL2;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::l2_cache.Set(cacheSizeL2);
    }
  }
  if (cacheSizeL3 >= 0) {
    info.l3cacheKB = cacheSizeL3;
    if (XRE_IsParentProcess()) {
      glean::system_cpu::l3_cache.Set(cacheSizeL3);
    }
  }

  return NS_OK;
}



nsresult nsSystemInfo::Init() {
  // check that it is called from the main thread on all platforms.
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;

  static const struct {
    PRSysInfo cmd;
    const char* name;
  } items[] = {{PR_SI_SYSNAME, "name"},
               {PR_SI_ARCHITECTURE, "arch"},
               {PR_SI_RELEASE, "version"},
               {PR_SI_RELEASE_BUILD, "build"}};

  for (auto item : items) {
    char buf[SYS_INFO_BUFFER_LENGTH];
    if (PR_GetSystemInfo(item.cmd, buf, sizeof(buf)) == PR_SUCCESS) {
      rv = SetPropertyAsACString(NS_ConvertASCIItoUTF16(item.name),
                                 nsDependentCString(buf));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    } else {
      NS_WARNING("PR_GetSystemInfo failed");
    }
  }

  SetPropertyAsBool(u"isPackagedApp"_ns, false);

  // Additional informations not available through PR_GetSystemInfo.
  SetInt32Property(u"pagesize"_ns, PR_GetPageSize());
  SetInt32Property(u"pageshift"_ns, PR_GetPageShift());
  SetInt32Property(u"memmapalign"_ns, PR_GetMemMapAlignment());
  SetUint64Property(u"memsize"_ns, PR_GetPhysicalMemorySize());
  SetUint32Property(u"umask"_ns, nsSystemInfo::gUserUmask);

#ifdef HAVE_64BIT_BUILD
  SetUint32Property(u"archbits"_ns, 64);
#else
  SetUint32Property(u"archbits"_ns, 32);
#endif

  uint64_t virtualMem = 0;

  if (virtualMem) SetUint64Property(u"virtualmemsize"_ns, virtualMem);

  for (auto cpuPropItem : cpuPropItems) {
    rv = SetPropertyAsBool(NS_ConvertASCIItoUTF16(cpuPropItem.name),
                           cpuPropItem.propfun());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }



  nsAutoCString modelId;
  if (NS_SUCCEEDED(GetAppleModelId(modelId))) {
    rv = SetPropertyAsACString(u"appleModelId"_ns, modelId);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  bool isRosetta;
  if (NS_SUCCEEDED(ProcessIsRosettaTranslated(isRosetta))) {
    rv = SetPropertyAsBool(u"rosettaStatus"_ns, isRosetta);
    NS_ENSURE_SUCCESS(rv, rv);
  }



  {
    nsAutoCString themeInfo;
    LookAndFeel::GetThemeInfo(themeInfo);
    MOZ_TRY(SetPropertyAsACString(u"osThemeInfo"_ns, themeInfo));
  }





  return NS_OK;
}


void nsSystemInfo::SetInt32Property(const nsAString& aPropertyName,
                                    const int32_t aValue) {
  NS_WARNING_ASSERTION(aValue > 0, "Unable to read system value");
  if (aValue > 0) {
#ifdef DEBUG
    nsresult rv =
#endif
        SetPropertyAsInt32(aPropertyName, aValue);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Unable to set property");
  }
}

void nsSystemInfo::SetUint32Property(const nsAString& aPropertyName,
                                     const uint32_t aValue) {
  // Only one property is currently set via this function.
  // It may legitimately be zero.
#ifdef DEBUG
  nsresult rv =
#endif
      SetPropertyAsUint32(aPropertyName, aValue);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Unable to set property");
}

void nsSystemInfo::SetUint64Property(const nsAString& aPropertyName,
                                     const uint64_t aValue) {
  NS_WARNING_ASSERTION(aValue > 0, "Unable to read system value");
  if (aValue > 0) {
#ifdef DEBUG
    nsresult rv =
#endif
        SetPropertyAsUint64(aPropertyName, aValue);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Unable to set property");
  }
}


JSObject* GetJSObjForProcessInfo(JSContext* aCx, const ProcessInfo& info) {
  JS::Rooted<JSObject*> jsInfo(aCx, JS_NewPlainObject(aCx));


  JS::Rooted<JS::Value> valCountInfo(aCx, JS::Int32Value(info.cpuCount));
  JS_SetProperty(aCx, jsInfo, "count", valCountInfo);

  JS::Rooted<JS::Value> valCoreInfo(
      aCx, info.cpuCores ? JS::Int32Value(info.cpuCores) : JS::NullValue());
  JS_SetProperty(aCx, jsInfo, "cores", valCoreInfo);

  JS::Rooted<JS::Value> valPCountInfo(
      aCx, info.cpuCores ? JS::Int32Value(info.cpuPCount) : JS::NullValue());
  JS_SetProperty(aCx, jsInfo, "pcount", valPCountInfo);

  JS::Rooted<JS::Value> valMCountInfo(
      aCx, info.cpuCores ? JS::Int32Value(info.cpuMCount) : JS::NullValue());
  JS_SetProperty(aCx, jsInfo, "mcount", valMCountInfo);

  JS::Rooted<JS::Value> valECountInfo(
      aCx, info.cpuCores ? JS::Int32Value(info.cpuECount) : JS::NullValue());
  JS_SetProperty(aCx, jsInfo, "ecount", valECountInfo);

  JSString* strVendor =
      JS_NewStringCopyN(aCx, info.cpuVendor.get(), info.cpuVendor.Length());
  JS::Rooted<JS::Value> valVendor(aCx, JS::StringValue(strVendor));
  JS_SetProperty(aCx, jsInfo, "vendor", valVendor);

  JSString* strName =
      JS_NewStringCopyN(aCx, info.cpuName.get(), info.cpuName.Length());
  JS::Rooted<JS::Value> valName(aCx, JS::StringValue(strName));
  JS_SetProperty(aCx, jsInfo, "name", valName);

  JS::Rooted<JS::Value> valFamilyInfo(aCx, JS::Int32Value(info.cpuFamily));
  JS_SetProperty(aCx, jsInfo, "family", valFamilyInfo);

  JS::Rooted<JS::Value> valModelInfo(aCx, JS::Int32Value(info.cpuModel));
  JS_SetProperty(aCx, jsInfo, "model", valModelInfo);

  JS::Rooted<JS::Value> valSteppingInfo(aCx, JS::Int32Value(info.cpuStepping));
  JS_SetProperty(aCx, jsInfo, "stepping", valSteppingInfo);

  JS::Rooted<JS::Value> valL2CacheInfo(aCx, JS::Int32Value(info.l2cacheKB));
  JS_SetProperty(aCx, jsInfo, "l2cacheKB", valL2CacheInfo);

  JS::Rooted<JS::Value> valL3CacheInfo(aCx, JS::Int32Value(info.l3cacheKB));
  JS_SetProperty(aCx, jsInfo, "l3cacheKB", valL3CacheInfo);

  JS::Rooted<JS::Value> valSpeedInfo(aCx, JS::Int32Value(info.cpuSpeed));
  JS_SetProperty(aCx, jsInfo, "speedMHz", valSpeedInfo);

  return jsInfo;
}

RefPtr<nsISerialEventTarget> nsSystemInfo::GetBackgroundTarget() {
  if (!mBackgroundET) {
    MOZ_ALWAYS_SUCCEEDS(NS_CreateBackgroundTaskQueue(
        "SystemInfoThread", getter_AddRefs(mBackgroundET)));
  }
  return mBackgroundET;
}

NS_IMETHODIMP
nsSystemInfo::GetOsInfo(JSContext* aCx, Promise** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSystemInfo::GetDiskInfo(JSContext* aCx, Promise** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED(nsSystemInfo, nsHashPropertyBag, nsISystemInfo)

NS_IMETHODIMP
nsSystemInfo::GetCountryCode(JSContext* aCx, Promise** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> promise = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  if (!mCountryCodePromise) {
    RefPtr<nsISerialEventTarget> backgroundET = GetBackgroundTarget();

    mCountryCodePromise = InvokeAsync(backgroundET, __func__, []() {
      nsAutoString countryCode;
      nsresult rv = GetSelectedCityInfo(countryCode);

      if (NS_SUCCEEDED(rv)) {
        return CountryCodePromise::CreateAndResolve(countryCode, __func__);
      }
      return CountryCodePromise::CreateAndReject(rv, __func__);
    });
  }

  auto requestHolder =
      MakeRefPtr<dom::DOMMozPromiseRequestHolder<CountryCodePromise>>(global);

  RefPtr<Promise> capturedPromise = promise;
  mCountryCodePromise
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [requestHolder, capturedPromise](const nsString& countryCode) {
            requestHolder->Complete();
            AutoJSAPI jsapi;
            if (NS_WARN_IF(!jsapi.Init(capturedPromise->GetGlobalObject()))) {
              capturedPromise->MaybeReject(NS_ERROR_UNEXPECTED);
              return;
            }
            JSContext* cx = jsapi.cx();
            JS::Rooted<JSString*> jsCountryCode(
                cx, JS_NewUCStringCopyZ(cx, countryCode.get()));

            JS::Rooted<JS::Value> val(cx, JS::StringValue(jsCountryCode));
            capturedPromise->MaybeResolve(val);
          },
          [requestHolder, capturedPromise](const nsresult rv) {
            requestHolder->Complete();
            // Resolve with null when countryCode is not available from the
            // system
            capturedPromise->MaybeResolve(JS::NullHandleValue);
          })
      ->Track(*requestHolder);

  promise.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSystemInfo::GetProcessInfo(JSContext* aCx, Promise** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> promise = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  if (!mProcessInfoPromise) {
    RefPtr<nsISerialEventTarget> backgroundET = GetBackgroundTarget();

    mProcessInfoPromise = InvokeAsync(backgroundET, __func__, []() {
      ProcessInfo info;
      nsresult rv = CollectProcessInfo(info);
      if (NS_SUCCEEDED(rv)) {
        return ProcessInfoPromise::CreateAndResolve(info, __func__);
      }
      return ProcessInfoPromise::CreateAndReject(rv, __func__);
    });
  };

  auto requestHolder =
      MakeRefPtr<dom::DOMMozPromiseRequestHolder<ProcessInfoPromise>>(global);

  // Chain the new promise to the extant mozpromise
  RefPtr<Promise> capturedPromise = promise;
  mProcessInfoPromise
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [requestHolder, capturedPromise](const ProcessInfo& info) {
            requestHolder->Complete();
            AutoJSAPI jsapi;
            if (NS_WARN_IF(!jsapi.Init(capturedPromise->GetGlobalObject()))) {
              capturedPromise->MaybeReject(NS_ERROR_UNEXPECTED);
              return;
            }
            JSContext* cx = jsapi.cx();
            JS::Rooted<JS::Value> val(
                cx, JS::ObjectValue(*GetJSObjForProcessInfo(cx, info)));
            capturedPromise->MaybeResolve(val);
          },
          [requestHolder, capturedPromise](const nsresult rv) {
            requestHolder->Complete();
            // Resolve with null when installYear is not available from the
            // system
            capturedPromise->MaybeResolve(JS::NullHandleValue);
          })
      ->Track(*requestHolder);

  promise.forget(aResult);

  return NS_OK;
}

NS_IMETHODIMP
nsSystemInfo::IsWindows10BuildOrLater(uint32_t aBuildNumber, bool* aResult) {
  return NS_ERROR_NOT_AVAILABLE;
}
