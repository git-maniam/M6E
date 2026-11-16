/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

namespace m6e {

// A copy of sandbox/m6e-torrent.sb, applied by --sandbox; it must stay
// identical to the file (tests/sandbox/run.sh checks).
inline constexpr char kSandboxProfile[] = R"SBPL((version 1)
(deny default)

; Parameters (all required): HELPER, EXEC_HELPER (YES under sandbox-exec, NO
; when the helper applies the profile to itself), DOWNLOAD_1 to DOWNLOAD_4,
; TORRENTS_DIR, INBOX_DIR. Every path is a realpath. See README.md.

(if (equal? (param "EXEC_HELPER") "YES")
  (allow process-exec (literal (param "HELPER"))))

(allow file-read-metadata)

(allow file-read*
  (literal "/")
  (literal (param "HELPER"))
  (subpath "/usr/lib")
  (subpath "/System/Library")
  (literal "/dev/null")
  (literal "/dev/urandom")
  (literal "/dev/random")
  (literal "/private/etc/hosts")
  (literal "/private/etc/resolv.conf")
  (literal "/private/etc/services"))

(allow file-write-data (literal "/dev/null"))

(allow file-read* file-write*
  (subpath (param "DOWNLOAD_1"))
  (subpath (param "DOWNLOAD_2"))
  (subpath (param "DOWNLOAD_3"))
  (subpath (param "DOWNLOAD_4"))
  (subpath (param "TORRENTS_DIR")))

(allow file-read* (subpath (param "INBOX_DIR")))
(deny file-write* (subpath (param "INBOX_DIR")))

(allow network*)
(allow system-socket)

(allow sysctl-read)

(allow mach-lookup
  (global-name "com.apple.SystemConfiguration.configd")
  (global-name "com.apple.SystemConfiguration.DNSConfiguration")
  (global-name "com.apple.mDNSResponder")
  (global-name "com.apple.dnssd.service")
  (global-name "com.apple.PowerManagement.control"))
)SBPL";

}  // namespace m6e
