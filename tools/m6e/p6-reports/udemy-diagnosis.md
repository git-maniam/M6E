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

If the same lecture plays in Chrome, say so; it helps (see part 6). If it plays
in Safari, say so too, but that tells us little: Safari uses a different DRM
system (FairPlay), not Widevine.

Also write down which M6E you have: menu M6E, About M6E, and read the version
(`161.0a1` or `161.0a2`). Part 3 has settings that differ between them.

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

The per-site switch turns off several protections at once for that site:
tracker blocking, fingerprinting protection (including Maximum protection),
the limits on third-party storage, and HTTPS-Only mode (it adds an HTTPS-Only
exception for the site). Next, find the one that matters. Switching the Tracker Blocker off for good is not the
answer; the goal is to learn which single setting Udemy needs.

How to flip a setting:

1. Quit M6E and start it again so udemy.com is protected again (the
   Tracker Blocker switch from part 2 resets on quit). Check by clicking the
   shield: it must say "Tracker Blocker is on".
2. In the address bar type `about:config` and press Enter. Click "Accept the
   Risk and Continue" if it asks.
3. Type the setting name in the search box at the top.
4. Write down the value the row has now (its original value).
5. For a true/false setting, click the toggle button at the right of the row
   to flip it. For a number, click the pencil button, type the new number,
   press Enter. A bold row means you changed it.
6. Open the failing lecture (reload with Cmd-R) and press play.
7. Put the setting back to the original value you wrote down: toggle it again,
   or type the number again. Do NOT use the circular "reset" arrow on rows 1
   and 3: it goes to Firefox's built-in default, which for those two rows is
   the opposite of the value Strict mode sets, so it would leave the setting
   wrong. Then do the next row.

Change one setting at a time, always put it back, and write down the result of
each.

Which rows apply depends on your build (the version you wrote down in part 1):

- `161.0a1` (the build you have today): Strict mode has first-party
  Fingerprinting Protection on, so all rows apply.
- `161.0a2` and later: Standard and Strict mode have no first-party
  Fingerprinting Protection. Rows 1 and 1b should already be `false`, so there
  is nothing to test in them; start at row 1c if you use Maximum protection,
  otherwise at row 2.

| # | Setting | Original value on `161.0a1` | Original value on `161.0a2`+ | Change it to | What it turns off |
|---|---------|-----------------------------|------------------------------|--------------|-------------------|
| 1 | `privacy.fingerprintingProtection` | `true` in Strict, `false` in Standard | `false` | `false` (skip if it is already `false`) | Fingerprinting Protection: it hides or alters things a page can read about your Mac (canvas, graphics, screen) |
| 1b | `privacy.baselineFingerprintingProtection` | `true` | `false` | `false` (skip if it is already `false`; on `161.0a1` in Strict, also have row 1 at `false`, because row 1 takes precedence) | The smaller, always-on set of the same protections |
| 1c | `privacy.resistFingerprinting` | `false`; `true` only if Maximum protection is selected | same | `false` (skip if it is already `false`) | Maximum protection: the strongest fingerprinting resistance. The per-site switch turns it off for the site too. If it is on because of Maximum, switch the Privacy Shield in Settings from Maximum to the normal level instead of editing it here, and reselect Maximum afterwards |
| 2 | `privacy.trackingprotection.fingerprinting.enabled` | `true` | `true` | `false` | Blocking of known third-party fingerprinting scripts and addresses |
| 3 | `privacy.trackingprotection.enabled` | `true` in Strict, `false` in Standard | same | `false` (skip if it is already `false`) | Blocking of known tracking scripts and addresses |
| 4 | `network.cookie.cookieBehavior` | `5` | `5` | `0` | Cookie partitioning and third-party cookie limits (`0` accepts all cookies; this is the biggest privacy loss, so only for the test) |
| 5 | `network.http.referer.XOriginTrimmingPolicy` | `2` | `2` | `0` | Referrer trimming: other sites then see the full address of the page you came from |
| 6 | `dom.security.https_only_mode` | `true` | `true` | `false` | HTTPS-Only mode (M6E falls back to Firefox's HTTPS-First) |
| 7 | `network.trr.mode` | `2` | `2` | `5` | DNS over HTTPS (Cloudflare for Families): `5` turns it off and uses your normal DNS instead. Needed for the Cloudflare filter test |

The "original value" columns are what a new profile starts with. Your profile
may differ, which is why step 4 says to write down what you see: put back what
you saw, not what the table says. The `161.0a2`+ column describes a change that
was still being finished when this was written; if a row shows something
else, trust the row and tell the developer.

Notes:

- Changing rows 1, 1b, 2 or 3 in about:config can switch your Enhanced
  Tracking Protection category from Strict to Custom. When all the tests are
  done, restore the values (step 7), then open Settings, Privacy & Security,
  Enhanced Tracking Protection, and check that "Strict" is selected and nothing
  says "Custom". If it says Custom, click Strict. A profile left in Custom is
  not put back to Strict by M6E updates, so do not skip this.
- M6E locks some settings so they cannot be changed in about:config
  (telemetry, updates, promotions, and two DNS-over-HTTPS safety settings).
  None of the seven in the table is locked, so each row can be tested
  directly.
- Some of these settings are also managed by Settings, Privacy and Security
  (the Tracker Blocker level and the Privacy Shield) and by the per-site
  switch. The Privacy Shield in Settings is a test too: switching it off
  turns off tracker blocking, fingerprinting protection and the limits above
  for every site, and sets rows 5 and 6 back to Firefox's values. That is a
  one-click version of rows 1 to 6 (it does not touch the Strict category);
  use it if you do not want to open about:config. Switch it back on
  afterwards.
- Rows 1 to 4 and row 6 are the protections the per-site switch in part 2
  turns off, so if only the per-site switch fixed it, one of rows 1 to 4 or 6
  is the cause (rows 5 and 7 are not part of the per-site switch). If no single
  row changes anything but the per-site switch did, the cause is a
  combination: try rows 1, 1b and 2 together, then 1 to 4 together.
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
   `code: 6007`: write the number down, and copy the whole line, including
   any list of numbers after it (called `data`). What the numbers mean is
   from memory of the Shaka player's error list and not checked: look the
   number up at
   https://shaka-player-demo.appspot.com/docs/api/shaka.util.Error.html
   before relying on any of this.
   - Numbers starting with 1 (1xxx) are believed to be network errors.
   - Numbers starting with 6 (6xxx) are believed to be DRM errors: asking for
     a kind of DRM, starting the module, or the licence request or its answer.
   - 3016 is believed to be `VIDEO_ERROR`; the first number in its `data` list
     is then the browser's own media error code, so copy it.
5. In the Network tab, type `license` in the filter box and look for a request
   (often a POST). Click it. Write down: the address (the host name only, for
   example `something.udemy.com`), the status number (200, 403, ...), and
   whether it has a red mark or the word "Blocked". A status 403 means the
   server refused; "Blocked" with a reason like "tracking" or "content
   blocking" means M6E blocked it.
6. Clear the filter. Click the "Status" column heading to sort by it and look
   for rows marked "Blocked", and for rows with a status other than 200 and
   206 near the time of the error. Copy
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
3. Get the script. It is in the project at `tools/m6e/drm_log.sh`. If you have
   the project folder at `~/Projects/M6E`, type `cd ~/Projects/M6E` and then
   `sh tools/m6e/drm_log.sh`. Otherwise download it and run it. It is not on
   the `main` branch yet, so use the branch address until it is merged:

   ```
   cd ~/Downloads
   curl -O https://raw.githubusercontent.com/git-maniam/M6E/m6e-fix/udemy-diagnosis/tools/m6e/drm_log.sh
   sh drm_log.sh
   ```

   (Once merged, replace `m6e-fix/udemy-diagnosis` with `main` in the
   address.)

   Options go after `sh drm_log.sh`: `--net` also logs network requests and
   DNS (use it if part 4 pointed at a blocked or missing host; the logs get a
   lot bigger), and `--max-mb 50` limits each log file to about 50 megabytes
   (the default is 100). Add `--no-sandbox` only if the
   next step leaves no `child` files; it turns off a security feature of the
   media processes, so quit and start M6E normally afterwards. It can also
   change whether the video plays: if it plays only with `--no-sandbox`, say so.
   A different app location or log folder: `sh drm_log.sh /path/to/M6E.app ~/Documents`.
4. M6E opens. Sign in to Udemy, open the failing lecture, press play, wait for
   the error, then quit M6E with Cmd-Q. Do not browse elsewhere: the logs
   grow very quickly and can take up hundreds of megabytes of disk space.
   Delete the `m6e-drm-*` files from the Desktop when you are done.
5. The script lists the log files. They are on your Desktop and named
   `m6e-drm-<date>-<time>.moz_log` (the main process) and
   `m6e-drm-<date>-<time>.child-<number>.moz_log` (one for each helper process:
   web page, media decoding, Widevine). There is no process id in the names;
   the number is a helper number.
6. The script prints one line that starts with `for f in`. Paste it into
   Terminal and press Enter. It makes a small `-tail.txt` file next to each
   log, with the last 500 lines. Send those files (not the huge logs), unless
   asked for more.
7. Read the files before sending them: they can contain page addresses,
   cookies and the first part of licence requests. Delete lines you do not
   want to share. Send them privately (to the developer directly), never in a
   public issue or forum post.

## 6. What the evidence points to

| What you saw | What it suggests |
|--------------|------------------|
| Plays after the per-site Tracker Blocker switch (part 2) | Tracking or fingerprinting protection, or cookie partitioning, blocks or alters something Udemy needs |
| One row in part 3 fixes it (rows 1 to 4) | That protection is the cause: the developer can exempt Udemy's video and licence hosts or relax that one setting |
| Row 5 fixes it | Referrer trimming: Udemy's licence server or CDN wants a full referrer |
| Row 6 fixes it | HTTPS-Only: a video or licence host answers only in plain http or redirects badly |
| Row 7 fixes it, or a failed host in 4b shows a DNS error | DNS-over-HTTPS with Cloudflare for Families blocks or fails to resolve a CDN or licence host |
| Licence request in 4b is "Blocked" by M6E | Content blocking |
| Licence request is status 403, 401 or 400 and nothing is blocked | The licence server refuses this client: Udemy wants more than M6E's Widevine gives or a login or cookie is missing (cookie partitioning) |
| A DRM number (6xxx) whose name in Shaka's list is about the requested configuration or "no key system" | The player asked for something M6E's Widevine cannot do: high security level (hardware), HEVC, saved licences, or a device ID |
| A DRM number (6xxx) whose name is about creating the module, or `com.widevine.alpha` missing in about:support, or Widevine not "Enabled" | The Widevine module is missing or not started |
| 3016 with a media error code in its `data`, or the console says "decode" or the codec table lacks the video's codec | Decoder failure |
| `child` log mentions the Widevine process crashing or exiting | The module crashed; the developer will need the log |
| Other DRM sites also fail (4c) | The Widevine module, not Udemy |
| Plays in Chrome but fails in M6E with a DRM (6xxx) error about the configuration or the licence | Udemy's player or licence server needs something M6E's Widevine does not offer: saved (persistent) licences, HEVC, hardware-protected (`HW_SECURE_*`) robustness, or a CDM feature that depends on the CDM host |
| Plays in Safari but not in M6E | Tells us nothing about Widevine: Safari uses FairPlay, a different DRM system |
| Nothing from the list changes the result | Send everything from parts 1, 4 and 5 |

## 7. Where to send the results

Send them to the developer privately (a direct message or email, not a public
issue, forum or chat): your notes from parts 1 to 3, the screenshots and
copied lines from part 4, and the `-tail.txt` files from part 5. Logs and
network details can contain cookies, session tokens and the addresses of the
pages you visited, so do not post them publicly. Start the message with the
answer to part 2 (did it play with the Tracker Blocker off, yes or no).

---

## Developer notes

Facts checked against the source in this tree. Line numbers are for the current
`origin/main`; names are the stable reference.

### Setting values (checked)

- Strict is the start category for new profiles:
  `browser.contentblocking.initialCategory` in `browser/app/profile/m6e.js`.
- Build `161.0a1` (this tree): the Strict feature string
  (`browser.contentblocking.features.strict` in `browser/app/profile/firefox.js`)
  includes `tp`, `cookieBehavior5`, `fp`, `fpp`, `rp`, so Strict sets
  `privacy.trackingprotection.enabled` and `privacy.fingerprintingProtection`
  to true. Their built-in defaults are false (`StaticPrefList.yaml`), so the
  about:config reset arrow gives the opposite of the Strict value.
  `privacy.baselineFingerprintingProtection` defaults to true.
- Build `161.0a2`+ (branch `m6e-fix/fp-standard-mode`, not merged when this
  was written; read from `git diff origin/main origin/m6e-fix/fp-standard-mode`
  on `m6e.js`): Strict becomes `-fpp,-fppPrivate` and
  `privacy.baselineFingerprintingProtection` false. That diff of `m6e.js` does
  not set `privacy.fingerprintingProtection.pbmode`; the guide relies on the
  coordinator's statement that it is false. Maximum protection stays
  `privacy.resistFingerprinting`, set by `M6EPrivacyShield.sys.mjs` (`apply`).
- A profile whose prefs no longer match the category is shown as Custom, and
  an update does not reset it (as told by the reviewer; the code was not
  checked).
- Cookie partitioning is Firefox's default: `network.cookie.cookieBehavior` = 5
  (`firefox.js`); third-party fingerprinter blocking is on
  (`privacy.trackingprotection.fingerprinting.enabled`, `firefox.js`).
- M6E (`m6e.js`): `dom.security.https_only_mode` true,
  `network.http.referer.XOriginTrimmingPolicy` 2, `network.trr.mode` 2 and
  provider `https://security.cloudflare-dns.com/dns-query`. None is `locked`.
  The locked TRR prefs are `hard_fail_on_extended_error` and `allow-rfc1918`
  only.
- The per-site switch with the Shield on (`M6EPrivacyShield.sys.mjs`,
  `setSiteProtected`) adds the ETP allow-list entry
  (`ContentBlockingAllowList.add`) and a session `https-only-load-insecure`
  permission, which is an HTTPS-Only exception. The allow list is consulted by
  `ContentBlockingAllowList::Check`
  (`toolkit/components/antitracking/ContentBlockingAllowList.cpp`) for
  tracking protection, storage access (`StorageAccess.cpp`) and fingerprinting
  protection including resistFingerprinting
  (`nsContentUtils::ETPSaysShouldNotResistFingerprinting`; its M6E comment says
  it covers RFP too). It does not change `XOriginTrimmingPolicy` or the TRR
  mode; those are global (`COMPATIBILITY_VALUES`) and switch only with the
  Privacy Shield in Settings. Whether it also lifts cookie partitioning was
  not verified.
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
  with that one; if it offers only `HW_SECURE_*`, the call rejects (Shaka, from memory:
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
for; that shows as a licence request answered with an error (a licence
response error), not a configuration error.

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
  M6E fetch whatever Google currently serves. Untested here. Caveat: the
  forced branch deletes `size`, `hashValue` and `hashFunction` from the add-on
  (`adjustForChromiumUpdateService`), so that download is not checked against
  the hash in the JSON file. Use it for a test only, and set the pref back to
  false afterwards.
- Whether `4.10.3112.0` is current upstream could not be checked offline.

### Shaka error numbers

Part 4b gives only the classes (1xxx network, 6xxx DRM) and 3016 as
`VIDEO_ERROR`, all from memory of the Shaka Player error list. Nothing was
checked against the Shaka source, it was not verified that Udemy uses Shaka,
and individual 6xxx numbers are deliberately left out of the reader's part.
Check the list in Shaka's documentation before relying on a number.

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
- `MOZ_LOG` options used: `timestamp`, `append` and `maxsize:<MB>` (a size cap
  needs `append` and keeps the end of the file; `xpcom/base/Logging.cpp`, the
  option parser in `LogModuleManager::Init`). With `--net`: `nsHttp:4` (level 3
  logs almost nothing) and `nsHostResolver:5`. DNS-over-HTTPS logging in this
  tree goes through `nsHostResolver` (`netwerk/dns/DNSLogging.h`); no module
  named `TRR` was found.
- Log modules used: `EME` and `EMEV` (`dom/media/eme/EMEUtils.cpp`), `GMP`
  (`GMPService.cpp`), `MediaDecoder`, `MediaSource`, `PlatformDecoderModule`.
  `MediaKeys` from the original list is not a log module name in this tree;
  `MediaKeys.cpp` logs with `EME_LOG`, so it falls under `EME`.
