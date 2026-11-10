// Unit tests for confine.cpp and the path rules of safety.cpp. Everything
// happens in a fresh temporary folder; no network, no real torrent.
// Usage: confine_tests <folder with generated .torrent files>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

#include <libtorrent/bdecode.hpp>

#include "confine.hpp"
#include "safety.hpp"

namespace lt = libtorrent;
using namespace m6e;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    checks++;                                                            \
    if (!(cond)) {                                                       \
      failures++;                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                    \
  } while (0)

#define CHECK_CODE(expr, wanted)                                          \
  do {                                                                    \
    auto e_ = (expr);                                                     \
    checks++;                                                             \
    if (!e_ || e_->code != (wanted)) {                                    \
      failures++;                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__,  \
                   #expr, e_ ? e_->message.c_str() : "accepted");         \
    }                                                                     \
  } while (0)

#define CHECK_OK(expr)                                                    \
  do {                                                                    \
    auto e_ = (expr);                                                     \
    checks++;                                                             \
    if (e_) {                                                             \
      failures++;                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__,  \
                   #expr, e_->message.c_str());                           \
    }                                                                     \
  } while (0)

std::string tmpRoot;

std::string mkdtemp_() {
  char const* base = getenv("TMPDIR");
  std::string tmpl = std::string(base ? base : "/tmp") + "/m6e-confine-XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  if (!mkdtemp(buf.data())) {
    std::perror("mkdtemp");
    std::exit(2);
  }
  char real[PATH_MAX];
  realpath(buf.data(), real);
  return real;
}

void mkdirs(std::string const& path) {
  std::string cur;
  std::stringstream ss(path);
  std::string part;
  while (std::getline(ss, part, '/')) {
    if (part.empty()) {
      continue;
    }
    cur += "/" + part;
    mkdir(cur.c_str(), 0755);
  }
}

void touch(std::string const& path, mode_t mode = 0644) {
  int fd = open(path.c_str(), O_CREAT | O_WRONLY, mode);
  if (fd >= 0) {
    (void)!write(fd, "data", 4);
    close(fd);
    chmod(path.c_str(), mode);
  }
}

bool exists(std::string const& path) {
  struct stat st;
  return lstat(path.c_str(), &st) == 0;
}

std::string fresh(std::string const& label) {
  std::string dir = tmpRoot + "/" + label;
  mkdirs(dir);
  return dir;
}

Layout layoutOf(std::vector<PathComponents> const& paths) {
  Layout l;
  for (auto const& p : paths) {
    l.files.push_back(LayoutFile{p, false});
  }
  return l;
}

void testComponents() {
  CHECK_CODE(checkComponentLimits({"a", "..", "b"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"a", "", "b"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"a", std::string("b\0c", 3)}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"/etc", "passwd"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"a/b"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"a\\b"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"a:b"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"line\nbreak"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"\x7f"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"bad\xff"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"\xc0\xaf"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"\xed\xa0\x80"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({"\xe2\x82"}), "unsafe-path");
  CHECK_CODE(checkComponentLimits({std::string(256, 'a')}), "unsafe-path");
  CHECK_OK(checkComponentLimits({std::string(255, 'a')}));
  CHECK_CODE(checkComponentLimits({}), "unsafe-path");
  PathComponents deep(70, "d");
  CHECK_CODE(checkComponentLimits(deep), "unsafe-path");
  PathComponents wide(40, std::string(200, 'w'));
  CHECK_CODE(checkComponentLimits(wide), "unsafe-path");
  CHECK_OK(checkComponentLimits({"\xc3\xa9", "\xe6\x97\xa5\xe6\x9c\xac", "\xf0\x9f\x98\x80.txt"}));

  PathComponents out;
  CHECK_CODE(splitRelativePath("", &out), "unsafe-path");
  CHECK_CODE(splitRelativePath("/abs/path", &out), "unsafe-path");
  CHECK_CODE(splitRelativePath("a//b", &out), "unsafe-path");
  CHECK_CODE(splitRelativePath("a/b/", &out), "unsafe-path");
  CHECK_CODE(splitRelativePath("../x", &out), "unsafe-path");
  CHECK_CODE(splitRelativePath("a/../x", &out), "unsafe-path");
  CHECK_CODE(splitRelativePath(std::string("a/b\0c", 5), &out), "unsafe-path");
  CHECK_OK(splitRelativePath("name/dir/file.bin", &out));
  CHECK(out.size() == 3 && out[2] == "file.bin");
}

void testFold() {
  CHECK(foldName("README") == foldName("readme"));
  CHECK(foldName("\xc3\xa9") == foldName("e\xcc\x81"));
  CHECK(foldName("\xc3\x89") == foldName("e\xcc\x81"));
  CHECK(foldName("a") != foldName("b"));
  CHECK(foldName("bad\xff").empty());
  CHECK(isValidUtf8("plain") && !isValidUtf8("\xff"));
}

void testOwnCollisions() {
  CHECK_CODE(checkLayoutPaths(layoutOf({{"t", "Readme"}, {"t", "README"}})),
             "unsafe-path");
  CHECK_CODE(checkLayoutPaths(layoutOf({{"t", "Dir", "a"}, {"t", "dir", "b"}})),
             "unsafe-path");
  CHECK_CODE(checkLayoutPaths(layoutOf({{"T", "a"}, {"t", "b"}})),
             "unsafe-path");
  CHECK_CODE(checkLayoutPaths(layoutOf({{"t", "\xc3\xa9"}, {"t", "e\xcc\x81"}})),
             "unsafe-path");
  CHECK_CODE(checkLayoutPaths(layoutOf({{"t", "a"}, {"t", "a"}})),
             "unsafe-path");
  CHECK_CODE(checkLayoutPaths(layoutOf({{"t", "a"}, {"t", "a", "b"}})),
             "unsafe-path");
  CHECK_CODE(checkLayoutPaths(layoutOf({{"t", "a", "b"}, {"t", "a"}})),
             "unsafe-path");
  CHECK_OK(checkLayoutPaths(layoutOf({{"t", "a", "x"}, {"t", "a", "y"}, {"t", "b"}})));

  Layout pad = layoutOf({{"t", "a"}});
  pad.files.push_back(LayoutFile{{"t", ".pad", "1234"}, true});
  CHECK_OK(checkLayoutPaths(pad));
  Layout fakePad = layoutOf({{"t", "a"}});
  fakePad.files.push_back(LayoutFile{{"t", "movie.mkv"}, true});
  CHECK_CODE(checkLayoutPaths(fakePad), "unsafe-path");
  Layout badPad = layoutOf({{"t", "a"}});
  badPad.files.push_back(LayoutFile{{"t", ".pad", "x"}, true});
  CHECK_CODE(checkLayoutPaths(badPad), "unsafe-path");
}

void testRoot() {
  std::string real;
  CHECK_CODE(canonicalRoot("relative/dir", &real), "unsafe-path");
  CHECK_CODE(canonicalRoot("", &real), "unsafe-path");
  CHECK_CODE(canonicalRoot("/", &real), "unsafe-path");
  CHECK_CODE(canonicalRoot("/..", &real), "unsafe-path");
  CHECK_CODE(canonicalRoot(std::string("/tmp\0x", 6), &real), "unsafe-path");
  CHECK(canonicalRoot(tmpRoot + "/does-not-exist", &real).has_value());
  std::string file = tmpRoot + "/plainfile";
  touch(file);
  CHECK_CODE(canonicalRoot(file, &real), "unsafe-path");

  std::string target = fresh("rootlink-target");
  std::string link = tmpRoot + "/rootlink";
  symlink(target.c_str(), link.c_str());
  CHECK_OK(canonicalRoot(link, &real));
  CHECK(real == target);

  std::string profile = fresh("profile");
  std::string torrents;
  CHECK_OK(ensureProfileTorrentsDir(profile, &torrents));
  CHECK(torrents == profile + "/torrents");
  CHECK_OK(ensureProfileTorrentsDir(profile, &torrents));
  struct stat st;
  stat(torrents.c_str(), &st);
  CHECK((st.st_mode & 0777) == 0700);

  std::string profile2 = fresh("profile2");
  std::string elsewhere = fresh("elsewhere");
  symlink(elsewhere.c_str(), (profile2 + "/torrents").c_str());
  CHECK_CODE(ensureProfileTorrentsDir(profile2, &torrents), "unsafe-path");
}

void testExistingTree() {
  std::string outside = fresh("hostile-outside");
  touch(outside + "/victim.txt");

  std::string root = fresh("hostile-root");
  CHECK_OK(checkRelativePaths(root, {"name/a.txt"}, {}));

  symlink(outside.c_str(), (root + "/linked").c_str());
  CHECK_CODE(checkRelativePaths(root, {"linked/victim.txt"}, {}), "unsafe-path");
  CHECK_CODE(checkRelativePaths(root, {"linked"}, {}), "unsafe-path");

  mkdirs(root + "/mid/sub");
  symlink(outside.c_str(), (root + "/mid/sub/out").c_str());
  CHECK_CODE(checkRelativePaths(root, {"mid/sub/out/victim.txt"}, {}),
             "unsafe-path");
  CHECK_OK(checkRelativePaths(root, {"mid/sub/other/file"}, {}));

  mkdirs(root + "/leaf");
  symlink((outside + "/victim.txt").c_str(), (root + "/leaf/file").c_str());
  CHECK_CODE(checkRelativePaths(root, {"leaf/file"}, {}), "unsafe-path");

  symlink("/nonexistent/dangling", (root + "/dangling").c_str());
  CHECK_CODE(checkRelativePaths(root, {"dangling"}, {}), "unsafe-path");

  mkdirs(root + "/Existing/dir");
  touch(root + "/Existing/Report.pdf");
  CHECK_CODE(checkRelativePaths(root, {"existing/x"}, {}), "unsafe-path");
  CHECK_CODE(checkRelativePaths(root, {"Existing/report.pdf"}, {}),
             "unsafe-path");
  CHECK_OK(checkRelativePaths(root, {"Existing/Report.pdf"}, {}));

  touch(root + "/e\xcc\x81.txt");
  CHECK_CODE(checkRelativePaths(root, {"\xc3\xa9.txt"}, {}), "unsafe-path");
  CHECK_CODE(checkRelativePaths(root, {"\xc3\x89.TXT"}, {}), "unsafe-path");

  touch(root + "/plain");
  CHECK_CODE(checkRelativePaths(root, {"plain/child"}, {}), "unsafe-path");
  mkdirs(root + "/adir");
  CHECK_CODE(checkRelativePaths(root, {"adir"}, {}), "unsafe-path");

  touch(root + "/hl-original");
  link((root + "/hl-original").c_str(), (root + "/hl-copy").c_str());
  CHECK_CODE(checkRelativePaths(root, {"hl-copy"}, {}), "unsafe-path");

  std::string fifo = root + "/fifo";
  mkfifo(fifo.c_str(), 0644);
  CHECK_CODE(checkRelativePaths(root, {"fifo"}, {}), "unsafe-path");

  CHECK_CODE(checkRelativePaths(root, {"../escape.txt"}, {}), "unsafe-path");
  CHECK_CODE(checkRelativePaths(root, {"/etc/passwd"}, {}), "unsafe-path");
  CHECK_CODE(checkRelativePaths(root, {std::string(300, 'n') + "/x"}, {}),
             "unsafe-path");
  CHECK_CODE(checkRelativePaths(root, {std::string("n\0n/x", 5)}, {}),
             "unsafe-path");
  CHECK_CODE(checkRelativePaths(root + "/nope", {"a"}, {}), "io-error");
  CHECK(!exists(outside + "/escape.txt"));
}

void testOpenConfined() {
  std::string outside = fresh("open-outside");
  touch(outside + "/victim.txt");
  std::string root = fresh("open-root");
  SafetyError err;

  int fd = openConfined(root, {"t", "sub", "f.bin"}, O_CREAT | O_WRONLY, 0644,
                        true, &err);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
  }
  CHECK(exists(root + "/t/sub/f.bin"));

  fd = openConfined(root, {"t", "missing", "f.bin"}, O_WRONLY, 0, false, &err);
  CHECK(fd < 0);

  symlink(outside.c_str(), (root + "/t/swap").c_str());
  fd = openConfined(root, {"t", "swap", "victim.txt"}, O_WRONLY | O_TRUNC, 0,
                    true, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");
  fd = openConfined(root, {"t", "swap", "new.txt"}, O_CREAT | O_WRONLY, 0644,
                    true, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");
  CHECK(!exists(outside + "/new.txt"));

  symlink((outside + "/victim.txt").c_str(), (root + "/t/leaf").c_str());
  fd = openConfined(root, {"t", "leaf"}, O_WRONLY | O_TRUNC, 0, true, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");
  struct stat st;
  stat((outside + "/victim.txt").c_str(), &st);
  CHECK(st.st_size == 4);

  fd = openConfined(root, {"..", "x"}, O_CREAT | O_WRONLY, 0644, true, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");
  fd = openConfined(root, {"a", std::string("b\0c", 3)}, O_CREAT | O_WRONLY,
                    0644, true, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");
  fd = openConfined("relative", {"a"}, O_CREAT | O_WRONLY, 0644, true, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");

  std::string linkRoot = tmpRoot + "/open-rootlink";
  symlink(root.c_str(), linkRoot.c_str());
  fd = openConfined(linkRoot, {"x"}, O_CREAT | O_WRONLY, 0644, true, &err);
  CHECK(fd < 0);

  touch(root + "/hl-a");
  link((root + "/hl-a").c_str(), (root + "/hl-b").c_str());
  fd = openConfined(root, {"hl-b"}, O_WRONLY, 0, false, &err);
  CHECK(fd < 0 && err.code == "unsafe-path");

  mkfifo((root + "/fifo").c_str(), 0644);
  fd = openConfined(root, {"fifo"}, O_WRONLY, 0, false, &err);
  CHECK(fd < 0);

  // Time of check to time of use: the check passes, then a symlink appears.
  std::string race = fresh("open-race");
  Layout layout = layoutOf({{"t", "d", "f"}});
  CHECK_OK(checkLayout(race, layout));
  CHECK_OK(prepareDirectories(race, layout));
  CHECK(exists(race + "/t/d"));
  rmdir((race + "/t/d").c_str());
  symlink(outside.c_str(), (race + "/t/d").c_str());
  fd = openConfined(race, {"t", "d", "f"}, O_CREAT | O_WRONLY, 0644, false,
                    &err);
  CHECK(fd < 0 && err.code == "unsafe-path");
  CHECK(!exists(outside + "/f"));
  CHECK_CODE(prepareDirectories(race, layout), "unsafe-path");
}

void testPrepare() {
  std::string outside = fresh("prep-outside");
  std::string root = fresh("prep-root");
  mkdirs(root + "/t");
  symlink(outside.c_str(), (root + "/t/sub").c_str());
  Layout layout = layoutOf({{"t", "sub", "deeper", "f"}});
  CHECK_CODE(prepareDirectories(root, layout), "unsafe-path");
  CHECK(!exists(outside + "/deeper"));

  std::string root2 = fresh("prep-root2");
  Layout ok = layoutOf({{"t", "a", "b", "f1"}, {"t", "a", "g"}, {"t", "c"}});
  ok.files.push_back(LayoutFile{{"t", ".pad", "9"}, true});
  CHECK_OK(prepareDirectories(root2, ok));
  CHECK(exists(root2 + "/t/a/b"));
  CHECK(!exists(root2 + "/t/.pad"));
}

void testFinalize() {
  std::string root = fresh("fin-root");
  mkdirs(root + "/t");
  touch(root + "/t/run.sh", 0755);
  touch(root + "/t/plain.txt", 0644);
  touch(root + "/t/group", 0771);

  auto r = finalizeRelative(root, "t/run.sh");
  CHECK(r.executableCleared);
  CHECK(r.quarantined);
  struct stat st;
  stat((root + "/t/run.sh").c_str(), &st);
  CHECK((st.st_mode & 0111) == 0);
  CHECK((st.st_mode & 0644) == 0644);

  char buf[256] = {0};
  ssize_t n = getxattr((root + "/t/run.sh").c_str(), "com.apple.quarantine", buf,
                       sizeof buf - 1, 0, 0);
  CHECK(n > 0);
  std::string value(buf, n > 0 ? n : 0);
  std::fprintf(stderr, "info: quarantine via %s = %s\n",
               r.quarantineMethod.c_str(), value.c_str());
  CHECK(std::regex_match(
      value, std::regex("[0-9a-f]{4};[0-9a-f]+;[^;]*;[0-9A-Fa-f-]{36}")));

  {
    touch(root + "/t/direct", 0644);
    int dfd = open((root + "/t/direct").c_str(), O_RDONLY);
    CHECK(setQuarantineXattr(dfd, "M6E"));
    char dbuf[256] = {0};
    ssize_t dn = fgetxattr(dfd, "com.apple.quarantine", dbuf, sizeof dbuf - 1, 0, 0);
    close(dfd);
    CHECK(dn > 0);
    CHECK(std::regex_match(
        std::string(dbuf, dn > 0 ? dn : 0),
        std::regex("0081;[0-9a-f]+;M6E;[0-9A-F-]{36}")));
  }

  r = finalizeRelative(root, "t/group");
  stat((root + "/t/group").c_str(), &st);
  CHECK(r.executableCleared && (st.st_mode & 0111) == 0);

  std::string v = quarantineValue(0x6af3317b, "M6E", "921CABF4-5F25-4E90-8058-1284566E6144");
  CHECK(v == "0081;6af3317b;M6E;921CABF4-5F25-4E90-8058-1284566E6144");
  CHECK(quarantineValue(1, "a;b\nc", "U") == "0081;1;abc;U");

  std::string outside = fresh("fin-outside");
  touch(outside + "/victim", 0755);
  symlink((outside + "/victim").c_str(), (root + "/t/link").c_str());
  r = finalizeRelative(root, "t/link");
  CHECK(!r.executableCleared && !r.quarantined && !r.warning.empty());
  stat((outside + "/victim").c_str(), &st);
  CHECK((st.st_mode & 0100) != 0);
  CHECK(getxattr((outside + "/victim").c_str(), "com.apple.quarantine", buf,
                 sizeof buf, 0, 0) < 0);

  r = finalizeRelative(root, "../x");
  CHECK(!r.quarantined && !r.warning.empty());
  r = finalizeRelative(root, "t/missing");
  CHECK(!r.quarantined && !r.warning.empty());
}

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void testTorrentFiles(std::string const& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) {
    std::fprintf(stderr, "FAIL cannot open %s\n", dir.c_str());
    failures++;
    return;
  }
  int rejected = 0;
  int accepted = 0;
  while (dirent* e = readdir(d)) {
    std::string name = e->d_name;
    bool expectReject = name.rfind("reject-", 0) == 0;
    bool expectAccept = name.rfind("accept-", 0) == 0;
    if (!(expectReject || expectAccept) ||
        name.size() < 9 || name.substr(name.size() - 8) != ".torrent") {
      continue;
    }
    std::string data = readFile(dir + "/" + name);
    lt::error_code ec;
    lt::bdecode_node top = lt::bdecode(
        lt::span<char const>(data.data(), static_cast<int>(data.size())), ec,
        nullptr, kMaxBencodeDepth, kMaxBencodeItems);
    checks++;
    if (ec) {
      failures++;
      std::fprintf(stderr, "FAIL %s did not decode: %s\n", name.c_str(),
                   ec.message().c_str());
      continue;
    }
    auto info = top.dict_find("info");
    auto err = checkInfoDict(info);
    checks++;
    if (expectReject) {
      rejected++;
      if (!err) {
        failures++;
        std::fprintf(stderr, "FAIL %s was accepted\n", name.c_str());
      } else {
        std::fprintf(stderr, "info: %s -> %s\n", name.c_str(),
                     err->message.c_str());
      }
    } else {
      accepted++;
      if (err) {
        failures++;
        std::fprintf(stderr, "FAIL %s was rejected: %s\n", name.c_str(),
                     err->message.c_str());
      } else {
        std::vector<Layout> layouts;
        CHECK_OK(layoutsFromInfo(info, &layouts));
        CHECK(!layouts.empty());
        std::string root = fresh("accept-" + name);
        for (auto const& l : layouts) {
          CHECK_OK(checkLayout(root, l));
        }
      }
    }
  }
  closedir(d);
  std::fprintf(stderr, "info: %d hostile and %d valid torrent files checked\n",
               rejected, accepted);
  CHECK(rejected >= 20);
  CHECK(accepted >= 3);
}

}  // namespace

int main(int argc, char** argv) {
  tmpRoot = mkdtemp_();
  testComponents();
  testFold();
  testOwnCollisions();
  testRoot();
  testExistingTree();
  testOpenConfined();
  testPrepare();
  testFinalize();
  if (argc > 1) {
    testTorrentFiles(argv[1]);
  }
  std::string cmd = "/bin/rm -rf '" + tmpRoot + "'";
  (void)!std::system(cmd.c_str());
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
