// Test program for the sandbox profile. Each command exits 0 on success and 1
// (with the errno text on stderr) on failure. It uses only the confine code
// the helper uses, so a pass under sandbox-exec says the helper's file paths
// work inside the profile and that everything else is denied.

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <IOKit/pwr_mgt/IOPMLib.h>
#include <SystemConfiguration/SystemConfiguration.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../../src/confine.hpp"

extern char** environ;

namespace {

int fail(std::string const& what) {
  std::fprintf(stderr, "%s: %s\n", what.c_str(), std::strerror(errno));
  return 1;
}

int run(std::vector<std::string> const& a);

int cmdWrite(std::string const& path) {
  int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    return fail("write " + path);
  }
  if (write(fd, "x", 1) != 1) {
    close(fd);
    return fail("write data");
  }
  close(fd);
  return 0;
}

int cmdRead(std::string const& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return fail("read " + path);
  }
  char c;
  ssize_t n = read(fd, &c, 1);
  close(fd);
  return n < 0 ? fail("read data") : 0;
}

int cmdList(std::string const& path) {
  DIR* d = opendir(path.c_str());
  if (!d) {
    return fail("list " + path);
  }
  closedir(d);
  return 0;
}

int cmdStat(std::string const& path) {
  struct stat st;
  return lstat(path.c_str(), &st) == 0 ? 0 : fail("stat " + path);
}

int cmdExec(std::string const& path) {
  char* argv[] = {const_cast<char*>(path.c_str()), nullptr};
  execv(path.c_str(), argv);
  return fail("exec " + path);
}

int cmdSpawn(std::string const& path) {
  pid_t pid;
  char* argv[] = {const_cast<char*>(path.c_str()), nullptr};
  int rc = posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv, environ);
  if (rc != 0) {
    errno = rc;
    return fail("spawn " + path);
  }
  waitpid(pid, nullptr, 0);
  return 0;
}

int cmdFork() {
  pid_t pid = fork();
  if (pid < 0) {
    return fail("fork");
  }
  if (pid == 0) {
    _exit(0);
  }
  waitpid(pid, nullptr, 0);
  return 0;
}

int cmdNet() {
  int tcp = socket(AF_INET, SOCK_STREAM, 0);
  if (tcp < 0) {
    return fail("tcp socket");
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(tcp, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
      listen(tcp, 4) != 0) {
    return fail("tcp bind/listen");
  }
  socklen_t len = sizeof addr;
  getsockname(tcp, reinterpret_cast<sockaddr*>(&addr), &len);
  int client = socket(AF_INET, SOCK_STREAM, 0);
  if (connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    return fail("tcp connect");
  }
  int accepted = accept(tcp, nullptr, nullptr);
  if (accepted < 0) {
    return fail("tcp accept");
  }
  int udp = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in u{};
  u.sin_family = AF_INET;
  u.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (udp < 0 || bind(udp, reinterpret_cast<sockaddr*>(&u), sizeof u) != 0) {
    return fail("udp bind");
  }
  int udp6 = socket(AF_INET6, SOCK_DGRAM, 0);
  if (udp6 < 0) {
    return fail("udp6 socket");
  }
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) {
    return fail("getifaddrs");
  }
  freeifaddrs(list);
  return 0;
}

int cmdRouteSocket() {
  int fd = socket(PF_ROUTE, SOCK_RAW, 0);
  if (fd < 0) {
    return fail("route socket");
  }
  close(fd);
  return 0;
}

int cmdDns(std::string const& name) {
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  int rc = getaddrinfo(name.c_str(), "80", &hints, &res);
  if (rc != 0) {
    std::fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
    return 1;
  }
  freeaddrinfo(res);
  return 0;
}

int cmdPower() {
  IOPMAssertionID id;
  IOReturn rc = IOPMAssertionCreateWithName(
      kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
      CFSTR("m6e-torrent probe"), &id);
  if (rc != kIOReturnSuccess) {
    std::fprintf(stderr, "power assertion failed: 0x%x\n", rc);
    return 1;
  }
  IOPMAssertionRelease(id);
  return 0;
}

int cmdConfigd() {
  SCDynamicStoreRef store =
      SCDynamicStoreCreate(nullptr, CFSTR("m6e-probe"), nullptr, nullptr);
  if (!store) {
    std::fprintf(stderr, "SCDynamicStoreCreate failed\n");
    return 1;
  }
  CFPropertyListRef value =
      SCDynamicStoreCopyValue(store, CFSTR("State:/Network/Global/IPv4"));
  CFArrayRef names = SCDynamicStoreCopyKeyList(store, CFSTR("State:/Network/.*"));
  bool ok = names != nullptr;
  if (value) {
    CFRelease(value);
  }
  if (names) {
    CFRelease(names);
  }
  CFRelease(store);
  if (!ok) {
    std::fprintf(stderr, "configd is not reachable\n");
  }
  return ok ? 0 : 1;
}

int cmdConfined(std::string const& root, std::string const& rel) {
  m6e::PathComponents parts;
  if (auto err = m6e::splitRelativePath(rel, &parts)) {
    std::fprintf(stderr, "%s\n", err->message.c_str());
    return 1;
  }
  m6e::SafetyError err;
  int fd = m6e::openConfined(root, parts, O_CREAT | O_WRONLY | O_TRUNC, 0644,
                             true, &err);
  if (fd < 0) {
    std::fprintf(stderr, "%s: %s\n", err.code.c_str(), err.message.c_str());
    return 1;
  }
  bool ok = write(fd, "x", 1) == 1;
  close(fd);
  return ok ? 0 : 1;
}

int cmdFinalize(std::string const& root, std::string const& rel) {
  auto report = m6e::finalizeRelative(root, rel);
  std::printf("exec-cleared=%d quarantined=%d method=%s warning=%s\n",
              report.executableCleared, report.quarantined,
              report.quarantineMethod.c_str(), report.warning.c_str());
  return report.executableCleared && report.quarantined ? 0 : 1;
}

int cmdLayoutCheck(std::string const& root, std::string const& rel) {
  auto err = m6e::checkRelativePaths(root, {rel}, {});
  if (err) {
    std::fprintf(stderr, "%s: %s\n", err->code.c_str(), err->message.c_str());
    return 1;
  }
  return 0;
}

// selfsandbox <profile> KEY=VALUE... -- command...
int cmdSelfSandbox(std::vector<std::string> const& a) {
  std::map<std::string, std::string> params;
  size_t i = 2;
  for (; i < a.size() && a[i] != "--"; i++) {
    auto eq = a[i].find('=');
    params[a[i].substr(0, eq)] = a[i].substr(eq + 1);
  }
  std::string error;
  if (!m6e::enterSandbox(a[1], params, &error)) {
    std::fprintf(stderr, "sandbox: %s\n", error.c_str());
    return 2;
  }
  return run(std::vector<std::string>(a.begin() + i + 1, a.end()));
}

int run(std::vector<std::string> const& a) {
  if (a.empty()) {
    return 64;
  }
  auto const& c = a[0];
  if (c == "write" && a.size() == 2) return cmdWrite(a[1]);
  if (c == "read" && a.size() == 2) return cmdRead(a[1]);
  if (c == "list" && a.size() == 2) return cmdList(a[1]);
  if (c == "stat" && a.size() == 2) return cmdStat(a[1]);
  if (c == "exec" && a.size() == 2) return cmdExec(a[1]);
  if (c == "spawn" && a.size() == 2) return cmdSpawn(a[1]);
  if (c == "fork") return cmdFork();
  if (c == "net") return cmdNet();
  if (c == "route") return cmdRouteSocket();
  if (c == "dns" && a.size() == 2) return cmdDns(a[1]);
  if (c == "power") return cmdPower();
  if (c == "configd") return cmdConfigd();
  if (c == "confined" && a.size() == 3) return cmdConfined(a[1], a[2]);
  if (c == "finalize" && a.size() == 3) return cmdFinalize(a[1], a[2]);
  if (c == "check" && a.size() == 3) return cmdLayoutCheck(a[1], a[2]);
  if (c == "selfsandbox" && a.size() >= 3) return cmdSelfSandbox(a);
  std::fprintf(stderr, "unknown command\n");
  return 64;
}

}  // namespace

int main(int argc, char** argv) {
  return run(std::vector<std::string>(argv + 1, argv + argc));
}
