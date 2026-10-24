#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Checks to run after merging upstream Firefox into M6E (P5.3, D9).
#
#   tools/m6e/merge/post-merge-checks.sh [--base REV] [--head REV]
#       [--upstream-new SHA] [--only a,b] [--skip a,b] [--list]
#
# --base is the M6E commit from before the merge (default HEAD, for a merge
# that is applied but not committed); --head is the commit after it (default:
# the working tree and index). --upstream-new is the upstream commit that was
# merged, which lets the plugin lists be compared with upstream's. --only and
# --skip take check names; --list prints them.
#
# Every check only reads (or works on a copy in a temporary directory) and
# reports "ok" or what needs attention. Lines starting with "note:" are
# information. Exit status is 1 if any check needs attention. The tree-wide
# #ifdef scan takes two to eight minutes: --skip ifdefs leaves it out. --head
# only changes the checks that compare two trees; the dry runs (search,
# strings, ifdefs, frontend, mozbuild) always look at the working tree.

set -o pipefail
export GIT_OPTIONAL_LOCKS=0

# What the dry runs of strip_ifdefs.py and strip_mozbuild.py report on the tree
# as it is. A merge may only lower these numbers; a higher one is a problem.
IFDEFS_UNPARSABLE_BASELINE=73
FRONTEND_UNPARSABLE_BASELINE=0
MOZBUILD_UNPARSABLE_BASELINE=8

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../../.." && pwd)"
cd "$top" || exit 2

base=HEAD
head=
upstream_new=
only=
skip=
list=
while [ $# -gt 0 ]; do
  case "$1" in
    --base | --head | --upstream-new | --only | --skip)
      [ $# -ge 2 ] || { echo "$1 needs a value" >&2; exit 2; }
      ;;
  esac
  case "$1" in
    --base) base="$2"; shift 2 ;;
    --head) head="$2"; shift 2 ;;
    --upstream-new) upstream_new="$2"; shift 2 ;;
    --only) only=",$2,"; shift 2 ;;
    --skip) skip=",$2,"; shift 2 ;;
    --list) list=1; shift ;;
    -h | --help) awk 'NR > 5 && /^#/ { sub(/^# ?/, ""); print; next } NR > 5 { exit }' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

git rev-parse --verify --quiet "$base^{commit}" >/dev/null || { echo "not a commit: $base" >&2; exit 2; }
[ -z "$head" ] || git rev-parse --verify --quiet "$head^{commit}" >/dev/null ||
  { echo "not a commit: $head" >&2; exit 2; }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/m6e-merge-checks.XXXXXX")" || exit 2
trap 'rm -rf "$scratch"' EXIT

helper() { python3 "$here/check-helpers.py" "$1" --base "$base" ${head:+--head "$head"} ${upstream_new:+--upstream-new "$upstream_new"}; }

# One pass over the working tree: "status NUL path NUL" for every file that differs.
# The helpers read the same list instead of asking git again.
git diff --name-status --no-renames -z "$base" ${head:+"$head"} -- >"$scratch/changes.z"
export M6E_MERGE_CHANGES="$scratch/changes.z"

# Files the merge added or modified, one per line, sorted.
changed_files() {
  tr '\0' '\n' <"$scratch/changes.z" | paste - - | awk -F'\t' '$1 ~ /^[AM]/ { print $2 }' | sort -u
}

check_conflicts() {
  git diff --name-only --diff-filter=U | sed 's/^/unresolved conflict in the index: /'
  { grep -E '\.(rej|orig)$' "$scratch/untracked.txt"; changed_files | grep -E '\.(rej|orig)$'; } |
    sort -u | sed 's/^/leftover from a failed apply: /'
  changed_files >"$scratch/changed.txt"
  if [ -s "$scratch/changed.txt" ]; then
    tr '\n' '\0' <"$scratch/changed.txt" |
      xargs -0 git grep -n -I -E '^(<<<<<<<|>>>>>>>)( |$)' ${head:+"$head"} -- 2>/dev/null |
      sed 's/^/conflict marker left: /'
  fi
  echo "note: $(wc -l <"$scratch/changed.txt" | tr -d ' ') files added or modified between the trees"
}

check_search() {
  local work="$scratch/search"
  mkdir -p "$work/tools/m6e/search" "$work/services/settings/dumps"
  cp "$top/tools/m6e/search/prune-search-config.py" "$work/tools/m6e/search/"
  cp -R "$top/services/settings/dumps/main" "$work/services/settings/dumps/main"
  if ! python3 "$work/tools/m6e/search/prune-search-config.py" >"$scratch/search.out" 2>&1; then
    sed 's/^/prune-search-config.py failed: /' "$scratch/search.out"
    return 1
  fi
  local dumps=services/settings/dumps/main f
  { (cd "$top/$dumps" && find . -type f) ; (cd "$work/$dumps" && find . -type f); } | sort -u |
    while read -r f; do
      cmp -s "$top/$dumps/$f" "$work/$dumps/$f" ||
        echo "prune-search-config.py would change $dumps/${f#./}"
    done
  sed 's/^/note: /' "$scratch/search.out"
}

check_strings() {
  python3 tools/m6e/branding/scan-strings.py >"$scratch/strings.out" 2>&1
  local summary
  summary="$(tail -1 "$scratch/strings.out")"
  if [ "$summary" != "0 strings in 0 files" ]; then
    echo "scan-strings.py found: $summary"
    head -40 "$scratch/strings.out"
  fi
}

check_fox_art() {
  helper fox-art
  echo "note: replace-fox-art.sh has no dry run and rewrites files, so this check reads instead;" \
    "run tools/m6e/branding/replace-fox-art.sh after fixing what it lists"
}

check_remote_settings() { helper remote-settings; }
check_gmp() { helper gmp-sources; }
check_doh() { helper doh; }
check_lint() { helper lint-paths; }
check_about_pages() { helper about-pages; }

check_tokens() {
  local css=toolkit/themes/shared/design-system/dist/tokens-shared.css
  local gen=toolkit/themes/shared/design-system/config/helpers/general/custom-file-header.mjs
  local jar=toolkit/themes/shared/desktop-jar.inc.mn
  show_file() { if [ -n "$head" ]; then git show "$head:$1" 2>/dev/null; else cat "$1" 2>/dev/null; fi; }
  has_text() { grep -qF -- "$2" < <(show_file "$1"); }
  has_text "$css" '@import url("chrome://global/skin/m6e-tokens.css");' ||
    echo "$css: the m6e-tokens.css import is gone"
  has_text "$gen" 'm6e-tokens.css' ||
    echo "$gen: no longer writes the import, so \`mach buildtokens\` would drop it"
  has_text "$jar" 'm6e-tokens.css' ||
    echo "$jar: m6e-tokens.css is no longer packaged"
  has_text toolkit/themes/shared/m6e-tokens.css ':root' ||
    echo "toolkit/themes/shared/m6e-tokens.css is missing or has no :root rules"
}

# Judges a dry run of strip_ifdefs.py or strip_mozbuild.py by the summary line it
# prints on stderr, because their exit status says nothing: strip_ifdefs.py exits 1
# on the files unifdef cannot parse. $1 is the tool, $2 its stderr file, $3 a sed
# expression that captures the number of files it could not process, $4 the number
# it could not process on the tree as it is. Returns 1 if the tool did not finish.
check_summary() {
  local count
  count="$(sed -nE "s/$3/\1/p" "$2" | tail -1)"
  if [ -z "$count" ]; then
    echo "$1 did not finish (the last line it printed: $(tail -1 "$2"))"
    return 1
  fi
  if [ "$count" -gt "$4" ]; then
    echo "$1 could not process $count files; $4 is the most that is expected"
    grep -E ': (unparsable|error)' "$2" | head -5 | sed 's/^/for example /'
  fi
  return 0
}

check_ifdefs() {
  # They also build for the wasm sandbox, so keep their non-macOS branches (P4.6c).
  local known="js/src/vm/JSONPrinter.h js/src/wasm/WasmSignalHandlers.cpp memory/mozalloc/mozalloc.cpp"
  nice -n 15 python3 tools/m6e/platform/strip_ifdefs.py --list >"$scratch/ifdefs.list" 2>"$scratch/ifdefs.err"
  check_summary strip_ifdefs.py "$scratch/ifdefs.err" \
    '^[0-9]+ candidate files, [0-9]+ [a-z ]+, ([0-9]+) errors$' "$IFDEFS_UNPARSABLE_BASELINE" || return 0
  local f
  while read -r f; do
    [ -n "$f" ] || continue
    case " $known " in *" $f "*) continue ;; esac
    echo "non-macOS #ifdef branches came back in $f (python3 tools/m6e/platform/strip_ifdefs.py --apply)"
  done <"$scratch/ifdefs.list"
  changed_files >"$scratch/changed.txt"
  grep ': error: unifdef:' "$scratch/ifdefs.err" | sed 's/: error:.*//' | sort -u >"$scratch/ifdefs.unparsable"
  comm -12 "$scratch/changed.txt" "$scratch/ifdefs.unparsable" |
    sed 's/$/ was changed by the merge and unifdef cannot parse it: strip its platform branches by hand/'
  echo "note: $(grep 'candidate files' "$scratch/ifdefs.err" | tail -1)" \
    "($IFDEFS_UNPARSABLE_BASELINE unparsable and 3 wasm-shared files are expected)"
}

check_frontend() {
  local f
  # The preprocessed files that P4.6d stripped (frontend-preprocessed.txt).
  python3 tools/m6e/platform/strip_ifdefs.py --frontend "$here/frontend-preprocessed.txt" --list \
    >"$scratch/frontend.list" 2>"$scratch/frontend.err"
  if check_summary strip_ifdefs.py "$scratch/frontend.err" \
    '^[0-9]+ candidate files, [0-9]+ [a-z ]+, ([0-9]+) errors$' "$FRONTEND_UNPARSABLE_BASELINE"; then
    while read -r f; do
      [ -n "$f" ] && echo "non-macOS #ifdef branches came back in $f" \
        "(python3 tools/m6e/platform/strip_ifdefs.py --frontend tools/m6e/merge/frontend-preprocessed.txt --apply)"
    done <"$scratch/frontend.list"
    echo "note: $(grep 'candidate files' "$scratch/frontend.err" | tail -1)"
  fi
  helper frontend-ifdefs
}

check_mozbuild() {
  python3 tools/m6e/platform/strip_mozbuild.py --list >"$scratch/mozbuild.list" 2>"$scratch/mozbuild.err"
  check_summary strip_mozbuild.py "$scratch/mozbuild.err" \
    '^[0-9]+ files, [0-9]+ [a-z ]+ \([0-9]+ conditions\), ([0-9]+) skipped$' "$MOZBUILD_UNPARSABLE_BASELINE" || return 0
  sed -E 's/^([0-9]+)\t(.*)$/\2: \1 non-macOS conditions came back (python3 tools\/m6e\/platform\/strip_mozbuild.py --apply)/' \
    "$scratch/mozbuild.list"
  changed_files >"$scratch/changed.txt"
  grep -E '^[^ ]+: (unparsable|error)' "$scratch/mozbuild.err" | sed 's/: .*//' | sort -u >"$scratch/mozbuild.unparsable"
  comm -12 "$scratch/changed.txt" "$scratch/mozbuild.unparsable" |
    sed 's/$/ was changed by the merge and strip_mozbuild.py cannot parse it: strip its platform conditions by hand/'
  echo "note: $(grep -E 'files, [0-9]+ ' "$scratch/mozbuild.err" | tail -1)" \
    "($MOZBUILD_UNPARSABLE_BASELINE unparsable files are expected)"
}

check_backends() {
  echo "note: not run here: it needs two configured trees, and a merge legitimately changes the backend"
  echo "note: use it to prove that a strip_*.py --apply or a hand edit left the macOS build unchanged:"
  echo "note:   git worktree add --detach /tmp/m6e-before <commit before the edit>"
  echo "note:   (cd /tmp/m6e-before && ./mach configure) && ./mach configure"
  echo "note:   python3 tools/m6e/platform/compare_backends.py /tmp/m6e-before . # expect 0 differ"
  echo "note:   git worktree remove --force /tmp/m6e-before"
}

check_nightly_prefs() {
  python3 "$here/new-nightly-prefs.py" --format checks --since "$base" ${head:+--head "$head"}
}

check_urls() {
  python3 "$here/new-urls.py" --format checks --base "$base" ${head:+--head "$head"}
}

check_clobber() {
  local diff
  diff="$(git diff -U0 "$base" ${head:+"$head"} -- CLOBBER | grep -E '^[+-][^+-]')"
  if [ -n "$diff" ]; then
    echo "CLOBBER changed, so the next build is a full clean build (P4.5: 16.5 minutes with LTO; with PGO about 1.5 hours):"
    echo "$diff"
  fi
}

# name|title|function
checks='
conflicts|Merge left no conflict markers or leftovers|check_conflicts
search|Search configuration is still Google and Bing only (prune-search-config.py, on a copy)|check_search
strings|No user-visible string names Firefox or Mozilla (scan-strings.py)|check_strings
fox-art|Fox artwork is still the M6E icon and new fox artwork is listed (fox-art.txt)|check_fox_art
remote-settings|Remote Settings allowlist still guards every collection (D2)|check_remote_settings
gmp|Plugin download lists are upstream'"'"'s and name only Google and Cisco (D10)|check_gmp
tokens|m6e-tokens.css import survives in tokens-shared.css|check_tokens
doh|DNS-over-HTTPS default provider is Cloudflare for Families (D25)|check_doh
ifdefs|Non-macOS #ifdef branches have not come back in C, C++ and Objective-C (strip_ifdefs.py, dry run)|check_ifdefs
frontend|Non-macOS #ifdef branches have not come back in preprocessed front-end files (strip_ifdefs.py --frontend, dry run)|check_frontend
mozbuild|Non-macOS moz.build conditions have not come back (strip_mozbuild.py, dry run)|check_mozbuild
backends|Build backend comparison (compare_backends.py, usage only)|check_backends
lint|Lint configs name no files that are gone|check_lint
nightly-prefs|Nightly-only prefs are handled in m6e.js (new-nightly-prefs.py)|check_nightly_prefs
about-pages|No new about: page is reachable from web content|check_about_pages
urls|No new Mozilla or unknown host in shipped code (new-urls.py)|check_urls
clobber|CLOBBER|check_clobber
'

if [ -n "$list" ]; then
  echo "$checks" | grep . | cut -d'|' -f1,2 | tr '|' '\t'
  exit 0
fi
for name in $(printf '%s' "$only$skip" | tr ',' ' '); do
  echo "$checks" | cut -d'|' -f1 | grep -qx "$name" || { echo "unknown check: $name (see --list)" >&2; exit 2; }
done

echo "M6E post-merge checks"
echo "  before: $base ($(git log -1 --format='%h %s' "$base" 2>/dev/null | cut -c1-70))"
echo "  after:  ${head:-working tree and index}"
echo "  upstream merged before this: $(grep -v '^#' "$here/upstream-base.txt" | head -1 | cut -c1-12)"
if [ ! -s "$scratch/changes.z" ]; then
  echo "  warning: no files differ between the two trees; has the merge been applied?"
fi
# The scan walks the whole tree (a minute on a busy machine), so it runs once.
git ls-files --others --exclude-standard >"$scratch/untracked.txt"
untracked="$(wc -l <"$scratch/untracked.txt" | tr -d ' ')"
[ "$untracked" -gt 0 ] && echo "  note: $untracked untracked files are not examined; stage the merge (git add -A <paths>) first"

failed=0
passed=0
while IFS='|' read -r name title fn; do
  [ -n "$name" ] || continue
  case "$only" in ?*) case "$only" in *",$name,"*) ;; *) continue ;; esac ;; esac
  case "$skip" in *",$name,"*) continue ;; esac
  echo
  echo "== $name: $title"
  out="$($fn 2>&1 </dev/null)"
  rc=$?
  notes="$(printf '%s\n' "$out" | grep '^note: ' | sed 's/^note: /   note: /')"
  problems="$(printf '%s\n' "$out" | grep -v '^note: ' | grep .)"
  if [ -n "$problems" ] || [ $rc -ne 0 ]; then
    failed=$((failed + 1))
    echo "   ATTENTION"
    [ -n "$problems" ] && printf '%s\n' "$problems" | sed 's/^/   - /'
    [ -z "$problems" ] && echo "   - the check exited with status $rc and printed nothing"
  else
    passed=$((passed + 1))
    echo "   ok"
  fi
  [ -n "$notes" ] && printf '%s\n' "$notes"
done <<<"$checks"

echo
echo "$passed ok, $failed need attention"
echo "After committing the merge, put the upstream commit in tools/m6e/merge/upstream-base.txt."
[ "$failed" -eq 0 ]
