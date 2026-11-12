#!/usr/bin/env bash
# Checks the Play listing text in fastlane/metadata/android/<locale>/ against Google Play's
# limits and metadata policy, the old product name, and the owner's rule that the listing makes
# no tempo claim.
#
#   tools/store-check.sh
#
# Limits (characters, not bytes), read 2026-11:
#   title 30, short description 80, full description 4000
#     support.google.com/googleplay/android-developer/answer/9859152
#   release notes 500 per language
#     support.google.com/googleplay/android-developer/answer/9859348
# Play's metadata policy (answer/9898842) also bans emoji, ALL CAPS used for emphasis, and
# ranking, price or Play-programme words in the title; the checks below cover what a script can see.
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
    if grep -nEi '\b(best|#1|top|free|new|sale|download|popular|editor)\b' "${dir}title.txt"; then
        echo "TITLE uses a word Play's metadata policy bans there"; fail=1
    fi
    # Ranking and award words anywhere in the listing.
    if grep -nEi '\b(best|#1|top[- ]rated|award|editor.s choice|number one|no\. ?1)\b' "${dir}"*.txt "${dir}"changelogs/*.txt; then
        echo "RANKING CLAIM in $dir"; fail=1
    fi
    # Owner's decisions (2026-11-11): strobe, flash, pulse and shake effects are retired, there is
    # no tap tempo and Studio has no video editor, so the listing names none of them. "Can flash"
    # in the photosensitivity warning is a warning, not an effect, and stays.
    if grep -nEi '\b(strobes?|flash effects?|pulse effects?|screen shake|tap tempo|video editor|edit(ing)? (your )?videos?|timeline)\b' "${dir}"*.txt "${dir}"changelogs/*.txt; then
        echo "RETIRED OR ABSENT FEATURE named in $dir"; fail=1
    fi
    # The product is BeatRealms 3D; the old name must not reach the store.
    if grep -nEi 'muz ?viz|studio pro' "${dir}"*.txt "${dir}"changelogs/*.txt; then
        echo "OLD NAME in $dir"; fail=1
    fi
    # Emoji and shouting: words of four or more capitals, bar the format names the text uses.
    if python3 - "${dir}" <<'PY'
import pathlib, re, sys
d = pathlib.Path(sys.argv[1])
allowed = {"HEVC", "HDMI"}
bad = False
for f in sorted([*d.glob("*.txt"), *d.glob("changelogs/*.txt")]):
    for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
        if re.search("[\U0001F000-\U0001FAFF\u2600-\u27BF]", line):
            print(f"EMOJI {f}:{n}: {line}"); bad = True
        for w in re.findall(r"\b[A-Z]{4,}\b", line):
            if w not in allowed:
                print(f"ALL CAPS {f}:{n}: {w}"); bad = True
sys.exit(1 if bad else 0)
PY
    then :; else fail=1; fi
done
exit "$fail"
