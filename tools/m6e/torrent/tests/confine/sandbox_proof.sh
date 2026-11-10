#!/bin/bash
# Runs sandbox_probe under the real profile and checks what it may and may not
# do. Usage: sandbox_proof.sh <folder with sandbox_probe>
# Local only: the probe opens loopback sockets and resolves "localhost".
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
profile="$here/../../sandbox/m6e-torrent.sb"
bin="$(cd "$1" && pwd -P)"
probe="$bin/sandbox_probe"

work="$(mktemp -d "${TMPDIR:-/tmp}/m6e-sandbox.XXXXXX")"
work="$(cd "$work" && pwd -P)"
trap '/bin/rm -rf "$work"' EXIT

allowed="$work/download"
second="$work/download2"
profiledir="$work/profile/torrents"
inbox="$work/inbox"
outside="$work/outside"
mkdir -p "$allowed" "$second" "$profiledir" "$inbox" "$outside"
echo "inbox file" > "$inbox/in.torrent"
echo "secret" > "$outside/secret.txt"
ln -s "$outside" "$allowed/escape"
printf '#!/bin/sh\n' > "$allowed/tool.sh"
chmod 755 "$allowed/tool.sh"

home_marker="${HOME:?}/.m6e-sandbox-probe-$$"
home_file=""
for candidate in .zshrc .zprofile .bash_profile .gitconfig; do
  if [ -f "$HOME/$candidate" ]; then
    home_file="$HOME/$candidate"
    break
  fi
done

params=(
  -D "HELPER=$probe"
  -D "EXEC_HELPER=YES"
  -D "DOWNLOAD_1=$allowed"
  -D "DOWNLOAD_2=$second"
  -D "DOWNLOAD_3=/nonexistent/3"
  -D "DOWNLOAD_4=/nonexistent/4"
  -D "TORRENTS_DIR=$work/profile/torrents"
  -D "INBOX_DIR=$inbox"
)

pass=0
fail=0

sandboxed() {
  /usr/bin/sandbox-exec -f "$profile" "${params[@]}" "$probe" "$@"
}

selfsandboxed() {
  "$probe" selfsandbox "$profile" "HELPER=$probe" "EXEC_HELPER=NO" "DOWNLOAD_1=$allowed" \
    "DOWNLOAD_2=$second" "DOWNLOAD_3=/nonexistent/3" \
    "DOWNLOAD_4=/nonexistent/4" "TORRENTS_DIR=$work/profile/torrents" \
    "INBOX_DIR=$inbox" -- "$@"
}

expect_ok() {
  local label="$1"
  shift
  if out="$("$@" 2>&1)"; then
    pass=$((pass + 1))
    echo "ok   $label"
  else
    fail=$((fail + 1))
    echo "FAIL $label (should succeed): $out"
  fi
}

expect_denied() {
  local label="$1"
  shift
  if out="$("$@" 2>&1)"; then
    fail=$((fail + 1))
    echo "FAIL $label (should be denied)"
  else
    pass=$((pass + 1))
    echo "ok   $label: ${out%%$'\n'*}"
  fi
}

echo "== sandbox-exec -f $(basename "$profile")"
expect_ok "start and stat" sandboxed stat "$allowed"
expect_ok "write in download folder" sandboxed write "$allowed/ok.txt"
expect_ok "write in second download folder" sandboxed write "$second/ok.txt"
expect_ok "write in profile torrents folder" sandboxed write "$profiledir/x.resume"
expect_ok "confined write creating folders" sandboxed confined "$allowed" "t/a/b/file.bin"
expect_ok "layout check" sandboxed check "$allowed" "t/a/b/other.bin"
expect_ok "read inbox torrent" sandboxed read "$inbox/in.torrent"
expect_ok "list download folder" sandboxed list "$allowed"
expect_ok "finalize: clear exec bit, set quarantine" sandboxed finalize "$allowed" "tool.sh"
expect_ok "loopback tcp, udp, interface list" sandboxed net
expect_ok "route socket" sandboxed route
expect_ok "resolve localhost" sandboxed dns localhost
expect_ok "power assertion" sandboxed power
expect_ok "system configuration store (configd)" sandboxed configd

expect_denied "write outside" sandboxed write "$outside/new.txt"
expect_denied "write through symlink out of download folder" sandboxed write "$allowed/escape/new.txt"
expect_denied "write into read-only inbox" sandboxed write "$inbox/new.txt"
expect_denied "write in /tmp" sandboxed write "/tmp/m6e-sandbox-probe-$$"
expect_denied "write in HOME" sandboxed write "$home_marker"
expect_denied "write beside the download folder" sandboxed write "$work/sibling.txt"
expect_denied "read outside" sandboxed read "$outside/secret.txt"
expect_denied "list HOME" sandboxed list "$HOME"
expect_denied "list /Users" sandboxed list /Users
if [ -n "$home_file" ]; then
  expect_denied "read a file in HOME" sandboxed read "$home_file"
fi
expect_denied "read /etc/passwd" sandboxed read /etc/passwd
expect_denied "read the login keychain folder" sandboxed list "$HOME/Library/Keychains"
expect_denied "exec /bin/echo" sandboxed exec /bin/echo
expect_denied "spawn /bin/echo" sandboxed spawn /bin/echo
expect_denied "spawn /bin/sh" sandboxed spawn /bin/sh
expect_denied "fork" sandboxed fork
expect_denied "confined write refuses symlink out" sandboxed confined "$allowed" "escape/new.txt"
expect_denied "finalize outside" sandboxed finalize "$outside" "secret.txt"

echo "== sandbox_init in the process (no exec allowed at all)"
expect_ok "write in download folder" selfsandboxed write "$allowed/self.txt"
expect_ok "confined write" selfsandboxed confined "$allowed" "self/dir/file"
expect_ok "net" selfsandboxed net
expect_denied "write outside" selfsandboxed write "$outside/self.txt"
expect_denied "list HOME" selfsandboxed list "$HOME"
expect_denied "exec /bin/echo" selfsandboxed exec /bin/echo
expect_denied "exec the helper itself" selfsandboxed exec "$probe"
expect_denied "spawn /bin/echo" selfsandboxed spawn /bin/echo
expect_denied "fork" selfsandboxed fork

if [ -e "$home_marker" ]; then
  /bin/rm -f "$home_marker"
  fail=$((fail + 1))
  echo "FAIL the sandbox let a file be written in HOME"
fi
if [ -e "$outside/new.txt" ] || [ -e "$work/sibling.txt" ]; then
  fail=$((fail + 1))
  echo "FAIL a file appeared outside the download folder"
fi
if [ "$(/usr/bin/stat -f %Lp "$allowed/tool.sh")" != "644" ]; then
  fail=$((fail + 1))
  echo "FAIL tool.sh mode is $(/usr/bin/stat -f %Lp "$allowed/tool.sh")"
fi
if ! /usr/bin/xattr -p com.apple.quarantine "$allowed/tool.sh" > /dev/null 2>&1; then
  fail=$((fail + 1))
  echo "FAIL tool.sh has no com.apple.quarantine"
else
  echo "info quarantine: $(/usr/bin/xattr -p com.apple.quarantine "$allowed/tool.sh")"
fi

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
