#!/usr/bin/env bash
# Fails when the musical intelligence tables are not what their scripts write: tools/musical_intelligence.py and
# tools/music_emotion.py generate core/sound/ScaleTable.inc, core/sound/EmotionTable.inc and
# docs/musical-intelligence/*.csv, so a hand edit, or a script edit not run, shows here. Runs the scripts on a copy;
# no network.
#
# Usage: tools/music-tables-gate.sh [repo-root]
#        tools/music-tables-gate.sh --self-test
set -euo pipefail

copy() {  # copy <from-root> <to-root>: the scripts and what they write
    mkdir -p "$2/tools" "$2/core/sound" "$2/docs/musical-intelligence"
    cp "$1/tools/musical_intelligence.py" "$1/tools/music_emotion.py" "$2/tools/"
    cp "$1/core/sound/ScaleTable.inc" "$1/core/sound/EmotionTable.inc" "$2/core/sound/"
    cp "$1"/docs/musical-intelligence/*.csv "$2/docs/musical-intelligence/"
}

if [ "${1:-}" = "--self-test" ]; then
    self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
    repo=$(cd "$(dirname "$0")/.." && pwd)
    t=$(mktemp -d)
    trap 'rm -rf "${t:?}"' EXIT
    copy "$repo" "$t"
    bash "$self" "$t" >/dev/null || { echo "::error::music tables gate self-test: clean tree failed"; exit 1; }
    plant() {  # plant <file> <sed expression>: the edit must fail the gate
        cp "$t/$1" "$t/keep"
        sed -i "$2" "$t/$1"
        cmp -s "$t/$1" "$t/keep" && { echo "::error::music tables gate self-test: the plant changed nothing: $1"; exit 1; }
        if bash "$self" "$t" >/dev/null; then echo "::error::music tables gate self-test: passed: $1: $2"; exit 1; fi
        mv "$t/keep" "$t/$1"
    }
    plant core/sound/EmotionTable.inc '0,/0\.[0-9]/s/0\.\([0-9]\)/0.9\1/'
    plant core/sound/ScaleTable.inc '0,/0x/s/0x/0x1/'
    plant docs/musical-intelligence/emotions.csv '2s/,/,x/'
    plant tools/music_emotion.py 's/^MODE_WEIGHT = 0.75/MODE_WEIGHT = 0.7/'
    echo "music tables gate self-test passed"
    exit 0
fi

root=$(cd "${1:-.}" && pwd)
t=$(mktemp -d)
trap 'rm -rf "${t:?}"' EXIT
mkdir -p "$t/tools" "$t/core/sound"
cp "$root/tools/musical_intelligence.py" "$root/tools/music_emotion.py" "$t/tools/"
(cd "$t" && python3 -B tools/musical_intelligence.py)

stale=()
while IFS= read -r f; do
    rel=${f#"$t"/}
    cmp -s "$f" "$root/$rel" || stale+=("$rel")
done < <(find "$t/core" "$t/docs" -type f | sort)
written=$(find "$t/core" "$t/docs" -type f | wc -l)
[ "$written" -ge 7 ] || { echo "::error title=Music tables::the scripts wrote $written files, not the 7 expected"; exit 1; }

if [ "${#stale[@]}" -gt 0 ]; then
    echo "::error title=Music tables stale::Not what tools/musical_intelligence.py writes. Edit the scripts and run: python3 tools/musical_intelligence.py"
    printf '%s\n' "${stale[@]}"
    exit 1
fi
echo "Music tables gate passed: the scale and emotion tables are their scripts' output."
