#!/bin/sh
# M6E P4.8e: clang-tidy security checks on one first-party source file.
# Needs obj-m6e/compile_commands.json (./mach build-backend -b CompileDB).
# Usage: tools/m6e/security-tidy.sh <source file>; the log goes to
# artifacts/P4.8e-tidy/.
f="$1"
out="artifacts/P4.8e-tidy/$(echo "$f" | tr / _).log"
CHECKS='-*,clang-analyzer-core.*,clang-analyzer-cplusplus.*,clang-analyzer-security.*,clang-analyzer-unix.*,bugprone-use-after-move,bugprone-dangling-handle,bugprone-sizeof-expression,bugprone-sizeof-container,bugprone-string-constructor,bugprone-suspicious-memset-usage,bugprone-undefined-memory-manipulation,bugprone-not-null-terminated-result,bugprone-signed-char-misuse,bugprone-misplaced-widening-cast'
/opt/homebrew/opt/llvm/bin/clang-tidy -p obj-m6e --quiet --header-filter="^$" --checks="$CHECKS" "$f" > "$out" 2>&1
echo "$? $f"
