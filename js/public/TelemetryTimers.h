/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_TelemetryTimers_h
#define js_TelemetryTimers_h

#include "mozilla/TimeStamp.h"

#include "jstypes.h"

struct JS_PUBLIC_API JSContext;

namespace JS {

/** Timing information for telemetry purposes **/
struct JSTimers {
  mozilla::TimeDuration executionTime;       // Total time spent executing
  mozilla::TimeDuration delazificationTime;  // Total time spent delazifying
  // M6E: page telemetry (D29). Main-thread parsing, compilation and stencil
  // instantiation, outermost only.
  mozilla::TimeDuration compileTime;
  // M6E: the part of compileTime and delazificationTime spent while timed
  // execution was running, which executionTime also includes.
  mozilla::TimeDuration compileTimeWhileExecuting;
};

extern JS_PUBLIC_API void SetMeasuringExecutionTimeEnabled(JSContext* cx,
                                                           bool value);
extern JS_PUBLIC_API JSTimers GetJSTimers(JSContext* cx);

// M6E: adds compilation done off the main thread for the current realm, such
// as a script loader's off-thread parse, to its compileTime (D29).
extern JS_PUBLIC_API void AddCompileTime(JSContext* cx,
                                         mozilla::TimeDuration time);

}  // namespace JS

#endif  // js_TelemetryTimers_h
