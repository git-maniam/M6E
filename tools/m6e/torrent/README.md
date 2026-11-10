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
- `src/safety.*`, `src/confine.*` (helper-confine): the parsing and path rules; `sandbox/`: the sandbox profile, not applied yet.

## Behaviour
- **Session settings at start:** no listen interface, no outgoing interface, DHT, local discovery, UPnP, NAT-PMP and the interface notifier off, no DHT bootstrap nodes, Peer ID prefix `-LT0000-` and user agent `libtorrent` (neither names M6E or a version), `stop_tracker_timeout` 1 s, 4 MiB of queued disk writes, 2 disk threads, metadata limited to 16 MiB and 3 million bencoding tokens, `min_reconnect_time` 10 s (libtorrent's 60 s left a paused and resumed torrent without peers for a minute).
- **Sockets follow activity:** the listen sockets (all interfaces on one random port, 20000 to 59999, chosen once per process unless `listenPort` is set; or only `bindInterface`) open just before a torrent starts or a magnet link's metadata is fetched, and close when nothing is active or fetching. DHT runs only while `dht` is on and a public torrent is active or a magnet is fetched; local discovery only while `lpd` is on and a public torrent is active; port mapping only while `portMapping` is on and the sockets are open. A `.torrent` waiting for `confirm` opens nothing.
- **No payload before `confirm`:** a `.torrent` is parsed and checked and stays out of the engine until `confirm`. A magnet link is added with every file at priority 0 (`default_dont_download`), with peer exchange and local discovery off, and with DHT only if `dht` is on; when the metadata arrives it is checked like a `.torrent` and the handle is dropped. `confirm` adds the torrent again with the user's priorities, save folder and flags. Private torrents get DHT, peer exchange and local discovery off for good.
- **Before any transfer** (`confirm` with `start`, `resume`, `resumeAll`, `restore` with `start`): `checkRelativePaths` walks the save folder without following links, then `prepareDirectories` creates the folders. `confirm` also refuses a selection that does not fit (`disk-full`).
- **Completion:** on libtorrent's finished alert the helper flushes the torrent's disk cache first, because libtorrent reports a piece finished before its blocks reach the file. Then `finalizeRelative` clears every execute bit and sets `com.apple.quarantine` on each selected file, and only then the `completed` event is written. If a file cannot be finalised (a volume without extended attributes), the event carries a `warning`. Until the flush is done the torrent still reports `downloading`.
- **Resume data:** `<profile-dir>/<tid>.resume`, mode 0600, written atomically at `confirm` (with the full metadata, so the original `.torrent` path is not needed), on pause and completion, every 30 s when changed, and at shutdown (up to 0.6 s). `restore` checks every file again (`checkInfoDict`, the info hash against the file name, an absolute save path). A torrent the user paused stays paused at `restore`.
- **Power:** an IOKit `PreventUserIdleSystemSleep` assertion named "M6E BitTorrent transfer" is held while a confirmed torrent is active and released when none is (and at exit).
- **Idle exit:** after `M6E_TORRENT_IDLE_SECONDS` with nothing pending, nothing active and no request.

## Not yet done (P8.5 and later)
- `setConfig` answers `unsupported` for a SOCKS5 `proxy`, an `ipFilterPath` and a `seedGoal` with a ratio or hours; the service then refuses to run with a proxy, which fails closed.
- `bindInterface` binds the listen and outgoing sockets; the kill-switch watchdog and the `interfaceDown` and `interfaceUp` events are a stub (`Engine::watchBoundInterface`), as are seeding goals (`Engine::applySeedGoals`). Without seeding goals a finished torrent seeds until paused.
- `setLimits` applies the global rates (Turtle mode switches between the two sets) and stores the queue sizes, but torrents are not auto-managed yet, so the queue does nothing. Peers on loopback and the local network are not rate-limited by libtorrent's default peer classes.
- No TLS: `https://` trackers and web seeds fail.
- The sandbox profile in `sandbox/` is not applied (see its README for what the service must provide first).

## Notes for testing
- Only the synthetic loopback fixtures in `fixtures/` (spec 8.9, D38). `tests/run_tests.py` is the conformance suite; `--slow --idle-seconds 5` includes the idle exit.
- libtorrent tries uTP before TCP, so a TCP-only peer such as the fixture seed is reached about 4 s after the tracker answers.
