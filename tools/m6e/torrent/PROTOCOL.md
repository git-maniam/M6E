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

## Clarifications (added by the lead, 2026-11-05, answering the helper agent)
- `Metadata.files[].path` is relative to the torrent's top-level folder. On disk a multi-file torrent lives at `<savePath>/<name>/<path>`; a single-file torrent at `<savePath>/<name>`.
- `list` and `progress` report only confirmed torrents. Pending torrents (`pending-confirm`, `fetching-metadata`) are reachable through `add`, `confirm`, `cancel`, `detail` and the `metadata` event only.
- A repeated `add` of a torrent that is still pending is idempotent: it returns the same `tid`, `state` and `metadata`. Adding one that is already confirmed fails with `duplicate`.
- Unknown `seeds` or `peers` counts are `-1`.
- A torrent with a symlink entry (BEP 47) is rejected as `unsafe-path`.
- At `confirm` the helper stores everything it needs (the full metadata) in its own resume data. It must not depend on the original `.torrent` path, which may be a temporary file.
- "No listening socket at idle" means: nothing pending and nothing active. Sockets (DHT, tracker, listening) may be open while a magnet link's metadata is being fetched. A private torrent behind a magnet link cannot be recognised before its metadata arrives, so DHT may be used for that short fetch only; the helper drops the handle as soon as the metadata is in.
- If the engine sets the executable bit from a torrent attribute, the helper clears it when the file completes (and says so in the README if it cannot).


## Additions (helper-core, 2026-11-10)
- Environment: `M6E_TORRENT_IDLE_SECONDS` (integer, default 60; 0 turns the idle exit off). The helper exits with status 0 once nothing has been pending or active, and no request has arrived, for that long. Exit status 2 means a bad command line.
- Before the first `setConfig`, `dht`, `pex`, `lpd` and `portMapping` are off and `encryption` is `"enabled"`.
- `setConfig` lists as `unsupported`, until P8.5 adds them: `proxy` with `type` `"socks5"`, an `ipFilterPath` that is not null, and a `seedGoal` with a `ratio` or `hours`. Unknown keys are `unsupported` too. A key with a wrong type fails the whole request with `bad-request` and nothing is applied.
- Event `trackers`: `{ "tid", "trackers": [the tracker objects of detail] }`, at most once a second per torrent, after a tracker answers or fails.
- Event `peers`: `{ "tid", "seeds", "peers" }`, when a torrent's connected counts change (checked once a second).
- Event `completed` may carry `"warning": string` when the helper could not clear the execute bits or set the quarantine attribute on a finished file. `completed` is written only after the torrent's data is flushed to its files; until then the torrent reports `downloading`.
- `Metadata.files[]` of a magnet link with a BEP 53 `so=` selection carries `"selected": boolean`.
- `Status`: `size` is the size of the selected files, `downloaded` the verified bytes of the selected files, `uploaded` all bytes uploaded, `ratio` uploaded divided by downloaded (0 when nothing is downloaded); `seeds` and `peers` count connected seeds and connected peers that are not seeds.
- `progress` lists every confirmed torrent. After the last active torrent stops, one more `progress` event reports the final states, then none.
- A line that is not a JSON object with an integer `id`, or that is longer than 1 MiB, cannot be answered: the helper writes an `error` event with `code` `"bad-request"` instead.
- A reply or `metadata` event that would be longer than 1 MiB (a torrent with tens of thousands of files) becomes a `too-large` error.
- `confirm` treats a missing `sequential` or `start` as false. `remove` of a pending torrent works like `cancel`. `remove` with `deleteFiles` replies after the engine has deleted the files (at most 3 seconds).
- Files in `--profile-dir`: `<tid>.resume` (libtorrent resume data with the info dictionary and an `m6e` dictionary), mode 0600; `pending/`, the engine's save folder while a magnet link's metadata is fetched (nothing is written there).

## Additions (privacy, P8.5, 2026-11-10)
- `setConfig` applies `proxy` with `type` `"socks5"`, an `ipFilterPath` and a `seedGoal` with a `ratio` or `hours`; none of them is `unsupported` any more. The result also carries `forcedOff`: the features the current `proxy` and `bindInterface` hold off whatever their own keys say, from `"dht"`, `"lpd"`, `"portMapping"`, `"utp"`, `"udpTrackers"`, `"incoming"` and `"ipv6"`.
- Changing `proxy`, `bindInterface` or `encryption` while a torrent is active or a magnet link is fetched closes every connection; new ones use the new settings.
- `proxy`: `host` 1 to 253 printable characters, `port` 1 to 65535, `username` and `password` optional (at most 255 bytes; with a `username` the user/password method of RFC 1929 is used), `peers` defaults to true. The proxy is used only while something is active; an idle helper does not contact it. With any proxy: DHT, local discovery, port mapping, uTP and UDP trackers are off; UDP trackers stay in `detail` with `status` `"disabled"`; tracker and web seed host names are sent to the proxy unresolved; nothing connects directly, and if the proxy refuses or is down, nothing connects at all. With `peers: true` peers and web seeds go through the proxy too, no TCP listening socket opens (`listening` is not sent) and incoming connections are impossible; libtorrent keeps one UDP socket on all interfaces for the proxy's UDP relay, which sends nothing directly and drops every datagram that does not come from the proxy. With `peers: false` peers and web seeds connect directly and the listening socket opens as usual.
- `ipFilterPath` must name a regular file directly in `<profile-dir>/inbox/` (the service copies the user's list there); it is opened without following links, at most 64 MiB, PeerGuardian text (`name:first-last`) or eMule `ipfilter.dat` (`first - last , level , name`; a level above 127 does not block), IPv4 and IPv6, leading zeros allowed, mixed and with CRLF; blank lines and lines starting with `#` or `//` are ignored. Errors, after which nothing in the request is applied: `bad-request` (not absolute, not in the inbox, or no range in the file), `not-found`, `unsafe-path` (a link, a hard-linked or non-regular file), `too-large`, `io-error`. The result carries `ipFilter`: `{ "ranges": integer, "skipped": integer }` (lines read as ranges, lines that could not be read). The filter applies to peers in both directions, to trackers whose names are resolved locally, to DHT nodes and to magnet metadata fetches. `null` removes it.
- `seedGoal`: `{ "ratio": number|null, "hours": number|null, "action": "stop"|"slow" }`; a ratio or hours of 0 or null sets no goal, `action` defaults to `"stop"`. Checked once a second for each seeding torrent, with `ratio` as in `Status` and `hours` the time spent seeding (kept across restarts); the first goal reached counts. Then the event `goalReached` `{ "tid", "action" }` is written and `Status.goalReached` is true. `"stop"` pauses the torrent in state `completed`, which `restore` keeps; `"slow"` keeps it seeding at the alternative upload limit, `turtle.upKBps` of `setLimits` (the lower of that and its own upload limit; 0 is unlimited). `resume` of a `completed` torrent seeds again without the goal, while `pauseAll` and `resumeAll` leave `completed` torrents alone; a new `seedGoal` applies again to torrents that are still seeding.
- `bindInterface` (kill switch): checked when set and once a second (`if_nametoindex`). While the interface is missing: event `interfaceDown` `{ "interface" }`, every connection is closed (without a tracker `stopped` announce), and no connection, listening socket, DHT, local discovery or port mapping opens; requests are answered as usual and torrents keep their states. Event `interfaceUp` `{ "interface" }` when it returns or the binding is cleared, then transfers resume. While an interface is bound IPv6 is off (every IPv6 address is blocked for peers, trackers and DHT): macOS's `IP_BOUND_IF`, which libtorrent uses, fails with EINVAL on IPv6 sockets, so they would be bound by source address only.
- Environment, for tests only: `M6E_TORRENT_TEST_INTERFACE_GATE` (a path); while that file is missing the bound interface counts as gone. It can only trigger the kill switch, never hide a missing interface.
- New operation `setTorrentLimits` `{ "tid", "downKBps"?, "upKBps"? }` returns `{ "status": Status }`; 0 is unlimited; the limits are saved with the torrent. `Status` carries `downLimitKBps`, `upLimitKBps` (the torrent's own limits) and `goalReached` (boolean).
- `setLimits`: the rates apply to every peer, including loopback and local network peers (libtorrent exempts those by default). A started torrent is queued (state `queued`, no connection) while `maxActiveDownloads` downloading or `maxActiveTorrents` active torrents (seeding ones included) are running; 0 or -1 is unlimited, and a slow or stalled torrent still counts. `resume` puts a torrent back in the queue, so it may report `queued` until a slot is free.
- Neutral identity (`peerIdPrefix` and `userAgent` null): Peer ID prefix `-LT0000-`, no client name (`v`) in the extension handshake, tracker `User-Agent` `curl/7.81.0` for public torrents and none for private ones, and no local addresses sent to private trackers (libtorrent's anonymous mode). A `userAgent` is sent to trackers and in the extension handshake, except that a proxy always keeps the neutral identity for public torrents.
- `encryption`: `"forced"` accepts and makes only RC4-encrypted connections and refuses plaintext peers; `"enabled"` accepts both and may try plaintext first (libtorrent retries without encryption after a failed attempt); `"disabled"` is plaintext only.
- Engine protocols on by default: uTP (BEP 29), web seeds (BEP 19), v2 and hybrid torrents (BEP 52). Local discovery (BEP 14) stays off until `lpd`.
