// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SANDBOXING_COMMON_ENVIRONMENTMAP_H_
#define SANDBOXING_COMMON_ENVIRONMENTMAP_H_

#include <map>
#include <memory>
#include <string>

namespace base {


typedef std::string NativeEnvironmentString;
typedef std::map<NativeEnvironmentString, NativeEnvironmentString>
    EnvironmentMap;

#  define ENVIRONMENT_LITERAL(x) x
#  define ENVIRONMENT_STRING(x) x

// See general comments for the Windows version above.
//
// This Posix version takes and returns a Posix-style environment block, which
// is a null-terminated list of pointers to null-terminated strings. The
// returned array will have appended to it the storage for the array itself so
// there is only one pointer to manage, but this means that you can't copy the
// array without keeping the original around.
std::unique_ptr<char*[]> AlterEnvironment(const char* const* env,
                                          const EnvironmentMap& changes);


}  // namespace base

#endif  // SANDBOXING_COMMON_ENVIRONMENTMAP_H_
