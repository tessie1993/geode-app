#!/usr/bin/env bash
# Prints the release number N for a commit: pre-release N is versionName <base>.N, tag apk-v<base>.N.
#
#   tools/release-number.sh <base> <sha> reserve   merge-release.yml: highest existing N + 1
#   tools/release-number.sh <base> <sha>           the N this commit shipped as; fails when it never
#                                                  shipped, since any other number could repeat a versionCode
#
# N is global across every apk-v* tag, so changing the version base cannot restart the sequence or
# lower versionCode. Reserving pushes a lightweight tag before the build, and a push onto an existing
# tag fails, so two runs racing for the same N cannot both get it: the loser takes the next number.
# A re-run of the same commit gets the highest N already reserved for it. Annotated tags are compared
# by their peeled commit. Needs push access to origin; authentication comes from tools/ci-git-auth.sh.
set -euo pipefail
base=$1 sha=$2 mode=${3:-}
[[ "$sha" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::$sha is not a full commit SHA" >&2; exit 1; }

# Read the remote, not the local tag cache or the release count: releases may be deleted, and a
# failed run may have left a reserved tag without a release. ls-remote prints an annotated tag's
# object and its peeled commit separately; keep the peeled value for same-commit re-runs.
refs=$(git ls-remote --tags origin "refs/tags/apk-v*")
declare -A peeled=()
while read -r object ref; do
  [ -n "$ref" ] || continue
  if [[ "$ref" == *'^{}' ]]; then
    peeled["${ref:0:${#ref}-3}"]=$object
  fi
done <<< "$refs"

prefix="refs/tags/apk-v$base."
mine=0
max=0
while read -r object ref; do
  [ -n "$ref" ] || continue
  [[ "$ref" == refs/tags/apk-v* ]] || continue
  [[ "$ref" != *'^{}' ]] || continue
  suffix=${ref##*.}
  [[ "$suffix" =~ ^[0-9]+$ ]] || continue
  n=$((10#$suffix))
  if [ "$n" -gt "$max" ]; then max=$n; fi
  commit=${peeled[$ref]:-$object}
  if [[ "$ref" == "$prefix"* ]] && [ "$commit" = "$sha" ] && [ "$n" -gt "$mine" ]; then
    mine=$n
  fi
done <<< "$refs"

if [ "$mine" -gt 0 ]; then
  echo "$mine"
  exit 0
fi

if [ "$mode" != reserve ]; then
  # No guess: the latest number belongs to another commit, and Play refuses a versionCode it
  # has already been sent.
  echo "::error::$sha has no apk-v$base.N tag, so it has no release number. Run merge-release.yml on this commit first." >&2
  exit 1
fi

n=$((max + 1))
refusal=
for _ in $(seq 50); do
  if refusal=$(git push --quiet origin "$sha:refs/tags/apk-v$base.$n" 2>&1); then
    echo "$n"
    exit 0
  fi
  n=$((n + 1))
done
echo "::error::Could not reserve a release number for $sha (tried up to $n)." >&2
# A taken number says "already exists"; anything else here is why no number could be had.
printf 'The last push was refused with:\n%s\n' "$refusal" >&2
exit 1
