#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Start the installed M6E with media and DRM (Widevine) logging on, to find out
# why an encrypted video does not play. See tools/m6e/p6-reports/udemy-diagnosis.md.
#
#   tools/m6e/drm_log.sh [--net] [--max-mb <n>] [--no-sandbox] [path to M6E.app [log directory]]
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
#   --max-mb <n>  cap each log file at about <n> megabytes, keeping the end
#                 (default 100); there is one file per process
#   --no-sandbox  also turn off the macOS sandbox of the content, media and
#                 Widevine processes. The sandbox may stop them writing their
#                 log files, so use this when only the main log shows up. It
#                 lowers security and can change whether a video plays: use it
#                 for this diagnosis only and start M6E normally afterwards.
#
# Verbose logs grow fast: keep the video short and quit M6E right after the
# error. With the cap, the disk use is at most about <n> MB times the number of
# files (a handful), and more with --net.
#
# The app runs in the foreground: closing this window quits M6E.

set -eu

app=/Applications/M6E.app
dir=$HOME/Desktop
maxmb=100
net=0
nosandbox=0
npos=0

while [ $# -gt 0 ]; do
    case $1 in
        --net) net=1 ;;
        --no-sandbox) nosandbox=1 ;;
        --max-mb)
            if [ $# -lt 2 ]; then
                echo "--max-mb needs a number." >&2
                exit 2
            fi
            case $2 in
                ''|*[!0-9]*) echo "--max-mb needs a whole number." >&2; exit 2 ;;
            esac
            maxmb=$2
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--net] [--max-mb <n>] [--no-sandbox] [path to M6E.app [log directory]]"
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
    modules=$modules,nsHttp:4,nsHostResolver:5
fi

MOZ_LOG=timestamp,append,maxsize:$maxmb,$modules
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

status=0
"$bin" || status=$?

cat <<EOF

M6E has quit (exit code $status). Log files:
EOF
ls -l "$base"*.moz_log 2>/dev/null || echo "  none were written."

echo
echo "To make a small file with the last 500 lines of every log, paste this one"
echo "line into Terminal and press Enter; the files end in -tail.txt:"
echo
printf 'for f in "%s"*.moz_log; do tail -n 500 "$f" > "${f%%.moz_log}-tail.txt"; done\n' "$base"
echo
echo "The logs may contain page addresses, cookies and licence-request details."
echo "Read them before sharing and send them privately, not in a public issue."
exit "$status"
