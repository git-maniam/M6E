# BitTorrent feature: browser-side contracts (spec Phase 8, D33 to D37)

Shared by the parallel steps P8.1 to P8.6. Agents may add to this file's contracts but never change or remove one; a needed change goes back to the lead. The helper protocol is in `tools/m6e/torrent/PROTOCOL.md`.

## Who owns what
| Step | Owns |
|---|---|
| helper (P8.0, P8.1 helper) | `tools/m6e/torrent/**` |
| service (P8.1 JS) | `browser/components/torrent/M6ETorrentService.sys.mjs`, `tests/xpcshell/service/**`, the `m6e.torrent.*` block in `browser/app/profile/m6e.js` |
| shady (P8.3) | `browser/components/torrent/M6ETorrentRisk.sys.mjs`, `m6e-torrent-rules.json` (register it in `moz.build`), `tests/xpcshell/risk/**` |
| ui (P8.2) | `browser/base/content/browser-m6eTorrent.js`, the panel and button markup in `browser/base/content/navigator-toolbox.inc.xhtml` and `browser.xhtml`, `browser/themes/shared/identity-block/m6e-torrent.css` and `.svg`, `browser/locales/en-US/browser/m6eTorrent.ftl`, the Add dialog files, `tests/browser/ui/**` |
| open (P8.4) | `browser/app/macbuild/Contents/Info.plist.in`, `browser/components/torrent/M6ETorrentHandler.sys.mjs`, `tests/browser/open/**` |
| privacy, settings, page (Wave B) | assigned later |

`moz.build`, the manifests and the three module stubs already exist; replace the stubs, and do not rename them. Shared registration files (`browser.xhtml`, `browser/base/jar.mn`, `browser/themes/shared/jar.inc.mn`, `browser/themes/shared/browser-shared.css`, `m6e.js`): keep edits to a few lines marked `M6E:`.

## Data shapes (JSON-compatible plain objects)
- `Metadata`, `Status`: exactly as in `PROTOCOL.md`.
- `Source`: `{ "kind": "page" | "magnet-page" | "finder" | "unknown", "origin": string | null }`. `origin` is the https origin (scheme, host and port, no path) of the page the `.torrent` was downloaded from or the `magnet:` link was clicked on; `null` when unknown. A non-https origin is never trusted.
- `RiskResult`: `{ "verdict": "ok" | "shady", "trusted": boolean, "reasons": [{ "code": string, "severity": "high" | "medium", "files": [string] }] }`. `code` is a stable identifier such as `windows-program`, `double-extension`, `bidi-override`, `piracy-tool-name`, `program-in-media`, `macos-installer`, `shortcut-file`, `password-file`.

## Modules and entry points
- `moz-src:///browser/components/torrent/M6ETorrentRisk.sys.mjs` exports `M6ETorrentRisk` with `assess(metadata, source, extraTrustedSites = [])` returning a `RiskResult`, and `isTrustedOrigin(origin, extraTrustedSites = [])`. It is a pure function of its inputs and the bundled rules, with no Gecko globals except what is needed to read the rules file.
- `moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs` exports `M6ETorrentService`, a parent-process singleton. All methods are async.
  - `addFile(path, source)` returns `{ tid, metadata }`; `addMagnet(uri, source)` returns `{ tid }`; `confirmAdd(tid, { files, savePath, sequential, start })`; `cancelAdd(tid)`; `pause(tid)`; `resume(tid)`; `remove(tid, deleteFiles)`; `pauseAll()`; `resumeAll()`; `list()`; `detail(tid)`; `setLimits(limits)`; `getConfig()` and `setConfig(partial)` (map the prefs below to the helper's `setConfig`); `hasActiveTorrents()`; `shutdown()`.
  - Observer notifications through `Services.obs` (the data is a JSON string): `m6e-torrent-changed` with `{ "torrents": [Status] }` (at most once a second); `m6e-torrent-metadata` with `{ "tid", "metadata", "source" }` when a magnet's metadata arrives; `m6e-torrent-error` with `{ "tid"?, "code", "message" }`; `m6e-torrent-completed` with `{ "tid" }`.
  - Lifetime: the helper is started on first need, stopped 60 seconds after no torrent is active, restarted after a crash, and stopped when the browser quits. With `enabled` false it never starts.
  - It enforces the safety rules of `PROTOCOL.md` before sending a file to the helper.
- `browser/base/content/browser-m6eTorrent.js` defines the global `gM6ETorrent` with `addTorrentFile(path, source)` and `addMagnet(uri, source)`: open the Add dialog for a file or a magnet link. The Add dialog calls `M6ETorrentRisk.assess` and `M6ETorrentService`. `gM6ETorrent.openPanel()` opens the drop-down panel.
- `moz-src:///browser/components/torrent/M6ETorrentHandler.sys.mjs` exports `M6ETorrentHandler` with `init()` (called once at startup from the existing startup path of the browser; say where in the report), which routes `.torrent` files, `application/x-bittorrent` downloads and `magnet:` navigations to `gM6ETorrent` of the most recent browser window, with the right `Source`.

## Prefs (the service agent defines all of them, with these defaults, in `m6e.js`)
`m6e.torrent.enabled` true; `resumeAtStartup` true; `confirmQuit` true; `encryption` "enabled"; `dht` true; `pex` true; `lpd` false; `portMapping` false; `listenPort` 0; `downKBps` 0; `upKBps` 0; `turtle.enabled` false; `turtle.downKBps` 50; `turtle.upKBps` 10; `maxActiveDownloads` 3; `maxActiveTorrents` 5; `seedRatioPercent` 100 (0 means no ratio goal); `seedHours` 0; `proxy.type` "none"; `proxy.host` ""; `proxy.port` 1080; `proxy.username` ""; `proxy.peers` true; `bindInterface` ""; `ipFilterPath` ""; `randomizePeerId` false; `sequentialDefault` false; `trustedSites` "" (a JSON array of extra trusted origins the user added). The proxy password is not a pref: store it with the login manager and say so in the report.

## Rules for everyone
- Follow `AGENTS.md` (almost no comments, no emoji) and mark edits to shared upstream files with `M6E:`.
- No file data may be requested before `confirm` with `start: true`; nothing runs, listens or connects at idle.
- Tests: name them `*_m6e_*` where the area has that convention; write xpcshell or browser tests the lead can run after the merge. Pure logic must also be runnable under plain Node when practical.

## Additions (lead, 2026-11-05)
- The service observes the `m6e.torrent.*` prefs and pushes changes to the helper; the UI only sets prefs.
- `M6ETorrentService.init()` is called once at startup by `gM6ETorrent.init()` (the ui agent); it starts the helper only when saved torrents exist.
- `M6ETorrentService.activeTorrentCount` is a synchronous getter over cached state (0 before the first status), for the quit prompt, which cannot wait. `hasActiveTorrents()` stays as the async form.
- `gM6ETorrent.addTorrentFile` and `gM6ETorrent.addMagnet` return a promise that settles when the Add dialog closes.
- The service reports errors by throwing an `Error` with `code` and, for `duplicate`, `tid` properties copied from the helper's error object.
- `list()` resolves to `{ torrents: [Status] }`.


## Additions (page agent, 2026-11-10)
- `about:torrents[#<tid>]` is a parent-process, chrome-privileged page (`browser/components/torrent/content/torrents.{html,css,js}`, registered in `AboutRedirector.cpp`, `components.conf` and the new `browser/components/torrent/jar.mn`). It calls `M6ETorrentService.list()` and `detail(tid)`, `pause`, `resume` and `remove`, and listens to `m6e-torrent-changed`. Its global is `gM6ETorrentsPage`. The fragment selects a torrent; an unknown or malformed one selects nothing.
- Strings: `browser/locales/en-US/browser/m6eTorrents.ftl` (`m6e-torrents-*`); the page also links `m6eTorrent.ftl` for the state and priority names.
- Torrent text is shown only through `textContent` or attribute values after `displayName`, never in a Fluent message value.

## Additions (service, P8.1, 2026-11-10)
- Observer notification `m6e-torrent-trackers` with `{ "tid", "trackers": [...] }` (the helper's `trackers` event). `m6e-torrent-completed` may carry `"warning"` (the helper's, when it could not clear execute bits or quarantine a file). The helper's `peers` event updates `seeds` and `peers` of the cached Status.
- The helper's idle exit: the service starts it with `M6E_TORRENT_IDLE_SECONDS` of 90 (never less than the service's own idle stop plus 30 s), and an exit with status 0 while nothing is active is not an error (no `helper-exited`).
- Input files are staged: a `.torrent` for `add` and an IP-filter file for `setConfig` are copied to `<profile>/torrents/inbox/` (mode 0600) and deleted after the helper replies. Files left behind by a crash and older than 5 minutes are removed at helper start.
- Prefs `m6e.torrent.sandbox` (default false) and `m6e.torrent.sandboxFolders` (JSON array of folders confirmed earlier, most recent first). With `sandbox` on, the helper gets `--sandbox --inbox-dir <inbox> --download-dir <folder>` (up to four folders: the default download folder, then the remembered ones). A `confirmAdd` into another folder remembers it, stops the helper and adds the pending torrent again to the restarted one.
- `setConfig` keys the helper does not apply fail closed: `encryption`, `dht`, `pex`, `lpd`, `portMapping`, `listenPort`, `proxy`, `bindInterface` and `ipFilterPath` (with a value that is not off) stop the helper and make start, `setConfig` and `setLimits` reject with `config-failed`. `seedGoal`, `peerIdPrefix` and `userAgent` are reported once per helper start as `m6e-torrent-error` with code `unsupported-setting` and the helper keeps running.
- The Gecko wake lock is gone: the helper holds the sleep assertion while a confirmed torrent is active.
- `list()` resolves to `{ torrents }` and `activeTorrentCount` exists as specified above.

## Additions (add-flow, P8.4, 2026-11-10)
- `gM6ETorrent.addTorrentFile(path, source, options)` and `addMagnet(uri, source, options)` take `options.goToPage`. The handler always passes `{ goToPage: true }` (D40): the window's `about:torrents` tab is selected, or opened in the foreground, the Add dialog opens on that tab's browser, and after Add the torrent is selected through `about:torrents#<tid>`. Cancel leaves the page as it is; a duplicate selects the existing torrent there. Without the option (the panel's own file picker and paste field) the dialog opens over the current tab as before.
- The Add dialog reads `m6e.torrent.downloadDir` (an absolute path; empty or relative means the browser's download folder), `m6e.torrent.askWhereToSave` (the folder chooser opens as soon as the dialog is ready; cancelling it keeps the default folder) and `m6e.torrent.startNow` (the default of the Start now checkbox), next to `sequentialDefault`. The prefs reach the dialog as `params.defaultSavePath`, `params.askWhereToSave` and `params.startNow`.
- The Add dialog ignores `m6e-torrent-error` events with code `unsupported-setting`.
