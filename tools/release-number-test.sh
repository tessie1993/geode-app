#!/usr/bin/env bash
# Regression test for release-number.sh, using a throwaway remote: no GitHub token or network.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/tools/release-number.sh"
TMP=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/release-number-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

git init --bare -q --initial-branch=main "$TMP/origin.git"
git init -q "$TMP/seed"
cd "$TMP/seed"
git config user.name Test
git config user.email test@example.invalid
git config core.autocrlf false
git remote add origin "$TMP/origin.git"

commit() {
  printf '%s\n' "$1" >> history.txt
  git add history.txt
  git commit -q -m "$1"
  git rev-parse HEAD
}

SHA1=$(commit one)
git push -q origin HEAD:main
[ "$(bash "$SCRIPT" 1.8.1 "$SHA1" reserve)" = 1 ]
[ "$(bash "$SCRIPT" 1.8.1 "$SHA1" reserve)" = 1 ]

SHA2=$(commit two)
git push -q origin HEAD:main
[ "$(bash "$SCRIPT" 1.8.1 "$SHA2" reserve)" = 2 ]

SHA3=$(commit three)
git push -q origin HEAD:main
git tag apk-v1.8.1.5 "$SHA2"
git push -q origin refs/tags/apk-v1.8.1.5
[ "$(bash "$SCRIPT" 1.8.1 "$SHA3" reserve)" = 6 ]

SHA4=$(commit four)
git push -q origin HEAD:main
if bash "$SCRIPT" 1.8.1 "$SHA4" >/dev/null 2>&1; then
  echo "an unshipped commit guessed a release number" >&2
  exit 1
fi

SHA5=$(commit five)
SHA6=$(commit six)
SHA7=$(commit seven)
SHA8=$(commit eight)
git push -q origin HEAD:main
shas=("$SHA5" "$SHA6" "$SHA7" "$SHA8")
for i in 1 2 3 4; do
  git clone -q "$TMP/origin.git" "$TMP/clone$i"
  (
    cd "$TMP/clone$i"
    bash "$SCRIPT" 1.8.1 "${shas[$((i - 1))]}" reserve > "$TMP/out$i"
  ) &
done
for pid in $(jobs -pr); do wait "$pid"; done
mapfile -t nums < <(cat "$TMP"/out{1,2,3,4} | sort -n)
[ "${#nums[@]}" -eq 4 ]
[ "${nums[0]}" -eq 7 ]
[ "${nums[1]}" -eq 8 ]
[ "${nums[2]}" -eq 9 ]
[ "${nums[3]}" -eq 10 ]
[ "$(bash "$SCRIPT" 1.8.1 "$SHA5" reserve)" = "$(cat "$TMP/out1")" ]

SHA9=$(commit nine)
git push -q origin HEAD:main
git tag apk-v9.9.9.99 "$SHA9"
git tag -a apk-v1.8.1.20 "$SHA9" -m annotated
git push -q origin refs/tags/apk-v9.9.9.99 refs/tags/apk-v1.8.1.20
SHA10=$(commit ten)
git push -q origin HEAD:main
[ "$(bash "$SCRIPT" 1.8.1 "$SHA10" reserve)" = 21 ]

echo "release-number tests passed: reuse, gaps, concurrency, unrelated and annotated tags"
