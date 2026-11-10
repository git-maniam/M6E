# m6e-torrent, the BitTorrent helper

The helper runs M6E's torrent engine (libtorrent-rasterbar 2.1.2, D39) in its own process. The browser's `M6ETorrentService.sys.mjs` starts it and talks to it over standard input and output, one JSON object per line (`PROTOCOL.md`). It is not built by `./mach build`.

## Build

    tools/m6e/torrent/build-helper.sh [--clean] [--install-into <M6E.app>]

- Builds libtorrent once from the pinned release tarball `libtorrent-rasterbar-2.1.2.tar.gz` (sha256 `3362546d9cd71b9e49ee6cac7d3f1f914ce9cdb217c86b63d5b22cbed0334dbc`), checked before use, into `M6E_TORRENT_CACHE` (default `<main checkout>/artifacts/p8/cache`). The cache folder name includes a key over the tarball, compiler, SDK, Boost version and flags, so a toolchain change rebuilds it. The script looks for the tarball in `M6E_LIBTORRENT_TARBALL`, then the cache, then `artifacts/p8/engine-libtorrent/dl-2.1.2/`. It never downloads, and the libtorrent build runs under `sandbox-exec` with the network denied.
- `M6E_LIBTORRENT_PREFIX` uses an existing static install instead.
- libtorrent options: static, no TLS, no WebTorrent, no I2P, no deprecated functions. Compiler: Homebrew clang with the SDK's libc++ headers (as in `mozconfig`), arm64, macOS 11.0, `-ftrivial-auto-var-init=zero`. Boost (header-only, Boost.JSON included) comes from Homebrew.
- The helper is compiled from every `src/*.cpp` with the flags from libtorrent's pkgconf file, so its ABI matches the library. The script checks the result: arm64 only, minimum macOS 11.0, and links only to `/usr/lib` and `/System/Library`.
- Output: `tools/m6e/torrent/build/m6e-torrent` (git-ignored). `--install-into` copies it to `<M6E.app>/Contents/MacOS/`, where the service looks for it; sign the app again afterwards. Packaging and signing come with P8.7.
- First build: about 40 seconds on 14 cores. After that only the helper sources are compiled.

## Run

    m6e-torrent --protocol 1 --profile-dir <absolute path of <profile>/torrents>

`M6E_TORRENT_IDLE_SECONDS` (default 60, 0 turns it off) sets the idle exit. Exit status 0 on `shutdown`, stdin end of file, SIGTERM, SIGINT, SIGHUP or the idle exit; 2 for a bad command line; 1 if the engine cannot start.

## Sources
- `src/main.cpp`: command line, the `poll` loop over stdin, libtorrent's wake pipe and signals, request dispatch, and the exit path. A watchdog ends the process 1.7 s after `shutdown` or end of file, whatever the engine is doing.
- `src/engine.{hpp,cpp}`: the libtorrent session and every operation and event of `PROTOCOL.md`.
- `src/protocol.{hpp,cpp}`: line framing (1 MiB limit), argument checks, UTF-8 repair of output, base64.
- `src/json.{hpp,cpp}`: Boost.JSON, compiled header-only into `json.cpp`.
- `src/ipfilter.{hpp,cpp}`: the PeerGuardian and eMule block list parser, into libtorrent's `ip_filter` (a set of merged address ranges).
- `src/safety.*`, `src/confine.*` (helper-confine): the parsing and path rules; `sandbox/`: the sandbox profile, not applied yet.

## Behaviour
- **Session settings at start:** no listen interface, no outgoing interface, DHT, local discovery, UPnP, NAT-PMP and the interface notifier off, no DHT bootstrap nodes, Peer ID prefix `-LT0000-`, an empty user agent and libtorrent's anonymous mode (so the extension handshake names no client and public trackers see `curl/7.81.0`; nothing names libtorrent, M6E or a version), `stop_tracker_timeout` 1 s, 4 MiB of queued disk writes, 2 disk threads, metadata limited to 16 MiB and 3 million bencoding tokens, `min_reconnect_time` 10 s (libtorrent's 60 s left a paused and resumed torrent without peers for a minute).
- **Sockets follow activity:** the listen sockets (all interfaces on one random port, 20000 to 59999, chosen once per process unless `listenPort` is set; or only `bindInterface`) open just before a torrent starts or a magnet link's metadata is fetched, and close when nothing is active or fetching. DHT runs only while `dht` is on and a public torrent is active or a magnet is fetched; local discovery only while `lpd` is on and a public torrent is active; port mapping only while `portMapping` is on and the sockets are open. A `.torrent` waiting for `confirm` opens nothing.
- **No payload before `confirm`:** a `.torrent` is parsed and checked and stays out of the engine until `confirm`. A magnet link is added with every file at priority 0 (`default_dont_download`), with peer exchange and local discovery off, and with DHT only if `dht` is on; when the metadata arrives it is checked like a `.torrent` and the handle is dropped. `confirm` adds the torrent again with the user's priorities, save folder and flags. Private torrents get DHT, peer exchange and local discovery off for good.
- **Before any transfer** (`confirm` with `start`, `resume`, `resumeAll`, `restore` with `start`): `checkRelativePaths` walks the save folder without following links, then `prepareDirectories` creates the folders. `confirm` also refuses a selection that does not fit (`disk-full`).
- **Completion:** on libtorrent's finished alert the helper flushes the torrent's disk cache first, because libtorrent reports a piece finished before its blocks reach the file. Then `finalizeRelative` clears every execute bit and sets `com.apple.quarantine` on each selected file, and only then the `completed` event is written. If a file cannot be finalised (a volume without extended attributes), the event carries a `warning`. Until the flush is done the torrent still reports `downloading`.
- **Resume data:** `<profile-dir>/<tid>.resume`, mode 0600, written atomically at `confirm` (with the full metadata, so the original `.torrent` path is not needed), on pause and completion, every 30 s when changed, and at shutdown (up to 0.6 s). `restore` checks every file again (`checkInfoDict`, the info hash against the file name, an absolute save path). A torrent the user paused stays paused at `restore`.
- **Rate limits:** the global rates (Turtle mode switches to the second set) and per-torrent rates (`setTorrentLimits`). libtorrent puts loopback and local network peers in a "local" peer class that the global limits do not reach; the helper's peer class filter puts them in the global class as well, so the limits cover every peer.
- **Queue:** a started torrent is auto-managed and added paused, so libtorrent's queue (`active_downloads`, `active_seeds` and `active_limit` from `maxActiveDownloads` and `maxActiveTorrents`, 0 meaning unlimited) starts it when a slot is free; `dont_count_slow_torrents` is off, so a stalled torrent keeps its slot. A user pause takes the torrent out of the queue, `resume` puts it back.
- **Seeding goals:** checked once a second in `tick` (`applySeedGoals`) against the ratio of `Status` and libtorrent's `seeding_duration`. `stop` pauses the torrent as `completed`; `slow` caps its upload at Turtle mode's upload rate. The reached goal is kept in the resume data.
- **Proxy (SOCKS5):** the proxy settings are given to libtorrent only while something is active, together with the listen interfaces, because libtorrent opens its proxy UDP relay (and contacts the proxy) as soon as a peer proxy is set. With a proxy, uTP, DHT, local discovery and port mapping are off, UDP trackers are withheld from the engine (`replace_trackers`; the full list is kept in the resume data), `proxy_hostnames` sends names to the proxy, and anonymous mode stays on. libtorrent never falls back to a direct connection.
- **Kill switch:** `watchBoundInterface` runs once a second and after `setConfig`. When the bound interface is gone it sets `stop_tracker_timeout` to 0 (no `stopped` announce), pauses the session (every peer and tracker connection closes), closes the listen sockets and keeps DHT, local discovery and port mapping off; when it returns, it reopens the listen sockets and resumes the session. libtorrent binds outgoing and listen sockets with `IP_BOUND_IF`, which macOS refuses on IPv6 sockets (EINVAL, then libtorrent only binds the source address), so the IP filter blocks all of IPv6 while an interface is bound. The lookup is injectable (`EngineOptions::interfacePresent`); `M6E_TORRENT_TEST_INTERFACE_GATE` lets the tests make the interface look gone.
- **IP filter:** `loadIpFilter` takes a file directly in `<profile-dir>/inbox/` only, opened through `openConfined` (no link followed), parses it with `parseIpFilter` and applies it, with the IPv6 block when bound, through `set_ip_filter`. Every torrent and magnet fetch has `apply_ip_filter`, so incoming connections are filtered too.
- **Power:** an IOKit `PreventUserIdleSystemSleep` assertion named "M6E BitTorrent transfer" is held while a confirmed torrent is active and released when none is (and at exit).
- **Idle exit:** after `M6E_TORRENT_IDLE_SECONDS` with nothing pending, nothing active and no request.

## Not yet done and known gaps
- With a peer proxy, libtorrent keeps one UDP socket on all interfaces while a torrent is active (its SOCKS5 UDP relay endpoint). It sends nothing directly and drops datagrams that do not come from the proxy (`tests/privacy` checks that it answers nothing), but a port scan sees it as open or filtered.
- The kill switch was tested with the real watchdog and a real interface (`lo0`) made to look gone, and with an interface that does not exist; a VPN interface (`utun`) disappearing for real was not tested, because creating one needs root.
- With both a peer proxy and `bindInterface`, peer connections to the proxy are bound to the interface, but tracker connections to the proxy are not: libtorrent binds tracker connections to the listen socket, and its proxy listen socket has no device. The kill switch still stops them when the interface goes away.
- The service still sends the user's own IP filter path; it must copy the file into `<profile-dir>/inbox/` first.
- No TLS: `https://` trackers and web seeds fail.
- The sandbox profile in `sandbox/` is not applied (see its README for what the service must provide first).

## Notes for testing
- Only the synthetic loopback fixtures in `fixtures/` (spec 8.9, D38). `tests/run_tests.py` is the conformance suite; `--slow --idle-seconds 5` includes the idle exit.
- `tests/privacy/run.sh [helper]` runs the privacy and settings tests (encryption, limits, queue, seeding goals, SOCKS5, kill switch, IP filter, uTP, web seeds, v2, identity) against a local tracker, seed, SOCKS5 server, relay and web seed on 127.0.0.1 and ::1, in about 4 minutes.
- libtorrent tries uTP before TCP, so a TCP-only peer such as the fixture seed is reached about 4 s after the tracker answers.
