# m6e-torrent helper protocol, version 1

The contract between the browser's `M6ETorrentService.sys.mjs` and the `m6e-torrent` helper (spec Phase 8, D34). Agents may add fields and messages; they must never rename, remove or change the meaning of anything here. A needed change goes back to the lead.

## Transport
- The browser starts the helper and talks to it over the helper's standard input and output: UTF-8, one JSON object per line, no line longer than 1 MiB. Nothing else is written to standard output. Standard error is free-form log text that the browser captures and never parses.
- There is no control socket and no web server. The only sockets the helper opens are the BitTorrent peer, tracker and DHT sockets (see "Idle").
- Command line: `m6e-torrent --protocol 1 --profile-dir <absolute path>`. `--profile-dir` is the folder for resume data (`<profile>/torrents`), created by the helper if missing.
- The first line the helper writes is the `ready` event. It exits with status 0 after replying to `shutdown`, and promptly (within 2 seconds, closing every connection) when standard input reaches end of file, so a dead browser never leaves an orphan.

## Requests and replies
Request: `{"id": <integer>, "op": "<name>", "args": { ... }}`. Reply, always exactly one per request, in any order: `{"id": <same>, "ok": true, "result": { ... }}` or `{"id": <same>, "ok": false, "error": {"code": "<code>", "message": "<text>"}}`.

Error codes: `bad-request`, `not-found`, `invalid-torrent`, `too-large`, `unsafe-path`, `duplicate` (the error also carries `"tid"` of the existing torrent), `disk-full`, `io-error`, `engine-error`.

## Identifiers and objects
- `tid`: a torrent id, the lowercase hex info hash: 40 characters for a v1 or hybrid torrent (SHA-1); for a v2-only torrent the first 40 characters of its SHA-256 info hash.
- `Metadata`: `{ "infoHash": <tid>, "name": string, "totalSize": integer, "pieceLength": integer, "files": [{ "index": integer, "path": "a/b/c", "length": integer }], "trackers": [url], "webSeeds": [url], "isPrivate": boolean }`. Paths use `/`, are relative, and have already passed the safety rules below. Padding files (`.pad`) are listed with `"pad": true`.
- `Status`: `{ "tid", "name", "state", "progress": 0..1, "size", "downloaded", "uploaded", "downKBps", "upKBps", "etaSeconds": integer|null, "ratio": number, "seeds", "peers", "isPrivate", "savePath", "sequential": boolean, "addedAt": epoch ms, "error": {"code","message"}|null }`.
- `state` is one of: `pending-confirm` (metadata known, waiting for `confirm`; no payload is requested), `fetching-metadata` (a magnet link; only BEP 9 metadata is fetched), `checking`, `queued`, `downloading`, `seeding`, `paused`, `completed` (finished and not seeding), `error`.

## Operations
| op | args | result |
|---|---|---|
| `add` | `{ "file": "<absolute .torrent path>" }` or `{ "magnet": "<magnet URI>" }` | `{ "tid", "state", "metadata"? }`. A file returns `metadata` at once with `state` `pending-confirm`. A magnet returns `fetching-metadata` and the `metadata` event follows. Adding never downloads payload data. Pending torrents are not saved and vanish when the helper stops. |
| `confirm` | `{ "tid", "files": [{ "index", "priority": 0..3 }], "savePath": "<absolute folder>", "sequential": boolean, "start": boolean }` | `{ "status": Status }`. Priority 0 is skip, 1 low, 2 normal, 3 high. This is the only operation that starts payload transfer, and only with `start: true`. The torrent is saved for later startup. |
| `cancel` | `{ "tid" }` | `{}`. Drops a pending torrent. Deletes nothing. |
| `pause`, `resume` | `{ "tid" }` | `{ "status": Status }` |
| `pauseAll`, `resumeAll` | `{}` | `{}` |
| `remove` | `{ "tid", "deleteFiles": boolean }` | `{}` |
| `list` | `{}` | `{ "torrents": [Status] }` |
| `detail` | `{ "tid" }` | `{ "status": Status, "metadata": Metadata, "trackers": [{ "url", "status": "working"\|"updating"\|"error"\|"disabled", "seeds", "peers", "message" }], "peers": [{ "address", "client", "encrypted": boolean, "downKBps", "upKBps", "progress": 0..1 }], "pieces": { "count", "have": "<base64 bitfield>" }, "files": [{ "index", "priority", "progress": 0..1 }] }` |
| `setLimits` | `{ "downKBps", "upKBps", "turtle": { "enabled", "downKBps", "upKBps" }, "maxActiveDownloads", "maxActiveTorrents" }`, any subset | `{}`. 0 means unlimited. |
| `setConfig` | any subset of the keys below | `{ "applied": [keys], "unsupported": [keys] }` |
| `restore` | `{ "start": boolean }` | `{ "torrents": [Status] }`. Loads the saved torrents from `--profile-dir`; with `start: false` they all load paused. The browser sends it once after `ready`. |
| `shutdown` | `{}` | `{}` then exit |

`setConfig` keys: `encryption` (`"disabled"`, `"enabled"`, `"forced"`), `dht`, `pex`, `lpd` (booleans; always off for a private torrent whatever they say), `portMapping` (boolean), `listenPort` (0 chooses a random port at each start), `proxy` (`{ "type": "none"\|"socks5", "host", "port", "username", "password", "peers": boolean }`; names are resolved by the proxy; nothing may bypass it), `bindInterface` (string or null; when set and the interface is missing, every connection closes and none opens until it returns), `ipFilterPath` (absolute path of a PeerGuardian `.p2p` or eMule `ipfilter.dat` file, or null), `seedGoal` (`{ "ratio": number\|null, "hours": number\|null }`), `peerIdPrefix` (string, or null for a neutral prefix that names neither M6E nor a version), `maxConnections`, `userAgent` (string or null for a neutral value).

## Events
`{"event": "<name>", ...}`, written whenever they occur:
- `ready`: `{ "protocol": 1, "engine": "<name and version>", "pid": integer }`, always first.
- `metadata`: `{ "tid", "metadata": Metadata }` once a magnet link's metadata has arrived.
- `progress`: `{ "torrents": [Status] }`, at most once a second, and only while at least one torrent is active.
- `completed`: `{ "tid" }`.
- `listening`: `{ "port": integer }` when a listening socket opens; `closed` when the last one closes.
- `interfaceDown`: `{ "interface": string }` and `interfaceUp` for the kill switch.
- `error`: `{ "tid"?: string, "code": string, "message": string }`.
- `log`: `{ "level": "debug"\|"info"\|"warn"\|"error", "message": string }`.

## Idle and privacy rules the helper must keep
- With no torrent confirmed to start (and no resumed active torrent), the helper opens no listening socket, runs no DHT and maps no port. Fetching a magnet link's metadata may use trackers and DHT, but requests no payload.
- Private torrents (`isPrivate`) never use DHT, peer exchange or local discovery.
- Local discovery and port mapping are off until `setConfig` turns them on.

## Safety rules the helper enforces (the service enforces the same before sending)
- A `.torrent` larger than 16 MiB, or with bencoding nested deeper than 64 levels, is `too-large` or `invalid-torrent`.
- Any file path that is absolute, or has an empty, `.`, `..` or NUL component, or a `:` or `\`, is `unsafe-path`; the torrent is not added.
- Writes happen only under the torrent's `savePath`; never follow a symlink out of it.
- Completed files get the macOS quarantine attribute, applied by the browser side (the helper reports `completed`; it does not need to).
