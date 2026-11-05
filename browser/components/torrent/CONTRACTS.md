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

