/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <errno.h>

#include "jsctypes-test-errno.h"

#define FAIL                                                    \
  {                                                             \
    fprintf(stderr, "Assertion failed at line %i\n", __LINE__); \
    (*(int*)nullptr)++;                                         \
  }

void set_errno(int status) { errno = status; }
int get_errno() { return errno; }

