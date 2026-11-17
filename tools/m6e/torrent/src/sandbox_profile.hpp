/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

namespace m6e {

// A copy of sandbox/m6e-torrent.sb, applied by --sandbox; it must stay
// identical to the file (tests/sandbox/run.sh checks).
inline constexpr char kSandboxProfile[] = R"SBPL((version 1)
(deny default)
; Not covered by (deny default): other processes' information (arguments and
; environment through kern.procargs2, the process list). Only its own.
(deny process-info*)
(allow process-info-pidinfo process-info-setcontrol (target self))

; Parameters (all required): HELPER, EXEC_HELPER (YES under sandbox-exec, NO
; when the helper applies the profile to itself), DOWNLOAD_1 to DOWNLOAD_4,
; TORRENTS_DIR, INBOX_DIR. Every path is a realpath. See README.md.

(if (equal? (param "EXEC_HELPER") "YES")
  (allow process-exec (literal (param "HELPER"))))

; Metadata only of the folders on the way to the allowed ones (realpath and
; lstat walk them) and of the system, not of the rest of the disk.
(define (ancestors path)
  (let loop ((i 1) (out (list (literal "/"))))
    (cond ((>= i (string-length path)) out)
          ((char=? (string-ref path i) #\/)
           (loop (+ i 1) (cons (literal (substring path 0 i)) out)))
          (else (loop (+ i 1) out)))))
(for-each
  (lambda (path) (apply allow file-read-metadata (ancestors path)))
  (list (param "HELPER") (param "DOWNLOAD_1") (param "DOWNLOAD_2")
        (param "DOWNLOAD_3") (param "DOWNLOAD_4") (param "TORRENTS_DIR")
        (param "INBOX_DIR")))
(allow file-read-metadata
  (literal "/etc")
  (literal "/tmp")
  (literal "/var")
  (literal "/dev")
  (literal "/private")
  (literal "/private/var")
  (subpath "/usr")
  (subpath "/System")
  (subpath "/private/etc")
  (subpath "/private/var/run"))

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

; IP sockets only: no AF_UNIX socket but the DNS resolver's (no ssh-agent,
; gpg-agent or Docker socket).
(allow network-outbound
  (literal "/private/var/run/mDNSResponder")
  (remote ip))
(allow network-bind network-inbound (local ip))
(allow system-socket
  (require-all (socket-domain AF_SYSTEM) (socket-protocol 2))
  (socket-domain AF_ROUTE))

; What the helper and the system libraries read; not the process table nor
; other processes' arguments and environment (kern.proc.all, kern.procargs2).
(allow sysctl-read
  (sysctl-name-prefix "hw.")
  (sysctl-name-prefix "machdep.cpu.")
  (sysctl-name-prefix "net.")
  (sysctl-name-prefix "sysctl.")
  (sysctl-name-prefix "kern.proc.pid.")
  (sysctl-name-prefix "kern.ipc.")
  (sysctl-name "kern.osproductversion")
  (sysctl-name "kern.osrelease")
  (sysctl-name "kern.ostype")
  (sysctl-name "kern.osversion")
  (sysctl-name "kern.version")
  (sysctl-name "kern.osvariant_status")
  (sysctl-name "kern.secure_kernel")
  (sysctl-name "kern.hv_vmm_present")
  (sysctl-name "kern.maxfiles")
  (sysctl-name "kern.maxfilesperproc"))

(allow mach-lookup
  (global-name "com.apple.SystemConfiguration.configd")
  (global-name "com.apple.SystemConfiguration.DNSConfiguration")
  (global-name "com.apple.mDNSResponder")
  (global-name "com.apple.dnssd.service")
  (global-name "com.apple.PowerManagement.control"))
)SBPL";

}  // namespace m6e
