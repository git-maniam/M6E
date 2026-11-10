/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace m6e {

constexpr std::size_t kMaxLineBytes = 1024 * 1024;

// Thrown by an operation; becomes the error reply. `tid` is set only for
// "duplicate".
struct RequestError {
  std::string code;
  std::string message;
  std::string tid;
};

[[noreturn]] void fail(std::string code, std::string message,
                       std::string tid = {});

std::optional<std::int64_t> asInt(json::value const& value);

json::value const* findArg(json::object const& args, std::string_view key);
std::string stringArg(json::object const& args, std::string_view key);
bool boolArg(json::object const& args, std::string_view key,
             std::optional<bool> fallback = std::nullopt);
std::int64_t intArg(json::object const& args, std::string_view key,
                    std::int64_t min, std::int64_t max,
                    std::optional<std::int64_t> fallback = std::nullopt);
std::string tidArg(json::object const& args);

json::object event(std::string_view name);

std::string sanitizeUtf8(std::string_view text);
std::string base64(std::vector<std::uint8_t> const& bytes);

// Splits standard input into lines. A line longer than kMaxLineBytes is
// dropped, up to its newline, and reported once through onOverflow.
class LineReader {
 public:
  // onLine returns false to stop reading; feed then returns false too.
  template <typename OnLine, typename OnOverflow>
  bool feed(char const* data, std::size_t size, OnLine&& onLine,
            OnOverflow&& onOverflow) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < size; i++) {
      if (data[i] != '\n') {
        continue;
      }
      bool keepGoing = true;
      if (discarding_) {
        discarding_ = false;
      } else {
        buffer_.append(data + start, i - start);
        if (buffer_.size() > kMaxLineBytes) {
          onOverflow();
        } else {
          std::string_view line(buffer_);
          if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
          }
          keepGoing = onLine(line);
        }
      }
      buffer_.clear();
      start = i + 1;
      if (!keepGoing) {
        return false;
      }
    }
    if (!discarding_) {
      buffer_.append(data + start, size - start);
      if (buffer_.size() > kMaxLineBytes) {
        discarding_ = true;
        buffer_.clear();
        onOverflow();
      }
    }
    return true;
  }

 private:
  std::string buffer_;
  bool discarding_ = false;
};

// Writes one JSON object per line to standard output.
class Output {
 public:
  // False when the line would exceed kMaxLineBytes (nothing is written) or
  // standard output is gone.
  bool write(json::object const& message);
  bool closed() const { return closed_; }

 private:
  bool closed_ = false;
};

}  // namespace m6e
