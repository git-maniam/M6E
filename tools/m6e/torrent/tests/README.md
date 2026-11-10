# m6e-torrent conformance tests

`run_tests.py` checks a helper against `../PROTOCOL.md` using only loopback fixtures from `../fixtures` (a tracker and a seed on 127.0.0.1; no real torrent, magnet link or tracker, spec 8.9). Python 3 standard library only; TAP output; exit status 1 on any failure, 2 if the helper cannot be started.

    python3 -I tools/m6e/torrent/tests/run_tests.py                      # tools/m6e/torrent/build/m6e-torrent
    python3 -I tools/m6e/torrent/tests/run_tests.py --helper "python3 browser/components/torrent/tests/xpcshell/service/fake_helper.py"
    python3 -I tools/m6e/torrent/tests/run_tests.py --helper "python3 -I tools/m6e/torrent/tests/ref_helper.py"

Options: `--only NAME` (repeatable), `--slow` (adds `idle_exit`, which waits for the 60 s idle exit; `--idle-seconds N` passes `M6E_TORRENT_IDLE_SECONDS=N`, a proposed variable the protocol does not define), `--keep`, `--verbose`.

What each test proves:
- `ready_first`, `one_reply_per_request`: the `ready` event is the first line, stdout holds only JSON lines, each request gets exactly one reply with its id.
- `bad_requests`, `not_found`, `error_codes_*`, `duplicates_and_pending`, `confirm_with_unusable_save_path`, `disk_full`: the documented error codes, including `duplicate` with `tid`, and `disk-full` on a 6 MB disk image (macOS). `engine-error` cannot be provoked and is only checked as a legal code. Reply codes outside the documented set fail the last test.
- `operations_shape`: result and `Status`, `Metadata` and `detail` shapes.
- `error_codes_unsafe_and_malformed`, `error_codes_size_and_depth`, `no_symlink_escape`, `magnet_cancel_and_hostile_name`: hostile input (`..`, absolute, empty, dot, NUL, colon, backslash, BEP 47 symlink, over 16 MiB, over-deep bencoding) is refused, nothing is written outside the save folder, and a symlink in the save folder is never followed.
- `download_file_and_verify`, `selective_priority_zero`, `magnet_metadata_only_then_download`: no payload is requested and no file is written before `confirm`; after it the torrent completes and every output file matches the fixture by SHA-256.
- `progress_batched`, `pause_and_resume`, `remove_with_and_without_delete`: progress events at most once a second and only while active; pause stops transfer; remove keeps or deletes the files and the torrent does not return after a restart.
- `shutdown_*`, `eof_*`: exit 0 within 2 s on `shutdown` and on stdin EOF, idle or mid-download.
- `killed_helper_resumes`: after SIGKILL mid-download, `restore` finds the torrent (paused with `start:false`), and with `start:true` it finishes with correct data.
- `idle_has_no_sockets`, `sockets_follow_active_torrents`: `lsof -nP -iTCP -iUDP -a -p PID` shows nothing before the first add or after the last removal, and `listening`/`closed` events match.

Against the fake helper (engine name starting with `fake`) the checks it cannot satisfy are reported as `# fake gap:` lines instead of failures, and tests that need real transfer skip or run their non-transfer part. A target that answers a request with the error code `unsupported` makes that test skip. `ref_helper.py` is a small Python helper that really transfers from the loopback swarm; it exists to prove the harness works end to end and is not shipped.
