# Udemy videos that fail: how to find out why

Symptom: on udemy.com some lectures play, others stop with "We tried several
times to play your video but there was an unforeseen error."

This guide finds the cause with a few clicks and no programming. Do the parts
in order and stop at the first one that answers the question. Write down what
you see at each step; the table in part 6 turns your notes into a diagnosis.

Terms used below:

- Tracker Blocker: the shield icon at the left end of the address bar.
- about:config: M6E's list of hidden settings (type it in the address bar).
- Browser Console: a window with M6E's own error messages (Cmd-Shift-J).

## 1. Record what fails and what works

1. Open udemy.com, sign in and open a lecture that fails.
2. Write down the course name, the lecture number and the lecture title.
3. Let it fail. Do not click anything else yet.
4. Open a lecture that works (same course if possible) and write down the same.
5. Note whether the failing lecture fails every time (reload with Cmd-R and try
   twice) or only sometimes.
6. Note whether the failing lectures have something in common: all in one
   section, all newer, all with a high quality setting, and so on.

If the same lecture plays in another browser (Safari or Chrome), say so.
Chrome and Safari can play things M6E cannot (see "Developer notes"); the
lecture still fails in M6E either way.

## 2. THE KEY TEST: switch the Tracker Blocker off for udemy.com

1. Open a failing lecture.
2. Click the shield icon at the left of the address bar. A panel opens that says
   "Tracker Blocker is on".
3. Switch the Tracker Blocker off for this site.
4. Reload the page with Cmd-R, so the lecture starts again from scratch.
5. Press play. Wait a minute. Does it play?

The switch only lasts until you quit M6E, and it only affects udemy.com.

- It plays: go to part 3.
- It still fails: go to part 4.

Switch the Tracker Blocker back on afterwards (same panel), or just quit M6E.

## 3. It plays with the Tracker Blocker off: find which protection does it

The per-site switch turns off several protections at once (tracker blocking,
fingerprinting protection, and the limits on third-party storage). Next, find
the one that matters. Switching the Tracker Blocker off for good is not the
answer; the goal is to learn which single setting Udemy needs.

How to flip a setting:

1. Quit M6E and start it again so udemy.com is protected again (the
   Tracker Blocker switch from part 2 resets on quit). Check by clicking the
   shield: it must say "Tracker Blocker is on".
2. In the address bar type `about:config` and press Enter. Click "Accept the
   Risk and Continue" if it asks.
3. Type the setting name in the search box at the top.
4. For a true/false setting, click the arrow button at the right of the row to
   flip it. For a number, click the pencil button, type the new number, press
   Enter. A bold row means you changed it.
5. Open the failing lecture (reload with Cmd-R) and press play.
6. Put the setting back (the circular arrow button at the right of the row
   restores the default), then do the next row.

Change one setting at a time, always put it back, and write down the result of
each.

| # | Setting | Normal value | Change it to | What it turns off |
|---|---------|--------------|--------------|-------------------|
| 1 | `privacy.fingerprintingProtection` | `true` in strict, `false` in standard | the opposite | Fingerprinting Protection (it hides or alters things a page can read about your Mac) |
| 2 | `privacy.trackingprotection.fingerprinting.enabled` | `true` | `false` | Blocking of known fingerprinting scripts and addresses |
| 3 | `privacy.trackingprotection.enabled` | `true` in strict, `false` in standard | the opposite | Blocking of known tracking scripts and addresses |
| 4 | `network.cookie.cookieBehavior` | `5` | `0` | Cookie partitioning and third-party cookie limits (`0` accepts all cookies; this is the biggest privacy loss, so only for the test) |
| 5 | `network.http.referer.XOriginTrimmingPolicy` | `2` | `0` | Referrer trimming: other sites then see the full address of the page you came from |
| 6 | `dom.security.https_only_mode` | `true` | `false` | HTTPS-Only mode (M6E falls back to Firefox's HTTPS-First) |
| 7 | `network.trr.mode` | `2` | `5` | DNS over HTTPS (Cloudflare for Families): `5` turns it off and uses your normal DNS instead. Needed for the Cloudflare filter test |

Notes:

- "Normal value" is what a new M6E profile starts with. Your profile may
  differ; write down what you see before you change it. If `privacy.fingerprintingProtection`
  or `privacy.trackingprotection.enabled` already has the value in the "Change it
  to" column, skip that row.
- M6E locks some settings so they cannot be changed in about:config
  (telemetry, updates, promotions, and two DNS-over-HTTPS safety settings).
  None of the seven in the table is locked, so each row can be tested
  directly.
- Some of these settings are also managed by Settings, Privacy and Security
  (the Tracker Blocker level and the Privacy Shield) and by the per-site
  switch. The Privacy Shield in Settings is a test too: switching it off
  turns off tracker blocking, fingerprinting protection and the limits above
  for every site, and sets rows 5 and 6 back to Firefox's values. That is a
  one-click version of rows 1 to 6; use it if you do not want to open about:config. Switch
  it back on afterwards.
- Rows 1 to 4 are the same protections the per-site switch in part 2 turns
  off, so if only the per-site switch fixed it, one of rows 1 to 4 is the
  cause. If none of the rows changes anything but the per-site switch did,
  the cause is a combination: try rows 1 and 2 together, then 1 to 4 together.
- If a row makes the lecture play, stop. That setting is the answer: tell the
  developer which row and what value.

## 4. It still fails with the Tracker Blocker off: collect evidence

Do these in order, in the same window that shows the error. Keep notes or take
screenshots.

### 4a. What M6E says about video decryption (about:support)

1. Type `about:support` in the address bar.
2. Scroll to the section "Media".
3. Find "Content Decryption Modules". Look for a line `com.widevine.alpha`.
   Write down everything on that row (the columns for video and audio
   robustness and the capabilities list). Take a screenshot.
4. Scroll to the codec table (video codecs such as H264, VP9, AV1, HEVC, and
   whether decoding is by software or hardware). Take a screenshot.
5. Check the Widevine plug-in: open `about:addons`, click "Plugins" at the
   left (if there is no such entry, skip this step), find "Widevine Content Decryption Module", and write down its version
   number and whether it says "Enabled". (Developer's expectation: 4.10.3112.0.)

There is no `about:media` page in this M6E build (it is not in the source
tree), so use `about:support` for this.

### 4b. The errors and failed requests (Browser Console and Network)

1. Press Cmd-Shift-J to open the Browser Console. Click the trash-can icon to
   clear it.
2. In the lecture window press Cmd-Option-I to open the Web Developer Tools.
   Click the "Network" tab. If it says "Reload to see requests", reload the
   page with Cmd-R.
3. Click the video's play button and wait for the error.
4. In the Browser Console, look for red lines mentioning "EME", "MediaKeys",
   "license", "Widevine" or "shaka". Copy them. The player may print a
   numbered error such as `Shaka Error 6007` or a line like
   `code: 6007`: write the number down. Rough meaning of the number
   (from memory of the Shaka player's list, not checked against the source):
   - 6001: the player asked for a kind of DRM that M6E said it did not support
     (see "Developer notes");
   - 6002: the Widevine module could not be started;
   - 6007: the licence request failed (the licence server did not answer, or
     the request was blocked);
   - 6008: the licence server answered, but with a refusal;
   - 3016: a problem feeding the video data to the player (a decoding
     problem);
   - 1001, 1002, 1003 and other numbers starting 1 or 4: a network request
     failed.
5. In the Network tab, type `license` in the filter box and look for a request
   (often a POST). Click it. Write down: the address (the host name only, for
   example `something.udemy.com`), the status number (200, 403, ...), and
   whether it has a red mark or the word "Blocked". A status 403 means the
   server refused; "Blocked" with a reason like "tracking" or "content
   blocking" means M6E blocked it.
6. Clear the filter and type `is:blocked` or look for red rows; also look for
   rows with a status other than 200 and 206 near the time of the error. Copy
   the host names of any that failed or were blocked, especially CDN hosts
   (names containing `cloudfront`, `akamai`, `udemycdn`, `fastly`, `cdn`).
7. If a failed host cannot be found ("NS_ERROR_UNKNOWN_HOST" or a "could not
   connect" message), that is a DNS problem: go back to part 3 row 7 and test
   `network.trr.mode` = `5`.

### 4c. Does the Widevine module run at all?

Visit another site that uses Widevine (a streaming service's free trailer or a
DRM test page) and check that it plays. Write down
which site and whether it played. If every DRM site fails, the problem is the
module, not Udemy.

## 5. The detailed log (for the developer)

Use this when part 4 does not explain it, or when asked to.

1. Quit M6E completely (Cmd-Q).
2. Open Terminal (Cmd-Space, type Terminal, Enter).
3. Type these two lines (the second one is the script):

   ```
   cd ~/Projects/M6E
   tools/m6e/drm_log.sh
   ```

   Options: add `--net` to also log network requests and DNS (use this if part 4
   pointed at a blocked or missing host). Add `--no-sandbox` only if the
   next step leaves no `child` files; it turns off a security feature of the
   media processes, so quit and start M6E normally afterwards. It can also
   change whether the video plays: if it plays only with `--no-sandbox`, say so.
   A different app location or log folder: `tools/m6e/drm_log.sh /path/to/M6E.app ~/Documents`.
4. M6E opens. Sign in to Udemy, open the failing lecture, press play, wait for
   the error, then quit M6E with Cmd-Q. Do not browse elsewhere: the logs
   grow very quickly.
5. The script lists the log files. They are on your Desktop and named
   `m6e-drm-<date>-<time>.moz_log` (the main process) and
   `m6e-drm-<date>-<time>.child-<number>.moz_log` (one for each helper process:
   web page, media decoding, Widevine). There is no process id in the names;
   the number is a helper number.
6. To share only the end of each file, run the two `tail` commands the script
   prints. Each makes a small text file of the last 500 lines. If the main
   file is very big, that is fine.
7. Read the files before sending them: they can contain page addresses and
   the first part of licence requests. You can delete lines you do not want to
   share.

## 6. What the evidence points to

| What you saw | What it suggests |
|--------------|------------------|
| Plays after the per-site Tracker Blocker switch (part 2) | Tracking or fingerprinting protection, or cookie partitioning, blocks or alters something Udemy needs (hypothesis 1) |
| One row in part 3 fixes it (rows 1 to 4) | That protection is the cause: the developer can exempt Udemy's video and licence hosts or relax that one setting |
| Row 5 fixes it | Referrer trimming: Udemy's licence server or CDN wants a full referrer |
| Row 6 fixes it | HTTPS-Only: a video or licence host answers only in plain http or redirects badly |
| Row 7 fixes it, or a failed host in 4b shows a DNS error | DNS-over-HTTPS with Cloudflare for Families blocks or fails to resolve a CDN or licence host |
| Licence request in 4b is "Blocked" by M6E | Content blocking (hypothesis 1) |
| Licence request is status 403, 401 or 400 and nothing is blocked | The licence server refuses this client: Udemy wants more than M6E's Widevine gives (hypothesis 2), or a login or cookie is missing (cookie partitioning) |
| Number 6001 | The player asked for something M6E's Widevine cannot do: high security level (hardware), HEVC, saved licences, or a device ID (hypothesis 2) |
| Number 6002, or `com.widevine.alpha` missing in about:support, or Widevine not "Enabled" | The Widevine module is missing or not started |
| Number 3016, or the console says "decode" or the codec table lacks the video's codec | Decoder failure (hypothesis 4) |
| `child` log mentions the Widevine process crashing or exiting | The module crashed; the developer will need the log |
| Other DRM sites also fail (4c) | The Widevine module, not Udemy |
| Plays in Chrome or Safari but not in M6E, and 6001 | Udemy asks for a hardware-protected video that only those browsers offer |
| Nothing from the list changes the result | Send everything from parts 1, 4 and 5 |

## 7. Where to send the results

Send them to whoever is working on this with you, usually by pasting into the
conversation: your notes from parts 1 to 3, the screenshots and copied lines
from part 4, and the files from part 5. If you prefer to file it, open an
issue at https://github.com/git-maniam/M6E/issues and attach the same.
Start the message with the answer to part 2 (did it play with the Tracker
Blocker off, yes or no).

---

## Developer notes

Facts checked against the source in this tree. Line numbers are for the current
`origin/main`; names are the stable reference.

### Setting values (checked)

- Strict is the start category for new profiles:
  `browser/app/profile/m6e.js:203` (`browser.contentblocking.initialCategory`).
  The strict feature string includes `tp`, `cookieBehavior5`, `fp`, `fpp`, `rp`
  (`browser/app/profile/firefox.js:2003`). In Standard, `privacy.trackingprotection.enabled`
  (`modules/libpref/init/StaticPrefList.yaml`, `privacy.trackingprotection.enabled`) and
  `privacy.fingerprintingProtection` default to `false`; check the profile's
  actual values before assuming they are on.
- Cookie partitioning is Firefox's default: `network.cookie.cookieBehavior` = 5
  (`firefox.js:1902`); fingerprinter blocking is on
  (`firefox.js:1911`).
- M6E: `dom.security.https_only_mode` true (`m6e.js:209`),
  `network.http.referer.XOriginTrimmingPolicy` 2 (`m6e.js:212`),
  `network.trr.mode` 2 and provider `https://security.cloudflare-dns.com/dns-query`
  (`m6e.js:85`, `m6e.js:88`). None is `locked`. The locked TRR prefs are
  `hard_fail_on_extended_error` and `allow-rfc1918` only (`m6e.js:90`, `m6e.js:92`).
- The per-site switch is Firefox's ETP allow list: `ContentBlockingAllowList::Check`
  (`toolkit/components/antitracking/ContentBlockingAllowList.cpp`) is consulted for
  tracking protection, storage access (`StorageAccess.cpp`) and
  fingerprinting protection
  (`nsContentUtils::ETPSaysShouldNotResistFingerprinting`). It does not change
  `XOriginTrimmingPolicy` or the TRR mode; those are global
  (`browser/components/protections/M6EPrivacyShield.sys.mjs`, `COMPATIBILITY_VALUES`),
  and switch only with the Privacy Shield in Settings. Whether it also lifts cookie
  partitioning was not verified.
- The Tracker Blocker panel text (on/off, "until M6E quits") is in
  `browser/locales/en-US/browser/browser.ftl` (`trustpanel-etp-*`).

### Widevine configuration (`dom/media/eme/KeySystemConfig.cpp`)

`CreateWivineL3KeySystemConfigs` (the only Widevine config on this build; the
Windows-only `MOZ_WMF_CDM` branches for L1 and PlayReady are not compiled in
on macOS):

- Init data types: `cenc`, `keyids`, `webm`.
- Session types: `temporary` only. No `persistent-license`.
- `persistentState`: optional. `distinctiveIdentifier`: not allowed.
- Robustness: audio `SW_SECURE_CRYPTO`; video `SW_SECURE_CRYPTO`, `SW_SECURE_DECODE`.
  (Empty robustness is accepted.) Everything else is a mismatch, including all
  `HW_SECURE_*` strings (`MediaKeySystemAccess.cpp`, "unsupported robustness
  string").
- MP4: decrypt-and-decode H.264, VP9, AV1 inside the CDM; decrypt only AAC,
  FLAC, Opus. WebM: decrypt-and-decode VP8, VP9, AV1; decrypt only Vorbis,
  Opus. Both `cenc` and `cbcs` schemes (no scheme restriction is set).
- HEVC is recognized as a video codec (`EME_CODEC_HEVC`,
  `MediaKeySystemAccess.cpp` `GetCodecType`) but is not in the Widevine list,
  so a capability with `hvc1`/`hev1`/`hevc` is dropped ("codec unsupported by
  CDM requested").

What this rejects for a typical Udemy-style (Shaka or HLS/DASH with Widevine)
`requestMediaKeySystemAccess` call:

- A video capability whose robustness is `HW_SECURE_ALL`, `HW_SECURE_DECODE` or
  `HW_SECURE_CRYPTO` (the usual "high tier" ask). If the player lists a
  `SW_SECURE_*` or empty-robustness entry as a fallback, the call succeeds
  with that one; if it offers only `HW_SECURE_*`, the call rejects (Shaka:
  `REQUESTED_KEY_SYSTEM_CONFIG_UNAVAILABLE`).
- `distinctiveIdentifier: "required"` (rejected; the CDM says not allowed).
- `sessionTypes: ["persistent-license"]` (offline playback; rejected).
  `persistentState: "required"` alone is accepted.
- HEVC (`hvc1`, `hev1`) video.
- Audio capability with `SW_SECURE_DECODE` (valid for video only here).
- Accepted: `initDataTypes: ["cenc"]`, `temporary`, H.264/VP9/AV1 MP4 with empty
  or `SW_SECURE_CRYPTO` robustness, AAC/Opus audio with empty or
  `SW_SECURE_CRYPTO`, `cenc` or `cbcs`.

This is derived from the code only; what Udemy's player actually requests
was not observed. The `EME` log in part 5 prints each rejected capability
with the reason ("MediaKeySystemMediaCapability ... unsupported;
unsupported robustness string"), so it settles this in one run.

If the licence server also checks the CDM's reported security level, a
software-only L3 module will be refused regardless of what the request asked
for; that shows as a licence request answered with an error (Shaka 6008), not
6001.

### Widevine version (`toolkit/content/gmp-sources/widevinecdm.json`)

- The file lists one version, `4.10.3112.0` (`"version"` and the Darwin
  aarch64 URL), the same as the installed one, so there is no newer entry in
  the file. The installed copy was not checked in the profile from this
  environment.
- `GMPInstallManager.adjustForChromiumUpdateService` only accepts a Chromium
  update whose version matches the file's (`gmpAddon.version !== version` is
  skipped) unless `media.gmp-widevinecdm.force-chromium-update` is true. With
  the default, M6E therefore cannot move past the version pinned in the
  file; a newer CDM needs a newer file. A cheap test: set
  `media.gmp-widevinecdm.force-chromium-update` to true, restart, and let
  M6E fetch whatever Google currently serves. Untested here.
- Whether `4.10.3112.0` is current upstream could not be checked offline.

### Shaka error numbers

The numbers in part 4b are from memory of the Shaka Player error list
(6001 `REQUESTED_KEY_SYSTEM_CONFIG_UNAVAILABLE`, 6002 `FAILED_TO_CREATE_CDM`,
6007 `LICENSE_REQUEST_FAILED`, 6008 `LICENSE_RESPONSE_REJECTED`; 3016 is a
`MEDIA_SOURCE_OPERATION_*` error, exact name not recalled). They were not checked against the
Shaka source, and it was not verified that Udemy uses Shaka. Treat them as a
pointer, not a proof.

### Logging (`tools/m6e/drm_log.sh`)

- The binary is `Contents/MacOS/m6e`; the script runs it directly, so the
  environment reaches the main process.
- Child processes inherit `MOZ_LOG`; `MOZ_LOG_FILE` is rewritten per child by
  `BaseProcessLauncher::MapChildLogging` (`ipc/glue/GeckoChildProcessHost.cpp`) to
  `<name>.child-<n>.moz_log` (n is the child id; the process id is added
  only when the name contains `%PID`, which the script does not use).
- The macOS sandbox profiles for content, RDD and GMP processes
  (`security/sandbox/mac/SandboxPolicy*.h`) deny file writes except a
  debug-only directory (`DEBUG_WRITE_DIR`, `#ifdef DEBUG` in
  `dom/ipc/ContentParent.cpp`). Whether a child can still write its log on a
  release build was not verified; hence the `--no-sandbox` option, which sets
  `MOZ_DISABLE_CONTENT_SANDBOX`, `MOZ_DISABLE_GMP_SANDBOX` and
  `MOZ_DISABLE_RDD_SANDBOX` (all three exist in the source).
- Log modules used: `EME` and `EMEV` (`dom/media/eme/EMEUtils.cpp`), `GMP`
  (`GMPService.cpp`), `MediaDecoder`, `MediaSource`, `PlatformDecoderModule`.
  `MediaKeys` from the original list is not a log module name in this tree;
  `MediaKeys.cpp` logs with `EME_LOG`, so it falls under `EME`.
