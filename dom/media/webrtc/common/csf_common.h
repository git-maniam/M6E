/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CSF_COMMON_E58E5677_950A_424c_B6C2_CA180092E6A2_H
#define CSF_COMMON_E58E5677_950A_424c_B6C2_CA180092E6A2_H

/*

This header file defines:

csf_countof
csf_sprintf
csf_vsprintf

*/

/*
  General security tip: Ensure that "format" is never a user-defined string.
  Format should ALWAYS be something that's built into your code, not user
  supplied. For example: never write:

  csf_sprintf(buffer, csf_countof(buffer), pUserSuppliedString);

  Instead write:

  csf_sprintf(buffer, csf_countof(buffer), "%s", pUserSuppliedString);

*/

#  define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
// csf_countof

#define csf_countof(anArray) _countof(anArray)

// csf_sprintf

#  define csf_sprintf(/* char */ buffer,                                    \
                      /* size_t */ sizeOfBufferInCharsInclNullTerm,         \
                      /* const char * */ format, ...)                       \
    snprintf(buffer, sizeOfBufferInCharsInclNullTerm, format, __VA_ARGS__); \
    buffer[sizeOfBufferInCharsInclNullTerm - 1] = '\0'

// csf_vsprintf

#  define csf_vsprintf(/* char */ buffer,                               \
                       /* size_t */ sizeOfBufferInCharsInclNullTerm,    \
                       /* const char * */ format, /* va_list */ vaList) \
    vsprintf(buffer, format, vaList);                                   \
    buffer[sizeOfBufferInCharsInclNullTerm - 1] = '\0'

#endif
