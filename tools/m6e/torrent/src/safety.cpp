#include "safety.hpp"

#include "confine.hpp"

#include <cerrno>
#include <sys/stat.h>

namespace lt = libtorrent;

namespace m6e {

namespace {

std::optional<SafetyError> unsafe(std::string message) {
  return SafetyError{"unsafe-path", std::move(message)};
}

std::optional<SafetyError> invalid(std::string message) {
  return SafetyError{"invalid-torrent", std::move(message)};
}

bool hasSymlinkEntry(lt::bdecode_node const& entry) {
  if (entry.type() != lt::bdecode_node::dict_t) {
    return false;
  }
  if (entry.dict_find("symlink path")) {
    return true;
  }
  auto attr = entry.dict_find_string_value("attr");
  return attr.find('l') != std::string_view::npos;
}

std::optional<SafetyError> checkName(lt::bdecode_node const& info,
                                     char const* key) {
  auto node = info.dict_find(key);
  if (!node) {
    return std::nullopt;
  }
  if (node.type() != lt::bdecode_node::string_t) {
    return invalid(std::string(key) + " is not a string");
  }
  if (!componentIsSafe(node.string_value())) {
    return unsafe("the torrent name is not a safe folder or file name");
  }
  return std::nullopt;
}

std::optional<SafetyError> checkPathList(lt::bdecode_node const& list) {
  if (list.type() != lt::bdecode_node::list_t || list.list_size() == 0) {
    return invalid("a file path is not a list of names");
  }
  if (list.list_size() > kMaxDirectoryDepth) {
    return invalid("a file path is nested too deeply");
  }
  for (int i = 0; i < list.list_size(); i++) {
    auto part = list.list_at(i);
    if (part.type() != lt::bdecode_node::string_t) {
      return invalid("a file path component is not a string");
    }
    if (!componentIsSafe(part.string_value())) {
      return unsafe("a file path is absolute or has an empty, dot, dot-dot, "
                    "NUL, colon, backslash or slash component");
    }
  }
  return std::nullopt;
}

std::optional<SafetyError> walkTree(lt::bdecode_node const& node, int depth) {
  if (node.type() != lt::bdecode_node::dict_t) {
    return invalid("the file tree is malformed");
  }
  if (depth > kMaxDirectoryDepth) {
    return invalid("the file tree is nested too deeply");
  }
  for (int i = 0; i < node.dict_size(); i++) {
    auto [key, child] = node.dict_at(i);
    if (key.empty()) {
      if (hasSymlinkEntry(child)) {
        return unsafe("the torrent contains a symbolic link");
      }
      continue;
    }
    if (!componentIsSafe(key)) {
      return unsafe("a file path is absolute or has an empty, dot, dot-dot, "
                    "NUL, colon, backslash or slash component");
    }
    if (auto err = walkTree(child, depth + 1)) {
      return err;
    }
  }
  return std::nullopt;
}

}  // namespace

bool componentIsSafe(std::string_view c) {
  if (c.empty() || c == "." || c == "..") {
    return false;
  }
  for (char ch : c) {
    if (ch == '\0' || ch == ':' || ch == '\\' || ch == '/') {
      return false;
    }
  }
  return true;
}

bool hasAttribute(lt::bdecode_node const& entry, char flag) {
  if (entry.type() != lt::bdecode_node::dict_t) {
    return false;
  }
  return entry.dict_find_string_value("attr").find(flag) !=
         std::string_view::npos;
}

bool isAbsolutePath(std::string const& path) {
  return !path.empty() && path[0] == '/' &&
         path.find('\0') == std::string::npos;
}

std::optional<SafetyError> checkInfoDict(lt::bdecode_node const& info) {
  if (info.type() != lt::bdecode_node::dict_t) {
    return invalid("the info dictionary is missing");
  }
  if (!info.dict_find("name")) {
    return invalid("the torrent has no name");
  }
  if (auto err = checkName(info, "name")) {
    return err;
  }
  if (auto err = checkName(info, "name.utf-8")) {
    return err;
  }
  auto files = info.dict_find("files");
  auto tree = info.dict_find("file tree");
  if (files) {
    if (files.type() != lt::bdecode_node::list_t) {
      return invalid("the file list is malformed");
    }
    for (int i = 0; i < files.list_size(); i++) {
      auto entry = files.list_at(i);
      if (entry.type() != lt::bdecode_node::dict_t) {
        return invalid("a file entry is malformed");
      }
      if (hasSymlinkEntry(entry)) {
        return unsafe("the torrent contains a symbolic link");
      }
      auto path = entry.dict_find("path");
      if (!path) {
        return invalid("a file entry has no path");
      }
      if (auto err = checkPathList(path)) {
        return err;
      }
      if (auto utf8 = entry.dict_find("path.utf-8")) {
        if (auto err = checkPathList(utf8)) {
          return err;
        }
      }
    }
  }
  if (tree) {
    if (auto err = walkTree(tree, 0)) {
      return err;
    }
  }
  if (!files && !tree && !info.dict_find("length")) {
    return invalid("the torrent has no files");
  }
  if (!files && !tree && hasSymlinkEntry(info)) {
    return unsafe("the torrent contains a symbolic link");
  }
  std::vector<Layout> layouts;
  if (auto err = layoutsFromInfo(info, &layouts)) {
    return err;
  }
  for (auto const& layout : layouts) {
    if (auto err = checkLayoutPaths(layout)) {
      return err;
    }
  }
  return std::nullopt;
}

std::optional<SafetyError> checkNoSymlinkEscape(
    std::string const& rootReal, std::vector<std::string> const& components) {
  std::string current = rootReal;
  for (auto const& part : components) {
    current += "/" + part;
    struct stat st;
    if (lstat(current.c_str(), &st) != 0) {
      if (errno == ENOENT) {
        return std::nullopt;
      }
      return unsafe("cannot inspect " + current);
    }
    if (S_ISLNK(st.st_mode)) {
      return unsafe("a symbolic link in the save folder would redirect a "
                    "download: " + current);
    }
  }
  return std::nullopt;
}

}  // namespace m6e
