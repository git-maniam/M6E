/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ETWTools_h
#define ETWTools_h

#include "mozilla/BaseProfilerMarkers.h"
#include "mozilla/BaseProfilerMarkersPrerequisites.h"
#include "mozilla/Flow.h"
#include "mozilla/TimeStamp.h"
#include "nsString.h"

namespace ETW {

// Allows checking for the presence of T::Name.
template <typename T, typename = void>
struct MarkerSupportsETW : std::false_type {};
template <typename T>
struct MarkerSupportsETW<T, std::void_t<decltype(T::Name)>> : std::true_type {};

}  // namespace ETW

namespace ETW {
static inline void Init() {}
static inline void Shutdown() {}
static inline bool IsProfilingGroup(mozilla::MarkerSchema::ETWMarkerGroup) {
  return false;
}
template <typename MarkerType, typename... PayloadArguments>
static inline void EmitETWMarker(const mozilla::ProfilerString8View& aName,
                                 const mozilla::MarkerCategory& aCategory,
                                 const mozilla::MarkerOptions& aOptions,
                                 MarkerType aMarkerType,
                                 const PayloadArguments&... aPayloadArguments) {
  // Do some static checks in this function. We don't actually emit any ETW
  // markers because this code is only compiled on non-Windows. The idea is that
  // we want to catch mistakes on all platforms.
  if constexpr (MarkerSupportsETW<MarkerType>::value) {
    // Only the Windows code above reads ETWStoreName, keep it referenced here.
    (void)MarkerType::ETWStoreName;
  }
  if constexpr (mozilla::MarkerHasPayloadFields<MarkerType>::value) {
    if constexpr (mozilla::MarkerHasTranslator<MarkerType>::value) {
      // Call TranslateMarkerInputToSchema, which we expect to be a no-op on
      // non-Windows.
      MarkerType::TranslateMarkerInputToSchema(nullptr, aPayloadArguments...);
    } else {
      const size_t argCount = sizeof...(PayloadArguments);
      static_assert(
          argCount == std::extent_v<decltype(MarkerType::PayloadFields)>,
          "Number and type of fields must be equal to number and type of "
          "payload arguments. If this is not the case a "
          "TranslateMarkerInputToSchema function must be defined.");
    }
  }
}
template <typename MarkerType, typename... PayloadArguments>
void OutputMarkerSchema(void* aContext, MarkerType aMarkerType,
                        const PayloadArguments&... aPayloadArguments) {}
}  // namespace ETW

#endif  // ETWTools_h
