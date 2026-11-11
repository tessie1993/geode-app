#!/usr/bin/env bash
# Fails when a tempo bias is written back into the app (owner's rule: no bias on BPM, ever).
#
# The tempo comes from the music alone. Nothing may prefer, weight toward, centre on or assume a
# tempo, and where no tempo has been heard the answer is "unknown" (bpm 0, no beat, clocks free-run
# in seconds), never a stand-in number. The bias came back four times, each time copied from the
# beat-tracking literature or a library default (Ellis 2007's log-normal prior at 120 BPM, librosa's
# start_bpm=120, "fall back to 120 BPM"), so this gate reads the engine, the shell and the docs for
# the shapes it took:
#
#   prior      a tempo prior or tempo weighting curve (kPriorBpm, "tempo prior", log-normal, Rayleigh)
#   preferred  a preferred or favoured tempo (preferredBpm, preferenceOctaves, start_bpm)
#   default    a default, fallback or assumed tempo (periodOrDefault, "fallback BPM", LEGACY_MORPH_BPM)
#   literal    a BPM variable or field set to a number other than 0 (lastBpm_ = 120.0f)
#   120        "120 BPM" written anywhere but a test's stimulus
#
# Usage: tools/no-bpm-bias-gate.sh [repo-root]. Host tests (core/tests, */src/test, androidTest) and
# tools/glslcheck are not read: a test may play any tempo it likes as a stimulus. The detector's
# search limits (Rhythm::kFastestBpm, kSlowestBpm) are limits, flat inside, and pass, as does the
# sound frame's slot index (SoundFrameLayout.BPM).
# There is deliberately no override. If the gate is wrong about a line, reword the line.
set -euo pipefail

root=${1:-.}
cd "$root"

paths=()
for p in core/sound core/viz core/signal core/studio core/audio core/api core/scene core/library \
         app/src/main/java app/src/main/cpp app/src/main/assets engine \
         docs .claude/skills tools AGENTS.md CLAUDE.md GLOSSARY.md README.md CHANGELOG.md; do
    [ -e "$p" ] && paths+=("$p")
done
[ ${#paths[@]} -eq 0 ] && { echo "No-BPM-bias gate: nothing to read under $root."; exit 0; }

prior='(tempo|bpm)[ _-]*(prior|weighting)|prior[ _-]*(tempo|bpm)|PriorBpm|PriorOctaves|log-?(normal|gaussian)|rayleigh[ -]*(weight|window|curve|prior|distribution)'
preferred='prefer(red|ence)?[ _-]*(tempo|bpm|octaves)|(tempo|bpm)[ _-]*prefer|start_?bpm|prefer[a-z]* [0-9]+ ?(-|–|to|\.\.) ?[0-9]+ ?bpm|favou?r(s|ed)?[ a-z]*\b(tempo|bpm)\b'
default='(default|fallback|assumed|nominal|typical|legacy)[ _-]*(morph[ _-]*)?(tempo|bpm)|(tempo|bpm)[ _-]*(default|fallback)|periodOrDefault|(fall(s|ing)?[ -]?back|defaults?) to [0-9]+ ?bpm'
literal='bpm[a-z_]*[[:space:]]*(:[[:space:]]*[a-z]+[[:space:]]*)?=[[:space:]]*[1-9][0-9.]*f?[[:space:]]*[;,)]?[[:space:]]*(//.*)?$'
onetwenty='(^|[^0-9.])120(\.0+)?f?[ -]?bpm|bpm[ =:]*120([^0-9]|$)'

hits=$(grep -rnEi --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.inc' --include='*.kt' \
        --include='*.glsl' --include='*.frag' --include='*.vert' --include='*.md' --include='*.py' \
        --include='*.mjs' --include='*.html' --include='*.txt' --include='*.milk' --include='*.patch' \
        -e "$prior" -e "$preferred" -e "$default" -e "$literal" -e "$onetwenty" "${paths[@]}" 2>/dev/null \
    | grep -vE '^(tools/no-bpm-bias-gate\.sh|tools/glslcheck/|[^:]*/src/(test|androidTest)/|core/tests/)' \
    | grep -vE 'k(Fastest|Slowest)Bpm|/SoundFrameLayout\.kt:[0-9]+: *const val BPM = [0-9]+$' \
    | grep -vE '^(AGENTS\.md|\.claude/skills/[^:]*):[0-9]+:.*no-bpm-bias-gate' || true)

if [ -n "$hits" ]; then
    echo "::error title=Tempo bias::A tempo prior, preferred tempo or default BPM is written here. The tempo comes from the music alone; unknown stays unknown (tools/no-bpm-bias-gate.sh)."
    printf '%s\n' "$hits"
    exit 1
fi
echo "No-BPM-bias gate passed: no tempo prior, preferred tempo or default BPM."
