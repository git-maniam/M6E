/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "protocol.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <poll.h>
#include <unistd.h>

namespace m6e {

void fail(std::string code, std::string message, std::string tid) {
  throw RequestError{std::move(code), std::move(message), std::move(tid)};
}

std::optional<std::int64_t> asInt(json::value const& value) {
  if (auto const* p = value.if_int64()) {
    return *p;
  }
  if (auto const* p = value.if_uint64()) {
    if (*p <= static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())) {
      return static_cast<std::int64_t>(*p);
    }
    return std::nullopt;
  }
  if (auto const* p = value.if_double()) {
    double d = *p;
    if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 9e15) {
      return static_cast<std::int64_t>(d);
    }
  }
  return std::nullopt;
}

json::value const* findArg(json::object const& args, std::string_view key) {
  auto const* v = args.if_contains(key);
  if (v && v->is_null()) {
    return nullptr;
  }
  return v;
}

std::string stringArg(json::object const& args, std::string_view key) {
  auto const* v = findArg(args, key);
  if (!v || !v->is_string()) {
    fail("bad-request", std::string(key) + " must be a string");
  }
  return std::string(v->as_string());
}

bool boolArg(json::object const& args, std::string_view key,
             std::optional<bool> fallback) {
  auto const* v = findArg(args, key);
  if (!v && fallback) {
    return *fallback;
  }
  if (!v || !v->is_bool()) {
    fail("bad-request", std::string(key) + " must be true or false");
  }
  return v->as_bool();
}

std::int64_t intArg(json::object const& args, std::string_view key,
                    std::int64_t min, std::int64_t max,
                    std::optional<std::int64_t> fallback) {
  auto const* v = findArg(args, key);
  if (!v && fallback) {
    return *fallback;
  }
  auto n = v ? asInt(*v) : std::nullopt;
  if (!n || *n < min || *n > max) {
    fail("bad-request", std::string(key) + " must be an integer from " +
                            std::to_string(min) + " to " +
                            std::to_string(max));
  }
  return *n;
}

std::string tidArg(json::object const& args) {
  std::string tid = stringArg(args, "tid");
  if (tid.size() != 40) {
    fail("bad-request", "tid must be 40 hexadecimal characters");
  }
  for (char& c : tid) {
    if (c >= 'A' && c <= 'F') {
      c = static_cast<char>(c - 'A' + 'a');
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      fail("bad-request", "tid must be 40 hexadecimal characters");
    }
  }
  return tid;
}

json::object event(std::string_view name) {
  json::object o;
  o["event"] = name;
  return o;
}

std::string sanitizeUtf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  auto const n = s.size();
  auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
  while (i < n) {
    unsigned char c = byte(i);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
      i++;
      continue;
    }
    std::size_t len = 0;
    unsigned char lo = 0x80, hi = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) {
      len = 2;
    } else if (c >= 0xe0 && c <= 0xef) {
      len = 3;
      if (c == 0xe0) {
        lo = 0xa0;
      } else if (c == 0xed) {
        hi = 0x9f;
      }
    } else if (c >= 0xf0 && c <= 0xf4) {
      len = 4;
      if (c == 0xf0) {
        lo = 0x90;
      } else if (c == 0xf4) {
        hi = 0x8f;
      }
    }
    bool ok = len > 0 && i + len <= n;
    for (std::size_t k = 1; ok && k < len; k++) {
      unsigned char b = byte(i + k);
      unsigned char min = k == 1 ? lo : 0x80;
      unsigned char max = k == 1 ? hi : 0xbf;
      ok = b >= min && b <= max;
    }
    if (ok) {
      out.append(s.data() + i, len);
      i += len;
    } else {
      out.append("\xef\xbf\xbd");
      i++;
    }
  }
  return out;
}

std::string base64(std::vector<std::uint8_t> const& bytes) {
  static char const table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    std::uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out.push_back(table[(v >> 18) & 63]);
    out.push_back(table[(v >> 12) & 63]);
    out.push_back(table[(v >> 6) & 63]);
    out.push_back(table[v & 63]);
  }
  if (i + 1 == bytes.size()) {
    std::uint32_t v = bytes[i] << 16;
    out.push_back(table[(v >> 18) & 63]);
    out.push_back(table[(v >> 12) & 63]);
    out.append("==");
  } else if (i + 2 == bytes.size()) {
    std::uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out.push_back(table[(v >> 18) & 63]);
    out.push_back(table[(v >> 12) & 63]);
    out.push_back(table[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

bool Output::write(json::object const& message) {
  if (closed_) {
    return false;
  }
  std::string line = sanitizeUtf8(json::serialize(message));
  if (line.size() >= kMaxLineBytes) {
    std::fprintf(stderr, "m6e-torrent: dropped an output line of %zu bytes\n",
                 line.size());
    return false;
  }
  line.push_back('\n');
  std::size_t offset = 0;
  while (offset < line.size()) {
    ssize_t n = ::write(STDOUT_FILENO, line.data() + offset,
                        line.size() - offset);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        pollfd p{STDOUT_FILENO, POLLOUT, 0};
        poll(&p, 1, 1000);
        continue;
      }
      closed_ = true;
      return false;
    }
    offset += static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace m6e
