#!/usr/bin/env bash
# Prints the release number N for a commit: pre-release N is versionName <base>.N, tag apk-v<base>.N.
#
#   tools/release-number.sh <base> <sha> reserve   ship-apk.yml: N = releases so far + 1
#   tools/release-number.sh <base> <sha>           release.yml: the N this commit shipped as
#
# Reserving pushes the tag before the build, and a push onto an existing tag fails, so two runs
# racing for the same N cannot both get it: the loser takes the next number. A re-run of the same
# commit gets the N it already reserved. Needs gh (GH_TOKEN) and push access to origin.
set -euo pipefail
base=$1 sha=$2 mode=${3:-}

git fetch --quiet --force origin "refs/tags/apk-v$base.*:refs/tags/apk-v$base.*"
mine=$(git tag --points-at "$sha" --list "apk-v$base.*" | sed "s/^apk-v$base\.//" | grep -E '^[0-9]+$' | sort -n | tail -n 1 || true)
if [ -n "$mine" ]; then
  echo "$mine"
  exit 0
fi

count=$(gh release list --limit 10000 --json tagName --jq length)
if [ "$mode" != reserve ]; then
  echo "::warning::$sha has no apk-v$base.N tag; using the latest release number, $count." >&2
  echo "$count"
  exit 0
fi

n=$((count + 1))
for _ in $(seq 50); do
  if git push --quiet origin "$sha:refs/tags/apk-v$base.$n" 2>/dev/null; then
    echo "$n"
    exit 0
  fi
  n=$((n + 1))
done
echo "::error::Could not reserve a release number for $sha (tried up to $n)." >&2
exit 1
