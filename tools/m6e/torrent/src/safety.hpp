#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libtorrent/bdecode.hpp>

namespace m6e {

struct SafetyError {
  std::string code;
  std::string message;
};

constexpr int kMaxTorrentBytes = 16 * 1024 * 1024;
constexpr int kMaxBencodeDepth = 64;
constexpr int kMaxBencodeItems = 3000000;
constexpr int kMaxDirectoryDepth = 64;

bool componentIsSafe(std::string_view component);

std::optional<SafetyError> checkInfoDict(libtorrent::bdecode_node const& info);

std::optional<SafetyError> checkNoSymlinkEscape(
    std::string const& rootReal, std::vector<std::string> const& components);

bool isAbsolutePath(std::string const& path);

}  // namespace m6e
