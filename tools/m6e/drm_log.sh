#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Start the installed M6E with media and DRM (Widevine) logging on, to find out
# why an encrypted video does not play. See tools/m6e/p6-reports/udemy-diagnosis.md.
#
#   tools/m6e/drm_log.sh [--net] [--no-sandbox] [path to M6E.app [log directory]]
#
# The app defaults to /Applications/M6E.app and the directory to ~/Desktop.
# Quit M6E first: a second start only opens a window in the running copy, which
# never sees these settings, so the script refuses to start then.
#
# Logs go to <dir>/m6e-drm-<date>-<time>.moz_log for the main process and
# <dir>/m6e-drm-<date>-<time>.child-<n>.moz_log for each child process (web
# content, media decoding, Widevine). Firefox adds the .child-<n> part itself
# (ipc/glue/GeckoChildProcessHost.cpp, GetChildLogName): <n> is a child number,
# not a process id, and no pid is added to the main log's name.
#
#   --net         also log network requests, DNS and DNS-over-HTTPS (larger)
#   --no-sandbox  also turn off the macOS sandbox of the content, media and
#                 Widevine processes. The sandbox may stop them writing their
#                 log files, so use this when only the main log shows up. It
#                 lowers security and can change whether a video plays: use it
#                 for this diagnosis only and start M6E normally afterwards.
#
# The app runs in the foreground: closing this window quits M6E.

app=/Applications/M6E.app
dir=$HOME/Desktop
net=0
nosandbox=0
npos=0

while [ $# -gt 0 ]; do
    case $1 in
        --net) net=1 ;;
        --no-sandbox) nosandbox=1 ;;
        -h|--help)
            echo "Usage: $0 [--net] [--no-sandbox] [path to M6E.app [log directory]]"
            exit 0
            ;;
        --*)
            echo "Unknown option: $1" >&2
            exit 2
            ;;
        *)
            npos=$((npos + 1))
            if [ "$npos" = 1 ]; then
                app=$1
            elif [ "$npos" = 2 ]; then
                dir=$1
            else
                echo "Too many arguments." >&2
                exit 2
            fi
            ;;
    esac
    shift
done

bin=$app/Contents/MacOS/m6e
if [ ! -x "$bin" ]; then
    echo "Cannot find M6E at $app (looked for $bin)." >&2
    echo "Pass the path to M6E.app as an argument." >&2
    exit 1
fi

if pgrep -x m6e >/dev/null 2>&1; then
    echo "M6E is already running. Quit it (Cmd-Q) and run this again." >&2
    exit 1
fi

if [ ! -d "$dir" ] || [ ! -w "$dir" ]; then
    echo "Cannot write to $dir." >&2
    exit 1
fi

stamp=$(date +%Y%m%d-%H%M%S)
base=$dir/m6e-drm-$stamp

modules=EME:5,EMEV:5,GMP:5,MediaDecoder:5,MediaSource:5,PlatformDecoderModule:5
if [ "$net" = 1 ]; then
    modules=$modules,nsHttp:3,nsHostResolver:5,TRR:5
fi

MOZ_LOG=timestamp,$modules
MOZ_LOG_FILE=$base
export MOZ_LOG MOZ_LOG_FILE

if [ "$nosandbox" = 1 ]; then
    MOZ_DISABLE_CONTENT_SANDBOX=1
    MOZ_DISABLE_GMP_SANDBOX=1
    MOZ_DISABLE_RDD_SANDBOX=1
    export MOZ_DISABLE_CONTENT_SANDBOX MOZ_DISABLE_GMP_SANDBOX MOZ_DISABLE_RDD_SANDBOX
fi

cat <<EOF
Starting M6E with logging on.
  Main log:   $base.moz_log
  Child logs: $base.child-<n>.moz_log
Play the video that fails, wait for the error, then quit M6E (Cmd-Q).
The logs grow quickly; do not leave M6E running for long.
EOF

"$bin"
status=$?

cat <<EOF

M6E has quit (exit code $status). Log files:
EOF
ls -l "$base"*.moz_log 2>/dev/null || echo "  none were written."

cat <<EOF

To share the end of a log (the last 500 lines), run for the main log and for
the biggest child log:
  tail -n 500 "$base.moz_log" > "$base-main-tail.txt"
  tail -n 500 "$base.child-<n>.moz_log" > "$base-child-tail.txt"
Files that mention EME or GMP lines near the error are the ones that matter:
  grep -l "EME" "$base"*.moz_log
The logs may contain page addresses and licence-request details; read them
before sharing.
EOF
exit "$status"
