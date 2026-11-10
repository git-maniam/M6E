/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <cstddef>
#include <string_view>

#include <libtorrent/ip_filter.hpp>

namespace m6e {

constexpr std::size_t kMaxIpFilterBytes = 64 * 1024 * 1024;

struct IpFilterList {
  lt::ip_filter filter;
  int ranges = 0;
  int skipped = 0;
};

// Parses a PeerGuardian text list ("name:first-last") or an eMule
// ipfilter.dat ("first - last , level , name"; a level above 127 is not a
// block), line by line, in any mix. Blank and comment lines are ignored;
// lines that cannot be read are counted in `skipped`.
IpFilterList parseIpFilter(std::string_view text);

}  // namespace m6e
