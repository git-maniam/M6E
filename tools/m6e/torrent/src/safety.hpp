#pragma once

// Additions for P8.1 (existing declarations are unchanged):
//  - hasAttribute(entry, flag): BEP 47 "attr" test.
//  - checkInfoDict now also rejects over-long, non-UTF-8 and control
//    character names, padding files without a .pad name, and paths that
//    collide by case or Unicode normalisation (checkLayoutPaths).
//  - confine.hpp holds the canonical-path, no-follow, quarantine and sandbox
//    functions; call checkLayout before confirm starts a transfer.

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

bool hasAttribute(libtorrent::bdecode_node const& entry, char flag);

}  // namespace m6e
