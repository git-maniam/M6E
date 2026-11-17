/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipfilter.hpp"

#include <arpa/inet.h>
#include <cstdint>
#include <optional>
#include <string>

#include <libtorrent/address.hpp>

namespace m6e {

namespace {

std::string_view trim(std::string_view s) {
  auto isSpace = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
  };
  while (!s.empty() && isSpace(s.front())) {
    s.remove_prefix(1);
  }
  while (!s.empty() && isSpace(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

std::optional<lt::address> parseV4(std::string_view s) {
  unsigned long octets[4];
  int count = 0;
  std::size_t i = 0;
  while (count < 4) {
    std::size_t digits = 0;
    unsigned long value = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 3) {
      value = value * 10 + static_cast<unsigned long>(s[i] - '0');
      i++;
      digits++;
    }
    if (digits == 0 || value > 255) {
      return std::nullopt;
    }
    octets[count++] = value;
    if (count < 4) {
      if (i >= s.size() || s[i] != '.') {
        return std::nullopt;
      }
      i++;
    }
  }
  if (i != s.size()) {
    return std::nullopt;
  }
  return lt::address_v4(static_cast<std::uint32_t>(
      (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]));
}

std::optional<lt::address> parseAddress(std::string_view s) {
  s = trim(s);
  if (s.empty() || s.size() > 45) {
    return std::nullopt;
  }
  if (s.find(':') == std::string_view::npos) {
    return parseV4(s);
  }
  std::string text(s);
  in6_addr a6;
  if (inet_pton(AF_INET6, text.c_str(), &a6) != 1) {
    return std::nullopt;
  }
  lt::address_v6::bytes_type bytes;
  for (std::size_t k = 0; k < bytes.size(); k++) {
    bytes[k] = a6.s6_addr[k];
  }
  return lt::address_v6(bytes);
}

bool addRange(IpFilterList& out, std::string_view range) {
  auto dash = range.find('-');
  if (dash == std::string_view::npos) {
    return false;
  }
  auto first = parseAddress(range.substr(0, dash));
  auto last = parseAddress(range.substr(dash + 1));
  if (!first || !last || first->is_v4() != last->is_v4() || *last < *first) {
    return false;
  }
  out.filter.add_rule(*first, *last, lt::ip_filter::blocked);
  out.ranges++;
  return true;
}

std::optional<long> parseLevel(std::string_view s) {
  s = trim(s);
  if (s.empty() || s.size() > 6) {
    return std::nullopt;
  }
  long value = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
  }
  return value;
}

void parseLine(IpFilterList& out, std::string_view line) {
  line = trim(line);
  if (line.empty() || line.front() == '#' || line.substr(0, 2) == "//") {
    return;
  }
  auto comma = line.find(',');
  if (comma != std::string_view::npos &&
      line.substr(0, comma).find('-') != std::string_view::npos) {
    auto rest = line.substr(comma + 1);
    auto level = parseLevel(rest.substr(0, rest.find(',')));
    if (!level) {
      out.skipped++;
      return;
    }
    if (*level > 127) {
      return;
    }
    if (!addRange(out, line.substr(0, comma))) {
      out.skipped++;
    }
    return;
  }
  auto colon = line.rfind(':');
  auto dash = line.rfind('-');
  if (colon != std::string_view::npos && dash != std::string_view::npos &&
      colon < dash && addRange(out, line.substr(colon + 1))) {
    return;
  }
  if (addRange(out, line)) {
    return;
  }
  out.skipped++;
}

}  // namespace

IpFilterList parseIpFilter(std::string_view text) {
  IpFilterList out;
  std::size_t start = 0;
  while (start < text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    parseLine(out, text.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

}  // namespace m6e
