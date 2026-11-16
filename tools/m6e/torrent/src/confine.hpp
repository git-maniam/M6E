#pragma once

// Write confinement for the helper (spec 8.4 "Confinement", 8.5 "Files").
// Every path is a vector of components under a canonical (realpath) root, the
// torrent's save folder or <profile>/torrents. Nothing here follows a symlink.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "safety.hpp"

namespace m6e {

constexpr size_t kMaxComponentBytes = 255;
constexpr size_t kMaxPathBytes = 1000;

using PathComponents = std::vector<std::string>;

// A file the engine will create below the root. `path` starts with the
// torrent name for multi-file torrents (and is just the name for a single
// file). `pad` marks a BEP 47 padding file, which is never written to disk.
struct LayoutFile {
  PathComponents path;
  bool pad = false;
};

struct Layout {
  std::vector<LayoutFile> files;
};

struct FinalizeReport {
  bool executableCleared = false;
  bool quarantined = false;
  std::string quarantineMethod;
  std::string warning;
};

bool isValidUtf8(std::string_view s);

// Case and Unicode-normalisation fold used to find names that APFS treats as
// the same file. Empty for invalid UTF-8.
std::string foldName(std::string_view name);

// Splits "a/b/c" and rejects empty, dot, dot-dot, NUL, absolute, over-long,
// non-UTF-8 and control-character components.
std::optional<SafetyError> splitRelativePath(std::string const& path,
                                             PathComponents* out);

std::optional<SafetyError> checkComponentLimits(PathComponents const& parts);

// Resolves a root to its canonical form. The root must be absolute, an
// existing directory and not "/".
std::optional<SafetyError> canonicalRoot(std::string const& root,
                                         std::string* rootReal);

// Creates <profile>/torrents (0700) if missing and rejects a symlink there.
std::optional<SafetyError> ensureProfileTorrentsDir(
    std::string const& profileDir, std::string* torrentsReal);

// Builds the layouts the engine could use from an info dictionary: the plain
// names, and the "name.utf-8" and "path.utf-8" variants where present.
std::optional<SafetyError> layoutsFromInfo(
    libtorrent::bdecode_node const& info, std::vector<Layout>* out);

// The checks that need no file system: limits, padding rules, collisions.
std::optional<SafetyError> checkLayoutPaths(Layout const& layout);

// The full check run before payload transfer starts: component limits,
// padding rules, collisions among the torrent's own paths, then a no-follow
// walk of what already exists below the root (symlinks, non-regular files,
// hard links, differently cased or normalised names).
std::optional<SafetyError> checkLayout(std::string const& root,
                                       Layout const& layout);

// Same, from the engine's own path strings ("name/dir/file", relative to the
// save folder), with the padding flag per file.
std::optional<SafetyError> checkRelativePaths(
    std::string const& root, std::vector<std::string> const& relativePaths,
    std::vector<bool> const& pad);

// Opens <root>/<components> by walking with openat and O_NOFOLLOW, so a
// symlink at any level fails. Returns the descriptor or -1 with `err` set.
// createDirs makes missing parents (0755). flags may include O_CREAT.
int openConfined(std::string const& rootReal, PathComponents const& parts,
                 int flags, unsigned mode, bool createDirs, SafetyError* err);

// Creates every parent directory of every non-pad file, no-follow.
std::optional<SafetyError> prepareDirectories(std::string const& root,
                                              Layout const& layout);

// "flags;hex seconds;agent;UUID", the form of com.apple.quarantine.
std::string quarantineValue(int64_t seconds, std::string_view agent,
                            std::string_view uuid);

// The direct fallback: writes com.apple.quarantine on an open descriptor.
bool setQuarantineXattr(int fd, std::string_view agent);

// On a finished file: clears all execute bits and sets com.apple.quarantine.
// Never fails hard; what could not be done is in `warning`.
FinalizeReport finalizeFile(std::string const& rootReal,
                            PathComponents const& parts);

// Same for a file whose canonical absolute path is already known to be
// inside rootReal (checked again here).
FinalizeReport finalizeRelative(std::string const& rootReal,
                                std::string const& relativePath);

// For a file the engine writes, before and while it writes it: opens it
// no-follow (with `create`, creating it 0644 when missing), clears its execute
// bits and sets com.apple.quarantine when it has none. libtorrent keeps the
// mode of an existing file, so a file prepared here never becomes executable
// from a BEP 47 `x` attribute. A missing file without `create` is skipped
// (`existed` false). A link or non-regular file in the way is an error; a
// quarantine that could not be set is only reported in `warning`.
std::optional<SafetyError> protectFile(std::string const& rootReal,
                                       PathComponents const& parts,
                                       bool create, bool* existed,
                                       std::string* warning);

// Deletes the regular files at `files` below rootReal without following any
// link (a link is left alone), then the folders above them, deepest first,
// that are left empty. Missing files are skipped.
std::optional<SafetyError> removeFiles(std::string const& rootReal,
                                       std::vector<PathComponents> const& files);

// Applies an SBPL profile to this process (sandbox_init_with_parameters).
// After it returns true the process can no longer exec or leave its roots.
bool enterSandbox(std::string const& profilePath,
                  std::map<std::string, std::string> const& params,
                  std::string* error);

// Same, with the profile text itself (the helper's built-in copy).
bool enterSandboxWithProfile(std::string const& profile,
                             std::map<std::string, std::string> const& params,
                             std::string* error);

// Whether the canonical path `pathReal` is `rootReal` or below it.
bool pathIsWithin(std::string const& rootReal, std::string const& pathReal);

}  // namespace m6e
