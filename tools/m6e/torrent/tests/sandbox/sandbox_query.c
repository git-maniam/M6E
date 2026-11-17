/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Asks the kernel what the sandbox of a running process allows.
 *   sandbox_query <pid> sandboxed
 *   sandbox_query <pid> <operation> [path or service name]
 * Prints allow or deny; exit status 0 allow, 1 deny, 2 error. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

extern int sandbox_check(pid_t pid, const char* operation, int type, ...);

enum {
  kFilterNone = 0,
  kFilterPath = 1,
  kFilterGlobalName = 2,
  kCheckNoReport = 0x40000000,
};

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: sandbox_query <pid> sandboxed|<operation> [arg]\n");
    return 2;
  }
  pid_t pid = (pid_t)atoi(argv[1]);
  const char* op = argv[2];
  int rc;
  if (strcmp(op, "sandboxed") == 0) {
    rc = sandbox_check(pid, NULL, kFilterNone | kCheckNoReport);
    printf("%s\n", rc > 0 ? "sandboxed" : rc == 0 ? "not-sandboxed" : "error");
    return rc > 0 ? 0 : rc == 0 ? 1 : 2;
  }
  if (argc < 4) {
    rc = sandbox_check(pid, op, kFilterNone | kCheckNoReport);
  } else if (strcmp(op, "mach-lookup") == 0) {
    rc = sandbox_check(pid, op, kFilterGlobalName | kCheckNoReport, argv[3]);
  } else {
    rc = sandbox_check(pid, op, kFilterPath | kCheckNoReport, argv[3]);
  }
  printf("%s\n", rc == 0 ? "allow" : rc > 0 ? "deny" : "error");
  return rc == 0 ? 0 : rc > 0 ? 1 : 2;
}
