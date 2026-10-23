/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HeadlessLookAndFeel_h
#define mozilla_widget_HeadlessLookAndFeel_h

#include "nsLookAndFeel.h"
#include "nsXPLookAndFeel.h"

namespace mozilla {
namespace widget {


// When possible, we simply reuse the platform's existing nsLookAndFeel
// implementation in headless mode.

typedef nsLookAndFeel HeadlessLookAndFeel;


}  // namespace widget
}  // namespace mozilla

#endif
