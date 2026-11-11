#!/usr/bin/env bash
# Fails when a tempo bias is written back into the app (owner's rule: no bias on BPM, ever).
#
# The tempo comes from the music alone. Nothing may prefer, weight toward, centre on or assume a
# tempo, and where no tempo has been heard the answer is "unknown" (bpm 0, no beat, clocks free-run
# in seconds), never a stand-in number. The bias came back four times, each time copied from the
# beat-tracking literature or a library default (Ellis 2007's log-normal prior, librosa's start_bpm,
# "fall back to a typical tempo"), so this gate reads the engine, the shell and the docs for the
# shapes it took:
#
#   prior      a tempo prior or tempo weighting curve (kPriorBpm, "tempo prior", log-normal, Rayleigh)
#   preferred  a preferred or favoured tempo (preferredBpm, preferenceOctaves, start_bpm)
#   default    a default, fallback or assumed tempo (periodOrDefault, "fallback BPM", LEGACY_MORPH_BPM)
#   literal    a BPM variable or field set to a number other than 0 (lastBpm_ = 93.0f)
#   onetwenty  the old centre as a tempo anywhere, tests included: "120 BPM", bpm = 120, a beat of
#              60.0 / 120.0 s or 0.5 s. A test plays other tempos (77, 93, 141, 174), so nothing
#              passes because of a hidden pull to 120. A 120 that is no tempo (fps, Hz, ms) passes.
#
# Usage: tools/no-bpm-bias-gate.sh [repo-root]. The first four shapes skip host tests (core/tests,
# */src/test, androidTest) and tools/glslcheck, where a stimulus may set any tempo it plays. The
# detector's search limits (Rhythm::kFastestBpm, kSlowestBpm) are limits, flat inside, and pass, as
# does the sound frame's slot index (SoundFrameLayout.BPM).
# There is deliberately no override. If the gate is wrong about a line, reword the line.
set -euo pipefail

root=${1:-.}
cd "$root"

paths=()
for p in core app engine docs tools .claude/skills .github AGENTS.md CLAUDE.md GLOSSARY.md README.md CHANGELOG.md; do
    [ -e "$p" ] && paths+=("$p")
done
[ ${#paths[@]} -eq 0 ] && { echo "No-BPM-bias gate: nothing to read under $root."; exit 0; }

prior='(tempo|bpm)[ _-]*(prior|weighting)|prior[ _-]*(tempo|bpm)|PriorBpm|PriorOctaves|log-?(normal|gaussian)|rayleigh[ -]*(weight|window|curve|prior|distribution)'
preferred='prefer(red|ence)?[ _-]*(tempo|bpm|octaves)|(tempo|bpm)[ _-]*prefer|start_?bpm|prefer[a-z]* [0-9]+ ?(-|–|to|\.\.) ?[0-9]+ ?bpm|favou?r(s|ed)?[ a-z]*\b(tempo|bpm)\b'
default='(default|fallback|assumed|nominal|typical|legacy)[ _-]*(morph[ _-]*)?(tempo|bpm)|(tempo|bpm)[ _-]*(default|fallback)|periodOrDefault|(fall(s|ing)?[ -]?back|defaults?) to [0-9]+ ?bpm'
literal='bpm[a-z_]*[[:space:]]*(:[[:space:]]*[a-z]+[[:space:]]*)?=[[:space:]]*[1-9][0-9.]*f?[[:space:]]*[;,)]?[[:space:]]*(//.*)?$'
n120='120(\.0*)?f?'
end='([^0-9.]|$)'
onetwenty="(^|[^0-9.])$n120[ -]?(bpm|b\\.p\\.m|beats? (per|a) min)|(bpm|tempo)[a-z_]*[]\"' ]*[=:(,][[:space:]]*$n120$end|tempo (of|at|is) $n120$end"
onetwenty+="|(^|[^0-9.])120\\.0*f?[[:space:]]*/[[:space:]]*60\\.0*f?$end|(^|[^0-9.])60\\.0*f?[[:space:]]*/[[:space:]]*120\\.0*f?$end"
onetwenty+="|beat[_ ]?(seconds|secs|period|length)[a-z_]*[[:space:]]*[=:][[:space:]]*0?\\.50*f?$end"
onetwenty+="|(^|[^0-9.])$n120[[:space:]]*,.*(//|#).*\\bbpm\\b"

types=(--include='*.cpp' --include='*.hpp' --include='*.h' --include='*.inc' --include='*.kt' --include='*.kts'
       --include='*.glsl' --include='*.frag' --include='*.vert' --include='*.md' --include='*.py' --include='*.mjs'
       --include='*.js' --include='*.html' --include='*.txt' --include='*.milk' --include='*.patch' --include='*.sh'
       --include='*.yml' --include='*.yaml' --include='*.json' --include='*.csv' --include='*.tsv')

bias=$(grep -rnEi "${types[@]}" -e "$prior" -e "$preferred" -e "$default" -e "$literal" "${paths[@]}" 2>/dev/null \
    | grep -vE '^[^:]*/src/(test|androidTest)/|^(core/tests|tools/glslcheck)/' || true)
centre=$(grep -rnEi "${types[@]}" -e "$onetwenty" "${paths[@]}" 2>/dev/null || true)

hits=$(printf '%s\n%s\n' "$bias" "$centre" | sed '/^$/d' | sort -u \
    | grep -vE '^(tools/no-bpm-bias-gate\.sh|\.github/workflows/arch-map\.yml):' \
    | grep -vE 'k(Fastest|Slowest)Bpm|/SoundFrameLayout\.kt:[0-9]+: *const val BPM = [0-9]+$' \
    | grep -vE '^(AGENTS\.md|\.claude/skills/[^:]*):[0-9]+:.*no-bpm-bias-gate' || true)

if [ -n "$hits" ]; then
    echo "::error title=Tempo bias::A tempo prior, preferred tempo, default BPM or a 120 BPM is written here. The tempo comes from the music alone; unknown stays unknown (tools/no-bpm-bias-gate.sh)."
    printf '%s\n' "$hits"
    exit 1
fi
echo "No-BPM-bias gate passed: no tempo prior, preferred tempo, default BPM or 120 BPM."
