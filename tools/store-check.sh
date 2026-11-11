#!/usr/bin/env bash
# Checks the Play listing text in fastlane/metadata/android/<locale>/ against Google Play's
# limits, and against the owner's rule that the listing makes no tempo claim.
#
#   tools/store-check.sh
#
# Limits (characters, not bytes), read 2026-11:
#   title 30, short description 80, full description 4000
#     support.google.com/googleplay/android-developer/answer/9859152
#   release notes 500 per language
#     support.google.com/googleplay/android-developer/answer/9859348
# Play's metadata policy (answer/9898842) also bans emoji, ALL CAPS words that are not the brand,
# and ranking or price words in the title; the title check below covers the ones a script can see.
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0
count() { python3 -c 'import sys; print(len(open(sys.argv[1], encoding="utf-8").read().rstrip("\n")))' "$1"; }
limit() {
    local file="$1" max="$2" n
    [ -f "$file" ] || { echo "MISSING $file"; fail=1; return; }
    n=$(count "$file")
    if [ "$n" -gt "$max" ]; then echo "TOO LONG $file: $n > $max"; fail=1; else echo "ok $file: $n / $max"; fi
}
for dir in fastlane/metadata/android/*/; do
    limit "${dir}title.txt" 30
    limit "${dir}short_description.txt" 80
    limit "${dir}full_description.txt" 4000
    for notes in "${dir}"changelogs/*.txt; do limit "$notes" 500; done
    if grep -nEi '\b(bpm|tempo|beats? per minute)\b' "${dir}"*.txt "${dir}"changelogs/*.txt; then
        echo "TEMPO CLAIM in $dir (the listing makes none)"; fail=1
    fi
    if grep -nEi '\b(best|#1|top|free|new|sale|download)\b' "${dir}title.txt"; then
        echo "TITLE uses a word Play's metadata policy bans there"; fail=1
    fi
done
exit "$fail"
