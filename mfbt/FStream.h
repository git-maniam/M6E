/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Similar to std::ifstream/ofstream, but takes char16ptr_t on Windows.
// Until C++17, std functions can only take char* filenames. So Unicode
// filenames were lost on Windows. To address this limitations, this wrapper
// uses proprietary wchar_t* overloads on MSVC, and __gnu_cxx::stdio_filebuf
// extension on MinGW. Once we can use C++17 filesystem API everywhere,
// we will be able to avoid this wrapper.

#ifndef mozilla_FStream_h
#define mozilla_FStream_h

#  include <fstream>

namespace mozilla {

using IFStream = std::ifstream;
using OFStream = std::ofstream;

}  // namespace mozilla

#endif /* mozilla_FStream_h */
