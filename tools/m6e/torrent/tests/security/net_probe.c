/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Applies the helper's sandbox profile to itself, then tries one thing.
 *   net_probe <profile.sb> KEY=VALUE... -- unix <path> | tcp <port> | udp |
 *             sysctl <name> | sysctl-proc | sysctl-procargs | stat <path>
 * Exit status 0 when it worked, 1 when it was refused (EPERM or EACCES),
 * 2 for anything else. Loopback only. */

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>

extern int sandbox_init_with_parameters(const char* profile, uint64_t flags,
                                        const char* const parameters[],
                                        char** errorbuf);

static int result(int rc, const char* what) {
  if (rc == 0) {
    printf("%s: ok\n", what);
    return 0;
  }
  int e = errno;
  printf("%s: %s\n", what, strerror(e));
  return e == EPERM || e == EACCES ? 1 : 2;
}

static char* readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  static char buf[1 << 16];
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: net_probe <profile> KEY=VALUE... -- <command>\n");
    return 2;
  }
  const char* params[64];
  int count = 0;
  int i = 2;
  for (; i < argc && strcmp(argv[i], "--") != 0 && count < 60; i++) {
    char* eq = strchr(argv[i], '=');
    if (!eq) {
      return 2;
    }
    *eq = 0;
    params[count++] = argv[i];
    params[count++] = eq + 1;
  }
  params[count] = NULL;
  if (i + 1 >= argc) {
    return 2;
  }
  const char* cmd = argv[i + 1];
  const char* arg = i + 2 < argc ? argv[i + 2] : "";
  char* profile = readFile(argv[1]);
  char* error = NULL;
  if (!profile || sandbox_init_with_parameters(profile, 0, params, &error) != 0) {
    fprintf(stderr, "cannot enter the sandbox: %s\n", error ? error : "?");
    return 2;
  }
  if (strcmp(cmd, "unix") == 0) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sun = {0};
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, arg, sizeof sun.sun_path - 1);
    return result(connect(fd, (struct sockaddr*)&sun, sizeof sun), "unix");
  }
  if (strcmp(cmd, "tcp") == 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)atoi(arg));
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return result(connect(fd, (struct sockaddr*)&sin, sizeof sin), "tcp");
  }
  if (strcmp(cmd, "udp") == 0) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr*)&sin, sizeof sin) != 0) {
      return result(-1, "udp bind");
    }
    sin.sin_port = htons(9);
    return result(sendto(fd, "x", 1, 0, (struct sockaddr*)&sin, sizeof sin) == 1 ? 0 : -1,
                  "udp send");
  }
  static char buf[1 << 20];
  size_t len = sizeof buf;
  if (strcmp(cmd, "sysctl") == 0) {
    return result(sysctlbyname(arg, buf, &len, NULL, 0), arg);
  }
  if (strcmp(cmd, "sysctl-proc") == 0) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    return result(sysctl(mib, 3, buf, &len, NULL, 0), "kern.proc.all");
  }
  if (strcmp(cmd, "sysctl-procargs") == 0) {
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, (int)getppid()};
    return result(sysctl(mib, 3, buf, &len, NULL, 0), "kern.procargs2 of the parent");
  }
  if (strcmp(cmd, "stat") == 0) {
    struct stat st;
    return result(stat(arg, &st), "stat");
  }
  fprintf(stderr, "unknown command %s\n", cmd);
  return 2;
}
