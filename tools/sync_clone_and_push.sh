#!/bin/sh
# Run this ON a machine that holds a clone of this repo (e.g. the CM5 build
# host) to bring that clone onto the scrubbed branch and push from there.
#
# Why: the published branch had its history rewritten to drop a customer name
# and a device identity. A clone made BEFORE that rewrite still carries the old
# history, so a push from it would republish what was removed. This script
# syncs first, then pushes, and proves the result.
#
#   sh tools/sync_clone_and_push.sh
#   SENSITIVE=<string> sh tools/sync_clone_and_push.sh   # verify a rewrite removed <string>
#   BRANCH=<other-branch> sh tools/sync_clone_and_push.sh
#
# The string to search for is NEVER stored in this file: pass it in, or the
# check is skipped. That keeps the guard from becoming the leak.
set -eu

BRANCH="${BRANCH:-wifi-scan-fix-2026-10-15}"
SENSITIVE="${SENSITIVE:-}"

echo "=== clone: $(pwd) ==="
git rev-parse --is-inside-work-tree >/dev/null

echo "=== uncommitted work (must be empty before a reset) ==="
git status --short
if [ -n "$(git status --porcelain)" ]; then
  echo "STOP: uncommitted changes here. Commit or stash them first." >&2
  exit 1
fi
echo "(clean)"

echo "=== fetch the published branch ==="
git fetch origin "$BRANCH"
echo "published head: $(git rev-parse "origin/$BRANCH")"

echo "=== commits that exist only here (they would be lost by the reset) ==="
AHEAD=$(git log --oneline "origin/$BRANCH..HEAD" 2>/dev/null || true)
if [ -n "$AHEAD" ]; then
  echo "$AHEAD"
  echo "STOP: these are not on GitHub. Push or park them first." >&2
  exit 1
fi
echo "(none)"

echo "=== align this clone with the published branch ==="
git checkout -q -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"

if [ -n "$SENSITIVE" ]; then
  H=$(git log --all --format=%B | grep -c -i -- "$SENSITIVE" || true)
  echo "history occurrences of the given string before the push: $H"
fi

echo "=== push from THIS machine ==="
git push origin "$BRANCH"

echo "=== verify after the push ==="
if [ -n "$SENSITIVE" ]; then
  A=$(git log --all --format=%B | grep -c -i -- "$SENSITIVE" || true)
  T=$(git grep -c -i -I -e "$SENSITIVE" 2>/dev/null | wc -l)
  echo "history occurrences: $A   files in the tree with it: $T"
fi
echo "head: $(git log --oneline -1)"
echo "DONE"
