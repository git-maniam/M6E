/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <mach-o/dyld.h>
#include <map>
#include <net/if.h>
#include <new>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "confine.hpp"
#include "engine.hpp"
#include "protocol.hpp"
#include "safety.hpp"
#include "sandbox_profile.hpp"

namespace json = m6e::json;
using namespace std::chrono_literals;

namespace {

constexpr auto kExitDeadline = 1700ms;
constexpr auto kResumeSaveBudget = 600ms;
constexpr std::size_t kMaxDownloadDirs = 4;

int gStopWrite = -1;

void onSignal(int) {
  char c = 1;
  (void)!::write(gStopWrite, &c, 1);
}

void usage() {
  std::fprintf(stderr,
               "usage: m6e-torrent --protocol 1 --profile-dir <absolute path>\n"
               "           [--sandbox [--inbox-dir <absolute path>]\n"
               "            [--download-dir <absolute path>]...]  (at most %zu "
               "download folders)\n",
               kMaxDownloadDirs);
}

int badArgument(char const* what, std::string const& value) {
  std::fprintf(stderr, "m6e-torrent: %s: %s\n", what, value.c_str());
  usage();
  return 2;
}

std::string resolvedPath(std::string const& path) {
  char buf[PATH_MAX];
  return realpath(path.c_str(), buf) ? std::string(buf) : std::string();
}

std::string executablePath() {
  char buf[PATH_MAX];
  std::uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) {
    return {};
  }
  return resolvedPath(buf);
}

bool makeProfileDir(std::string const& path) {
  std::size_t start = 1;
  while (start <= path.size()) {
    auto end = path.find('/', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    std::string current = path.substr(0, end);
    if (mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) {
      return false;
    }
    start = end + 1;
  }
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// M6E_TORRENT_TEST_INTERFACE_GATE names a file; while it is missing the bound
// interface counts as gone. It can only trigger the kill switch, never hide
// a missing interface.
std::function<bool(std::string const&)> interfaceLookup() {
  char const* gate = std::getenv("M6E_TORRENT_TEST_INTERFACE_GATE");
  std::string gatePath = gate ? gate : "";
  return [gatePath](std::string const& name) {
    if (if_nametoindex(name.c_str()) == 0) {
      return false;
    }
    return gatePath.empty() || access(gatePath.c_str(), F_OK) == 0;
  };
}

std::chrono::seconds idleSeconds() {
  char const* value = std::getenv("M6E_TORRENT_IDLE_SECONDS");
  if (!value || !*value) {
    return 60s;
  }
  char* end = nullptr;
  long n = std::strtol(value, &end, 10);
  if (*end || n < 0 || n > 86400) {
    return 60s;
  }
  return std::chrono::seconds(n);
}

json::object errorReply(std::int64_t id, m6e::RequestError const& e) {
  json::object error;
  error["code"] = e.code;
  error["message"] = e.message;
  if (!e.tid.empty()) {
    error["tid"] = e.tid;
  }
  json::object reply;
  reply["id"] = id;
  reply["ok"] = false;
  reply["error"] = std::move(error);
  return reply;
}

void protocolErrorEvent(m6e::Output& out, std::string const& message) {
  json::object e = m6e::event("error");
  e["code"] = "bad-request";
  e["message"] = message;
  out.write(e);
}

// Handles one request line. Returns false when the helper must stop.
bool dispatch(std::string_view line, m6e::Engine& engine, m6e::Output& out) {
  if (line.empty()) {
    return true;
  }
  boost::system::error_code ec;
  json::parse_options options;
  options.max_depth = 64;
  json::value value = json::parse(line, ec, {}, options);
  if (ec || !value.is_object()) {
    protocolErrorEvent(out, "a request is not a JSON object");
    return true;
  }
  auto const& request = value.as_object();
  auto const* idValue = request.if_contains("id");
  auto id = idValue ? m6e::asInt(*idValue) : std::nullopt;
  if (!id) {
    protocolErrorEvent(out, "a request has no integer id");
    return true;
  }
  auto const* opValue = request.if_contains("op");
  json::object noArgs;
  json::object const* args = &noArgs;
  if (auto const* a = request.if_contains("args"); a && !a->is_null()) {
    if (!a->is_object()) {
      out.write(errorReply(*id, {"bad-request", "args must be an object", {}}));
      return true;
    }
    args = &a->as_object();
  }
  if (!opValue || !opValue->is_string()) {
    out.write(errorReply(*id, {"bad-request", "op must be a string", {}}));
    return true;
  }
  std::string op(opValue->as_string());
  engine.noteRequest(m6e::Clock::now());
  if (op == "shutdown") {
    json::object reply;
    reply["id"] = *id;
    reply["ok"] = true;
    reply["result"] = json::object();
    out.write(reply);
    return false;
  }
  try {
    json::object reply;
    reply["id"] = *id;
    reply["ok"] = true;
    reply["result"] = engine.handle(op, *args);
    if (!out.write(reply) && !out.closed()) {
      out.write(errorReply(
          *id,
          {"too-large", "the reply is longer than the protocol allows", {}}));
    }
  } catch (m6e::RequestError const& e) {
    out.write(errorReply(*id, e));
  } catch (std::bad_alloc const&) {
    out.write(errorReply(*id, {"engine-error", "out of memory", {}}));
  } catch (std::exception const& e) {
    out.write(errorReply(*id, {"engine-error", e.what(), {}}));
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string profileDir;
  int protocol = 0;
  bool sandbox = false;
  bool inboxGiven = false;
  std::string inboxDir;
  std::vector<std::string> downloadDirs;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "--protocol" && i + 1 < argc) {
      protocol = std::atoi(argv[++i]);
    } else if (arg == "--profile-dir" && i + 1 < argc) {
      profileDir = argv[++i];
    } else if (arg == "--sandbox") {
      sandbox = true;
    } else if (arg == "--inbox-dir" && i + 1 < argc && !inboxGiven) {
      inboxGiven = true;
      inboxDir = argv[++i];
    } else if (arg == "--download-dir" && i + 1 < argc) {
      downloadDirs.push_back(argv[++i]);
    } else {
      usage();
      return 2;
    }
  }
  if (protocol != 1) {
    std::fprintf(stderr, "m6e-torrent: only protocol 1 is supported\n");
    return 2;
  }
  if (!m6e::isAbsolutePath(profileDir)) {
    usage();
    return 2;
  }
  if (!sandbox && (inboxGiven || !downloadDirs.empty())) {
    return badArgument("--inbox-dir and --download-dir need --sandbox",
                       inboxGiven ? inboxDir : downloadDirs.front());
  }
  if (downloadDirs.size() > kMaxDownloadDirs) {
    return badArgument("too many --download-dir folders",
                       std::to_string(downloadDirs.size()));
  }
  std::vector<std::string> saveRoots;
  for (auto const& dir : downloadDirs) {
    struct stat st;
    std::string real = m6e::isAbsolutePath(dir) ? resolvedPath(dir) : "";
    if (real.empty() || real == "/" || stat(real.c_str(), &st) != 0 ||
        !S_ISDIR(st.st_mode)) {
      return badArgument(
          "--download-dir must be an existing folder, given as an absolute "
          "path, and not /",
          dir);
    }
    if (std::find(saveRoots.begin(), saveRoots.end(), real) ==
        saveRoots.end()) {
      saveRoots.push_back(real);
    }
  }
  while (profileDir.size() > 1 && profileDir.back() == '/') {
    profileDir.pop_back();
  }
  if (inboxGiven && !m6e::isAbsolutePath(inboxDir)) {
    return badArgument("--inbox-dir must be an absolute path", inboxDir);
  }
  if (!inboxGiven) {
    inboxDir = profileDir + "/inbox";
  }
  if (!makeProfileDir(profileDir)) {
    std::fprintf(stderr, "m6e-torrent: cannot create %s: %s\n",
                 profileDir.c_str(), std::strerror(errno));
    return 1;
  }
  char resolved[PATH_MAX];
  if (!realpath(profileDir.c_str(), resolved)) {
    std::fprintf(stderr, "m6e-torrent: cannot resolve %s\n",
                 profileDir.c_str());
    return 1;
  }
  std::string inboxReal;
  if (sandbox) {
    if (!makeProfileDir(inboxDir) ||
        (inboxReal = resolvedPath(inboxDir)).empty()) {
      std::fprintf(stderr, "m6e-torrent: cannot create %s: %s\n",
                   inboxDir.c_str(), std::strerror(errno));
      return 1;
    }
    std::vector<std::string> writable = saveRoots;
    writable.push_back(resolved);
    for (auto const& root : writable) {
      if (m6e::pathIsWithin(inboxReal, root)) {
        return badArgument(
            "--inbox-dir must not be or contain the profile or a download "
            "folder",
            inboxDir);
      }
    }
    std::string helper = executablePath();
    if (helper.empty()) {
      std::fprintf(stderr, "m6e-torrent: cannot find its own executable\n");
      return 1;
    }
    std::map<std::string, std::string> params = {
        {"HELPER", helper},
        {"EXEC_HELPER", "NO"},
        {"TORRENTS_DIR", resolved},
        {"INBOX_DIR", inboxReal},
    };
    for (std::size_t i = 0; i < kMaxDownloadDirs; i++) {
      params["DOWNLOAD_" + std::to_string(i + 1)] =
          i < saveRoots.size() ? saveRoots[i] : std::string(resolved);
    }
    std::string error;
    if (!m6e::enterSandboxWithProfile(m6e::kSandboxProfile, params, &error)) {
      std::fprintf(stderr, "m6e-torrent: cannot enter the sandbox: %s\n",
                   error.c_str());
      return 1;
    }
  }

  std::signal(SIGPIPE, SIG_IGN);
  int stopPipe[2];
  if (pipe(stopPipe) != 0) {
    return 1;
  }
  for (int fd : stopPipe) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  gStopWrite = stopPipe[1];
  struct sigaction sa{};
  sa.sa_handler = onSignal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);

  m6e::Output out;
  std::unique_ptr<m6e::Engine> engine;
  m6e::EngineOptions options;
  options.profileDir = resolved;
  options.idleExit = idleSeconds();
  options.interfacePresent = interfaceLookup();
  if (sandbox) {
    options.inboxDir = inboxReal;
    options.saveRoots = saveRoots;
  }
  try {
    engine = std::make_unique<m6e::Engine>(
        std::move(options), [&out](json::object const& e) { out.write(e); });
  } catch (std::exception const& e) {
    std::fprintf(stderr, "m6e-torrent: cannot start the engine: %s\n",
                 e.what());
    return 1;
  }
  json::object ready = m6e::event("ready");
  ready["protocol"] = 1;
  ready["engine"] = m6e::Engine::engineName();
  ready["pid"] = static_cast<std::int64_t>(getpid());
  ready["sandbox"] = sandbox;
  out.write(ready);

  m6e::LineReader reader;
  bool running = true;
  char buf[65536];
  while (running) {
    auto now = m6e::Clock::now();
    auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
        engine->nextTick() - now);
    int timeout = static_cast<int>(std::max<std::int64_t>(0, wait.count()));
    pollfd fds[3] = {{STDIN_FILENO, POLLIN, 0},
                     {engine->wakeFd(), POLLIN, 0},
                     {stopPipe[0], POLLIN, 0}};
    int n = poll(fds, 3, timeout);
    if (n < 0 && errno != EINTR) {
      break;
    }
    if (n > 0 && fds[2].revents) {
      break;
    }
    if (n > 0 && fds[1].revents) {
      engine->processAlerts();
    }
    if (n > 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      ssize_t got = read(STDIN_FILENO, buf, sizeof buf);
      if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) {
        break;
      }
      if (got > 0) {
        running = reader.feed(
            buf, static_cast<std::size_t>(got),
            [&](std::string_view line) { return dispatch(line, *engine, out); },
            [&] {
              protocolErrorEvent(out, "a request line is longer than 1 MiB");
            });
      }
    }
    now = m6e::Clock::now();
    if (now >= engine->nextTick()) {
      engine->tick(now);
    }
    if (engine->idleExpired(now)) {
      std::fprintf(stderr, "m6e-torrent: idle, exiting\n");
      break;
    }
    if (out.closed()) {
      break;
    }
  }

  std::thread([] {
    std::this_thread::sleep_for(kExitDeadline);
    _exit(0);
  }).detach();
  try {
    engine->shutdown(m6e::Clock::now() + kResumeSaveBudget);
  } catch (std::exception const& e) {
    std::fprintf(stderr, "m6e-torrent: shutdown: %s\n", e.what());
  }
  _exit(0);
}
