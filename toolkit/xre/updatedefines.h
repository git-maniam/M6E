/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef UPDATEDEFINES_H
#define UPDATEDEFINES_H

#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>

#define NS_T(str) str
#define NS_tsnprintf snprintf

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  else
#    define MAXPATHLEN 1024
#  endif
#endif

#endif
