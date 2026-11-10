#include "confine.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <dirent.h>
#include <fcntl.h>
#include <sandbox.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

extern "C" int sandbox_init_with_parameters(char const* profile, uint64_t flags,
                                            char const* const parameters[],
                                            char** errorbuf);

namespace lt = libtorrent;

namespace m6e {

namespace {

constexpr char kQuarantineAttr[] = "com.apple.quarantine";

std::optional<SafetyError> unsafe(std::string message) {
  return SafetyError{"unsafe-path", std::move(message)};
}

std::optional<SafetyError> invalid(std::string message) {
  return SafetyError{"invalid-torrent", std::move(message)};
}

std::optional<SafetyError> ioError(std::string what, int err) {
  return SafetyError{err == ENOSPC || err == EDQUOT ? "disk-full" : "io-error",
                     what + ": " + std::strerror(err)};
}

void setError(SafetyError* err, std::string code, std::string message) {
  if (err) {
    *err = SafetyError{std::move(code), std::move(message)};
  }
}

void setErrno(SafetyError* err, std::string what, int e) {
  if (e == ELOOP || e == ENOTDIR || e == EISDIR) {
    setError(err, "unsafe-path",
             what + ": a symbolic link or non-directory is in the way");
  } else if (e == ENOSPC || e == EDQUOT) {
    setError(err, "disk-full", what + ": " + std::strerror(e));
  } else {
    setError(err, "io-error", what + ": " + std::strerror(e));
  }
}

bool isDigits(std::string const& s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

std::string joinPath(std::string const& root, PathComponents const& parts,
                     size_t count) {
  std::string out = root;
  for (size_t i = 0; i < count; i++) {
    out += "/" + parts[i];
  }
  return out;
}

bool isPadPath(PathComponents const& p) {
  return p.size() >= 2 && p[p.size() - 2] == ".pad" && isDigits(p.back());
}

struct DirListing {
  bool ok = false;
  int err = 0;
  std::unordered_map<std::string, std::string> byFold;
  std::set<std::string> exact;
};

DirListing listDirectory(std::string const& path) {
  DirListing out;
  DIR* d = opendir(path.c_str());
  if (!d) {
    out.err = errno;
    return out;
  }
  while (dirent* e = readdir(d)) {
    std::string name(e->d_name, e->d_namlen);
    if (name == "." || name == "..") {
      continue;
    }
    out.exact.insert(name);
    std::string folded = foldName(name);
    if (!folded.empty()) {
      out.byFold.emplace(std::move(folded), name);
    }
  }
  closedir(d);
  out.ok = true;
  return out;
}

int openDirChain(int rootFd, PathComponents const& parts, size_t count,
                 bool create, SafetyError* err) {
  int cur = dup(rootFd);
  if (cur < 0) {
    setErrno(err, "dup", errno);
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    if (!componentIsSafe(parts[i])) {
      close(cur);
      setError(err, "unsafe-path", "unsafe path component");
      return -1;
    }
    int next = openat(cur, parts[i].c_str(),
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create) {
      if (mkdirat(cur, parts[i].c_str(), 0755) != 0 && errno != EEXIST) {
        int e = errno;
        close(cur);
        setErrno(err, "cannot create " + parts[i], e);
        return -1;
      }
      next = openat(cur, parts[i].c_str(),
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    if (next < 0) {
      int e = errno;
      close(cur);
      setErrno(err, "cannot open " + parts[i], e);
      return -1;
    }
    close(cur);
    cur = next;
  }
  return cur;
}

int openRoot(std::string const& rootReal, SafetyError* err) {
  if (!isAbsolutePath(rootReal)) {
    setError(err, "unsafe-path", "the save folder is not an absolute path");
    return -1;
  }
  int fd = open(rootReal.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    setErrno(err, "cannot open the save folder", errno);
  }
  return fd;
}

std::string newUuid() {
  CFUUIDRef uuid = CFUUIDCreate(nullptr);
  CFStringRef str = CFUUIDCreateString(nullptr, uuid);
  char buf[64] = {0};
  CFStringGetCString(str, buf, sizeof buf, kCFStringEncodingUTF8);
  CFRelease(str);
  CFRelease(uuid);
  return buf;
}

bool quarantineViaCoreFoundation(std::string const& path) {
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      nullptr, reinterpret_cast<UInt8 const*>(path.data()), path.size(),
      false);
  if (!url) {
    return false;
  }
  CFMutableDictionaryRef props = CFDictionaryCreateMutable(
      nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(props, CFSTR("LSQuarantineType"),
                       CFSTR("LSQuarantineTypeOtherDownload"));
  Boolean ok = CFURLSetResourcePropertyForKey(
      url, kCFURLQuarantinePropertiesKey, props, nullptr);
  CFRelease(props);
  CFRelease(url);
  return ok;
}

std::optional<SafetyError> checkOwnPaths(Layout const& layout) {
  struct Seen {
    std::string raw;
    bool isFile;
  };
  std::unordered_map<std::string, Seen> seen;
  for (auto const& file : layout.files) {
    if (file.pad) {
      continue;
    }
    std::string foldKey;
    std::string rawKey;
    for (size_t i = 0; i < file.path.size(); i++) {
      bool last = i + 1 == file.path.size();
      foldKey += "/" + foldName(file.path[i]);
      rawKey += "/" + file.path[i];
      auto it = seen.find(foldKey);
      if (it == seen.end()) {
        seen.emplace(foldKey, Seen{rawKey, last});
        continue;
      }
      if (it->second.raw != rawKey) {
        return unsafe("two paths in the torrent differ only by case or "
                      "Unicode normalisation: " + it->second.raw + " and " +
                      rawKey);
      }
      if (last || it->second.isFile) {
        return unsafe("the torrent lists a path twice or uses a name as both "
                      "a file and a folder: " + rawKey);
      }
    }
  }
  return std::nullopt;
}

std::optional<SafetyError> walkExisting(std::string const& rootReal,
                                        Layout const& layout) {
  std::unordered_map<std::string, DirListing> cache;
  for (auto const& file : layout.files) {
    if (file.pad) {
      continue;
    }
    std::string cur = rootReal;
    for (size_t i = 0; i < file.path.size(); i++) {
      bool last = i + 1 == file.path.size();
      auto it = cache.find(cur);
      if (it == cache.end()) {
        it = cache.emplace(cur, listDirectory(cur)).first;
        if (!it->second.ok) {
          return unsafe("cannot inspect " + cur + ": " +
                        std::strerror(it->second.err));
        }
      }
      auto const& listing = it->second;
      auto const& name = file.path[i];
      if (!listing.exact.count(name)) {
        auto alias = listing.byFold.find(foldName(name));
        if (alias != listing.byFold.end()) {
          return unsafe("the name " + name + " would alias the existing " +
                        alias->second + " in " + cur);
        }
        break;
      }
      cur += "/" + name;
      struct stat st;
      if (lstat(cur.c_str(), &st) != 0) {
        if (errno == ENOENT) {
          break;
        }
        return unsafe("cannot inspect " + cur);
      }
      if (S_ISLNK(st.st_mode)) {
        return unsafe("a symbolic link in the save folder would redirect a "
                      "download: " + cur);
      }
      if (!last && !S_ISDIR(st.st_mode)) {
        return unsafe("a file is in the way of a folder: " + cur);
      }
      if (last && !S_ISREG(st.st_mode)) {
        return unsafe("a folder or special file is in the way of a file: " +
                      cur);
      }
      if (last && st.st_nlink > 1) {
        return unsafe("an existing file has other hard links: " + cur);
      }
    }
  }
  return std::nullopt;
}

void addTreeFiles(lt::bdecode_node const& node, PathComponents* prefix,
                  std::vector<LayoutFile>* out, int depth) {
  if (node.type() != lt::bdecode_node::dict_t || depth > kMaxDirectoryDepth) {
    return;
  }
  for (int i = 0; i < node.dict_size(); i++) {
    auto [key, child] = node.dict_at(i);
    if (key.empty()) {
      LayoutFile f;
      f.path = *prefix;
      f.pad = hasAttribute(child, 'p');
      out->push_back(std::move(f));
      continue;
    }
    prefix->push_back(std::string(key));
    addTreeFiles(child, prefix, out, depth + 1);
    prefix->pop_back();
  }
}

}  // namespace

bool isValidUtf8(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = s[i];
    size_t n = 0;
    uint32_t cp = 0;
    if (c < 0x80) {
      i++;
      continue;
    } else if (c >= 0xC2 && c <= 0xDF) {
      n = 1;
      cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
      n = 2;
      cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      n = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + n >= s.size()) {
      return false;
    }
    for (size_t k = 1; k <= n; k++) {
      unsigned char cc = s[i + k];
      if ((cc & 0xC0) != 0x80) {
        return false;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
      return false;
    }
    i += n + 1;
  }
  return true;
}

std::string foldName(std::string_view name) {
  if (!isValidUtf8(name)) {
    return {};
  }
  CFStringRef str = CFStringCreateWithBytes(
      nullptr, reinterpret_cast<UInt8 const*>(name.data()), name.size(),
      kCFStringEncodingUTF8, false);
  if (!str) {
    return {};
  }
  CFMutableStringRef m = CFStringCreateMutableCopy(nullptr, 0, str);
  CFRelease(str);
  CFStringNormalize(m, kCFStringNormalizationFormD);
  CFStringFold(m, kCFCompareCaseInsensitive, nullptr);
  CFIndex length = CFStringGetLength(m);
  CFIndex size = CFStringGetMaximumSizeForEncoding(length,
                                                   kCFStringEncodingUTF8) + 1;
  std::string out(size, '\0');
  bool ok = CFStringGetCString(m, out.data(), size, kCFStringEncodingUTF8);
  CFRelease(m);
  if (!ok) {
    return {};
  }
  out.resize(std::strlen(out.c_str()));
  return out;
}

std::optional<SafetyError> checkComponentLimits(PathComponents const& parts) {
  if (parts.empty()) {
    return unsafe("a file path is empty");
  }
  if (parts.size() > static_cast<size_t>(kMaxDirectoryDepth) + 1) {
    return unsafe("a file path is nested too deeply");
  }
  size_t total = 0;
  for (auto const& part : parts) {
    if (!componentIsSafe(part)) {
      return unsafe("a file path is absolute or has an empty, dot, dot-dot, "
                    "NUL, colon, backslash or slash component");
    }
    if (part.size() > kMaxComponentBytes) {
      return unsafe("a file or folder name is longer than 255 bytes");
    }
    if (!isValidUtf8(part)) {
      return unsafe("a file or folder name is not valid UTF-8");
    }
    for (unsigned char c : part) {
      if (c < 0x20 || c == 0x7F) {
        return unsafe("a file or folder name has a control character");
      }
    }
    total += part.size() + 1;
  }
  if (total > kMaxPathBytes) {
    return unsafe("a file path is too long");
  }
  return std::nullopt;
}

std::optional<SafetyError> splitRelativePath(std::string const& path,
                                             PathComponents* out) {
  if (path.empty() || path[0] == '/' ||
      path.find('\0') != std::string::npos) {
    return unsafe("a file path is empty, absolute or has a NUL");
  }
  PathComponents parts;
  size_t start = 0;
  while (true) {
    size_t slash = path.find('/', start);
    parts.push_back(path.substr(start, slash == std::string::npos
                                           ? std::string::npos
                                           : slash - start));
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  if (auto err = checkComponentLimits(parts)) {
    return err;
  }
  if (out) {
    *out = std::move(parts);
  }
  return std::nullopt;
}

std::optional<SafetyError> canonicalRoot(std::string const& root,
                                         std::string* rootReal) {
  if (!isAbsolutePath(root)) {
    return unsafe("the save folder is not an absolute path");
  }
  char buf[PATH_MAX];
  if (!realpath(root.c_str(), buf)) {
    return ioError("cannot resolve the save folder", errno);
  }
  std::string real = buf;
  struct stat st;
  if (stat(real.c_str(), &st) != 0) {
    return ioError("cannot inspect the save folder", errno);
  }
  if (!S_ISDIR(st.st_mode)) {
    return unsafe("the save folder is not a folder");
  }
  if (real == "/") {
    return unsafe("the save folder cannot be the root of the disk");
  }
  if (real.size() > kMaxPathBytes - 100) {
    return unsafe("the save folder path is too long");
  }
  if (rootReal) {
    *rootReal = real;
  }
  return std::nullopt;
}

std::optional<SafetyError> ensureProfileTorrentsDir(
    std::string const& profileDir, std::string* torrentsReal) {
  std::string profileReal;
  if (auto err = canonicalRoot(profileDir, &profileReal)) {
    return err;
  }
  std::string dir = profileReal + "/torrents";
  struct stat st;
  if (lstat(dir.c_str(), &st) != 0) {
    if (errno != ENOENT) {
      return ioError("cannot inspect " + dir, errno);
    }
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
      return ioError("cannot create " + dir, errno);
    }
    if (lstat(dir.c_str(), &st) != 0) {
      return ioError("cannot inspect " + dir, errno);
    }
  }
  if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
    return unsafe(dir + " is a symbolic link or not a folder");
  }
  return canonicalRoot(dir, torrentsReal);
}

std::optional<SafetyError> layoutsFromInfo(lt::bdecode_node const& info,
                                           std::vector<Layout>* out) {
  if (info.type() != lt::bdecode_node::dict_t) {
    return invalid("the info dictionary is missing");
  }
  std::vector<std::string> names;
  for (char const* key : {"name", "name.utf-8"}) {
    auto node = info.dict_find(key);
    if (!node) {
      continue;
    }
    if (node.type() != lt::bdecode_node::string_t) {
      return invalid(std::string(key) + " is not a string");
    }
    names.push_back(std::string(node.string_value()));
  }
  if (names.empty()) {
    return invalid("the torrent has no name");
  }
  auto files = info.dict_find("files");
  auto tree = info.dict_find("file tree");
  bool anyUtf8 = names.size() == 2;
  if (files && files.type() == lt::bdecode_node::list_t) {
    for (int i = 0; i < files.list_size() && !anyUtf8; i++) {
      auto entry = files.list_at(i);
      anyUtf8 = entry.type() == lt::bdecode_node::dict_t &&
                entry.dict_find("path.utf-8");
    }
  }
  for (int variant = 0; variant < (anyUtf8 ? 2 : 1); variant++) {
    bool utf8 = variant == 1;
    std::string name = utf8 ? names.back() : names.front();
    if (files) {
      if (files.type() != lt::bdecode_node::list_t) {
        return invalid("the file list is malformed");
      }
      Layout layout;
      for (int i = 0; i < files.list_size(); i++) {
        auto entry = files.list_at(i);
        if (entry.type() != lt::bdecode_node::dict_t) {
          return invalid("a file entry is malformed");
        }
        auto path = entry.dict_find("path");
        auto utf = entry.dict_find("path.utf-8");
        auto list = utf8 && utf ? utf : path;
        if (!list || list.type() != lt::bdecode_node::list_t) {
          return invalid("a file entry has no path");
        }
        LayoutFile f;
        f.path.push_back(name);
        for (int k = 0; k < list.list_size(); k++) {
          auto part = list.list_at(k);
          if (part.type() != lt::bdecode_node::string_t) {
            return invalid("a file path component is not a string");
          }
          f.path.push_back(std::string(part.string_value()));
        }
        f.pad = hasAttribute(entry, 'p');
        layout.files.push_back(std::move(f));
      }
      out->push_back(std::move(layout));
    } else if (!tree) {
      Layout layout;
      layout.files.push_back(LayoutFile{{name}, hasAttribute(info, 'p')});
      out->push_back(std::move(layout));
    }
    if (tree) {
      Layout layout;
      PathComponents prefix{name};
      addTreeFiles(tree, &prefix, &layout.files, 0);
      out->push_back(std::move(layout));
    }
  }
  return std::nullopt;
}

std::optional<SafetyError> checkLayoutPaths(Layout const& layout) {
  for (auto const& file : layout.files) {
    if (auto err = checkComponentLimits(file.path)) {
      return err;
    }
    if (file.pad && !isPadPath(file.path)) {
      return unsafe("a file marked as padding does not have a .pad name");
    }
  }
  return checkOwnPaths(layout);
}

std::optional<SafetyError> checkLayout(std::string const& root,
                                       Layout const& layout) {
  std::string rootReal;
  if (auto err = canonicalRoot(root, &rootReal)) {
    return err;
  }
  if (auto err = checkLayoutPaths(layout)) {
    return err;
  }
  for (auto const& file : layout.files) {
    size_t total = rootReal.size();
    for (auto const& part : file.path) {
      total += part.size() + 1;
    }
    if (total > kMaxPathBytes) {
      return unsafe("a file path is too long");
    }
  }
  return walkExisting(rootReal, layout);
}

std::optional<SafetyError> checkRelativePaths(
    std::string const& root, std::vector<std::string> const& relativePaths,
    std::vector<bool> const& pad) {
  Layout layout;
  for (size_t i = 0; i < relativePaths.size(); i++) {
    LayoutFile f;
    if (auto err = splitRelativePath(relativePaths[i], &f.path)) {
      return err;
    }
    f.pad = i < pad.size() && pad[i];
    layout.files.push_back(std::move(f));
  }
  return checkLayout(root, layout);
}

int openConfined(std::string const& rootReal, PathComponents const& parts,
                 int flags, unsigned mode, bool createDirs,
                 SafetyError* err) {
  if (parts.empty()) {
    setError(err, "unsafe-path", "a file path is empty");
    return -1;
  }
  int rootFd = openRoot(rootReal, err);
  if (rootFd < 0) {
    return -1;
  }
  int dirFd = openDirChain(rootFd, parts, parts.size() - 1, createDirs, err);
  close(rootFd);
  if (dirFd < 0) {
    return -1;
  }
  if (!componentIsSafe(parts.back())) {
    close(dirFd);
    setError(err, "unsafe-path", "unsafe path component");
    return -1;
  }
  int fd = openat(dirFd, parts.back().c_str(),
                  flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, mode);
  int e = errno;
  close(dirFd);
  if (fd < 0) {
    setErrno(err, "cannot open " + parts.back(), e);
    return -1;
  }
  struct stat st;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    close(fd);
    setError(err, "unsafe-path", "not a regular file: " + parts.back());
    return -1;
  }
  if (st.st_nlink > 1) {
    close(fd);
    setError(err, "unsafe-path",
             "a file with other hard links is in the way: " + parts.back());
    return -1;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
  return fd;
}

std::optional<SafetyError> prepareDirectories(std::string const& root,
                                              Layout const& layout) {
  std::string rootReal;
  if (auto err = canonicalRoot(root, &rootReal)) {
    return err;
  }
  SafetyError err;
  int rootFd = openRoot(rootReal, &err);
  if (rootFd < 0) {
    return err;
  }
  std::set<std::string> done;
  for (auto const& file : layout.files) {
    if (file.pad || file.path.size() < 2) {
      continue;
    }
    if (auto bad = checkComponentLimits(file.path)) {
      close(rootFd);
      return bad;
    }
    std::string key = joinPath("", file.path, file.path.size() - 1);
    if (!done.insert(key).second) {
      continue;
    }
    int fd = openDirChain(rootFd, file.path, file.path.size() - 1, true, &err);
    if (fd < 0) {
      close(rootFd);
      return err;
    }
    close(fd);
  }
  close(rootFd);
  return std::nullopt;
}

std::string quarantineValue(int64_t seconds, std::string_view agent,
                            std::string_view uuid) {
  std::string cleanAgent;
  for (char c : agent) {
    if (c != ';' && c != '\0' && c != '\n') {
      cleanAgent += c;
    }
  }
  char head[64];
  std::snprintf(head, sizeof head, "0081;%llx;",
                static_cast<unsigned long long>(seconds));
  return std::string(head) + cleanAgent + ";" + std::string(uuid);
}

bool setQuarantineXattr(int fd, std::string_view agent) {
  std::string value = quarantineValue(static_cast<int64_t>(std::time(nullptr)),
                                      agent, newUuid());
  return fsetxattr(fd, kQuarantineAttr, value.data(), value.size(), 0, 0) == 0;
}

FinalizeReport finalizeFile(std::string const& rootReal,
                            PathComponents const& parts) {
  FinalizeReport report;
  if (auto bad = checkComponentLimits(parts)) {
    report.warning = bad->message;
    return report;
  }
  SafetyError err;
  int fd = openConfined(rootReal, parts, O_RDONLY, 0, false, &err);
  if (fd < 0) {
    report.warning = err.message;
    return report;
  }
  struct stat st;
  if (fstat(fd, &st) == 0) {
    mode_t cleared = st.st_mode & ~(S_IXUSR | S_IXGRP | S_IXOTH);
    if (cleared == st.st_mode || fchmod(fd, cleared & 07777) == 0) {
      report.executableCleared = true;
    } else {
      report.warning += "could not clear the execute bit: ";
      report.warning += std::strerror(errno);
      report.warning += "; ";
    }
  }
  std::string path = joinPath(rootReal, parts, parts.size());
  if (quarantineViaCoreFoundation(path) &&
      fgetxattr(fd, kQuarantineAttr, nullptr, 0, 0, 0) >= 0) {
    report.quarantined = true;
    report.quarantineMethod = "launchservices";
  } else {
    if (setQuarantineXattr(fd, "M6E")) {
      report.quarantined = true;
      report.quarantineMethod = "xattr";
    } else {
      report.warning += "could not set com.apple.quarantine: ";
      report.warning += std::strerror(errno);
    }
  }
  close(fd);
  return report;
}

FinalizeReport finalizeRelative(std::string const& rootReal,
                                std::string const& relativePath) {
  PathComponents parts;
  if (auto err = splitRelativePath(relativePath, &parts)) {
    FinalizeReport report;
    report.warning = err->message;
    return report;
  }
  return finalizeFile(rootReal, parts);
}

bool enterSandbox(std::string const& profilePath,
                  std::map<std::string, std::string> const& params,
                  std::string* error) {
  std::ifstream in(profilePath);
  if (!in) {
    if (error) {
      *error = "cannot read the sandbox profile";
    }
    return false;
  }
  std::stringstream text;
  text << in.rdbuf();
  std::string profile = text.str();
  std::vector<char const*> argv;
  for (auto const& [key, value] : params) {
    argv.push_back(key.c_str());
    argv.push_back(value.c_str());
  }
  argv.push_back(nullptr);
  char* errbuf = nullptr;
  int rc = sandbox_init_with_parameters(profile.c_str(), 0, argv.data(),
                                        &errbuf);
  if (rc != 0) {
    if (error) {
      *error = errbuf ? errbuf : "sandbox_init failed";
    }
    if (errbuf) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      sandbox_free_error(errbuf);
#pragma clang diagnostic pop
    }
    return false;
  }
  return true;
}

}  // namespace m6e
