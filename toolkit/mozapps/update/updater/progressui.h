/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PROGRESSUI_H_
#define PROGRESSUI_H_

#include "updatedefines.h"

typedef char NS_tchar;
#  define NS_main main

// Called to perform any initialization of the widget toolkit
int InitProgressUI(int* argc, NS_tchar*** argv);

// Called on the main thread at startup
int ShowProgressUI(bool indeterminate = false);
// May be called from any thread
void QuitProgressUI();

// May be called from any thread: progress is a number between 0 and 100
void UpdateProgressUI(float progress);

#endif  // PROGRESSUI_H_
