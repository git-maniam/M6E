# m6e-torrent confinement (P8.1, spec 8.4 and 8.5)

Two layers. The code layer is the primary one and always on; the sandbox is a second wall that needs the download folders to be known when the helper starts.

## Layer 1: path confinement in code (`src/confine.{hpp,cpp}`, `src/safety.{hpp,cpp}`)
- `checkInfoDict` (no file system) rejects traversal, absolute, empty, NUL, `:`, `\`, control-character, over-long (255 bytes a name, 1000 a path, 64 levels) and non-UTF-8 names, BEP 47 symlinks, padding entries without a `.pad/<digits>` name, and paths that collide by case or Unicode normalisation (NFD plus case fold, so `é` and `e` + accent, `Readme` and `README`). It checks the plain and the `.utf-8` name variants.
- `checkLayout` / `checkRelativePaths` (before payload transfer starts) resolve the save folder with `realpath` (it must exist, be a folder and not be `/`), then walk what already exists below it with `lstat`, never following a link: a symlink at any level, a file where a folder is needed, a folder or special file where a file is needed, a file with other hard links, and an existing name that differs only by case or normalisation (it would alias that file on APFS) are all `unsafe-path`.
- `prepareDirectories` and `openConfined` do the writes with `openat` and `O_NOFOLLOW` one component at a time from a descriptor of the root, so a link planted after the check still fails (`unsafe-path`). A hostile torrent cannot create a link (BEP 47 is rejected), so the remaining threat is a link that already exists in the chosen folder.
- `ensureProfileTorrentsDir` creates `<profile>/torrents` (0700) and rejects a link there.
- libtorrent opens the payload files itself, with plain `open`. The helper must therefore call `checkLayout` (or `checkRelativePaths` with the engine's own `file_path` strings, `pad` flags included) and `prepareDirectories` before `confirm` starts the transfer. Between that check and libtorrent's `open` only a local attacker with write access to the save folder could plant a link; the sandbox covers that case for everything outside the allowed folders.
- Link against `CoreFoundation` (already in the libtorrent link line). `foldName` uses `CFStringNormalize` and `CFStringFold`.

## Finished files: execute bits and quarantine
`finalizeRelative(rootReal, "name/dir/file")` opens the file no-follow, checks it is a regular file, clears all execute bits (`fchmod`), and sets `com.apple.quarantine`:
1. First the mechanism Gecko uses for downloads (`CFURLSetResourcePropertyForKey` with `kCFURLQuarantinePropertiesKey`, type `LSQuarantineTypeOtherDownload`, see `CocoaFileUtils.mm`). On macOS 26 it writes `0281;<hex seconds>;;<UUID>`. It works inside the sandbox profile below.
2. If that leaves no attribute (checked with `fgetxattr` on the same descriptor), it writes `0081;<hex seconds>;M6E;<UUID>` directly with `fsetxattr`. `0081` is the flags value other browsers write for a download; this form (`flags;time;agent;UUID`) was not run through Gatekeeper here, only through the format test.
3. If both fail (a volume without extended attributes, such as FAT or some network shares), the call does not fail the download: `FinalizeReport.warning` says what was not done, and the helper should put it in the `completed` event or the log so the panel can show "could not quarantine".

## Layer 2: macOS sandbox (`m6e-torrent.sb`)
Deny-default SBPL. Allowed: network (`network*`, `system-socket`), `sysctl-read`, `file-read-metadata` everywhere, read of the helper binary, `/usr/lib`, `/System/Library`, a few `/dev` and `/etc` files; read and write under `DOWNLOAD_1` to `DOWNLOAD_4` and `TORRENTS_DIR` (`<profile>/torrents`); read-only `INBOX_DIR`; `mach-lookup` of configd, the DNS configuration service, mDNSResponder and `com.apple.PowerManagement.control`. Process execution: none, except that under `sandbox-exec` the helper binary itself (`EXEC_HELPER=YES`).

Parameters, all required, all `realpath`s (`/tmp` is `/private/tmp`): `HELPER`, `EXEC_HELPER`, `DOWNLOAD_1..4` (use a nonexistent path for unused slots), `TORRENTS_DIR`, `INBOX_DIR`.

Two ways to apply it:
- `/usr/bin/sandbox-exec -f m6e-torrent.sb -D HELPER=... -D EXEC_HELPER=YES ... m6e-torrent ...`
- In the helper at start: `m6e::enterSandbox(profilePath, params, &error)` with `EXEC_HELPER=NO`. After it, nothing can be executed at all, not even the helper binary again. Preferred: no `sandbox-exec` wrapper process, and no exec rule.

The power rule is needed for the idle-sleep assertion (spec 8.4) and the configd rule for libtorrent's interface and gateway lookups; the test proves each: without its `mach-lookup` line `power` and `configd` fail. DNS for `localhost` and a public name worked without the mDNSResponder line on macOS 26; it stays in because older systems and SOCKS/VPN setups may need it.

### Proof (`tests/confine/run.sh`)
`tests/confine/sandbox_proof.sh` runs the real code (`sandbox_probe`) under both ways of applying the profile and checks, with 41 cases: it writes inside each download folder and the profile folder (including through `openConfined`, creating folders), reads the inbox, finalizes a file (execute bit gone, quarantine set), uses loopback TCP and UDP, lists interfaces, opens a route socket, resolves `localhost`, takes a power assertion and reads configd; and it is denied writing outside, through a link out of the folder, into the inbox, in `/tmp` and in `$HOME`; reading outside, listing `$HOME` and `/Users`, reading a file in `$HOME`, `/etc/passwd`, the Keychains folder; `exec`, `posix_spawn` of `/bin/echo` and `/bin/sh`; and `fork`.

### Gaps (open, for the lead)
1. **Download folders are fixed at start.** `confirm` carries `savePath` at run time, but a sandbox cannot be widened afterwards. The profile takes up to four folders. Options: (a) the service starts the helper with the user's download folder(s) (`--download-dir` arguments to add, or read from the preferences) and restarts the idle helper when the user picks a new folder; (b) run unsandboxed when the folders cannot be listed. Until the lead chooses, the helper's code layer is the only wall for a folder picked after start. Not decided here; `PROTOCOL.md` is untouched.
2. **Input files.** `add` takes any absolute `.torrent` path, and `setConfig` an IP-filter path. A deny-default profile cannot read arbitrary paths. The service should stage them into `INBOX_DIR` (for example `<profile>/torrents/inbox/`) and send that path.
3. **`file-read-metadata` is global**, so a compromised helper can learn that a path exists and its size and times (needed for `realpath` and `lstat`), never its content.
4. **Network is not narrowed**: the helper may reach any host and port. Interface binding and the proxy are the helper's job (8.5).
5. **Deprecation.** `sandbox-exec` and `sandbox_init_with_parameters` are deprecated by Apple but ship in every macOS from 11 to 26 and are what Chromium and Gecko use. Tested only on macOS 26 (Darwin 25.6) on arm64; not on macOS 11.
6. **Hard links**: an existing file with other hard links is refused, but a link made by the user between the check and the open is not caught (local attacker case).
7. **Debug output**: macOS gives no sandbox denial log to a normal user here (`log show` is denied), so a rule that is missing on another macOS shows up as the helper aborting at start (exit 134) or `Operation not permitted`. Run the proof script on each target macOS.
