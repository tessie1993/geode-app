#!/usr/bin/env bash
# Regex fragments deliberately put a character class immediately after a variable; ShellCheck
# otherwise reads $name[ as an array expansion.
# shellcheck disable=SC1087
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
#   standin    a number put in for a missing or low tempo: bpm ?: 93f, bpm ?? 93, bpm || 93, bpm or 93,
#              ${BPM:-93}, bpm > 0 ? bpm : 93, bpm <= 0 ? 93 : bpm, max(bpm, 60), maxOf(60f, bpm),
#              bpm.coerceAtLeast(60f). Tests included.
#   onetwenty  the old centre as a tempo anywhere, tests included: "120 BPM", bpm = 120, a beat of
#              60.0 / 120.0 s or 0.5 s. A test plays other tempos (77, 93, 141, 174), so nothing
#              passes because of a hidden pull to 120. A 120 that is no tempo (fps, Hz, ms) passes.
#              A 120 passed to a tempo carrier also fails, named or not: brace or paren initialisation
#              of a type or callee that carries a tempo (Song{120.0}, const Song s{120.0},
#              checkGroove({"x", 120.0, ...}), Tempo(120f), bpm{120}, clicks/metronome/clockAt/musicAt
#              (..., 120, ...)) and TapAlong's frame(t, phase, 120, ...). Any 120 among such a
#              carrier's own arguments is read as its tempo: give a time there in other units.
#
# Usage: tools/no-bpm-bias-gate.sh [repo-root]. The default and literal shapes skip host tests (core/tests,
# */src/test, androidTest) and tools/glslcheck, where a stimulus may set any tempo it plays. The
# detector's search limits (Rhythm::kFastestBpm, kSlowestBpm) are limits, flat inside, and pass.
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
carrier='([a-z_]*(song|groove)s?|clicks?|metronome|clockat|musicat|[a-z_]*(tempo|bpm)[a-z_]*)'
args="[({]+([^;{}()]*[^0-9.a-z_])?$n120$end"
onetwenty+="|\\b$carrier$args|\\b$carrier[[:space:]]+[a-z_][a-z0-9_]*(\\[[0-9]*\\])?([[:space:]]*=[[:space:]]*)?$args"
onetwenty+="|\\.frame\\([^;,()]*,[^;,()]*,[[:space:]]*$n120$end"
num='0*[1-9][0-9]*(\.[0-9]*)?f?'
tv='(bpm|tempo)[a-z_]*'
standin="$tv[]).)]*[[:space:]]*(\\?:|\\?\\?|\\|\\||[[:space:]]or)[[:space:]]*$num$end|\\\$\\{$tv:?-[[:space:]]*$num"
standin+="|$tv[^;?]*[^?]\\?[^;?:]*:[[:space:]]*$num$end|$tv[^;?]*[^?]\\?[[:space:]]*$num[[:space:]]*:"
standin+="|\\b(max|fmaxf?|maxof)[[:space:]]*(<[a-z ]+>)?[[:space:]]*\\(([^;()]*$tv[[:space:]]*,[[:space:]]*$num[[:space:]]*\\)|[[:space:]]*$num[[:space:]]*,[^;()]*$tv)"
standin+="|$tv\\)?\\.coerceatleast\\([[:space:]]*$num"

types=(--include='*.cpp' --include='*.hpp' --include='*.h' --include='*.inc' --include='*.kt' --include='*.kts'
       --include='*.glsl' --include='*.frag' --include='*.vert' --include='*.md' --include='*.py' --include='*.mjs'
       --include='*.js' --include='*.html' --include='*.txt' --include='*.patch' --include='*.sh'
       --include='*.yml' --include='*.yaml' --include='*.json' --include='*.csv' --include='*.tsv'
       --include='*.xml' --include='*.java' --include='*.toml' --include='*.cmake')

bias=$(grep -rnEi "${types[@]}" -e "$default" -e "$literal" "${paths[@]}" 2>/dev/null \
    | grep -vE '^[^:]*/src/(test|androidTest)/|^(core/tests|tools/glslcheck)/' || true)
centre=$(grep -rnEi "${types[@]}" -e "$prior" -e "$preferred" -e "$standin" -e "$onetwenty" "${paths[@]}" 2>/dev/null || true)

hits=$(printf '%s\n%s\n' "$bias" "$centre" | sed '/^$/d' | sort -u \
    | grep -vE '^(tools/no-bpm-bias-gate\.sh|\.github/workflows/arch-map\.yml):' \
    | grep -vE 'k(Fastest|Slowest)Bpm' \
    | grep -vE '^(AGENTS\.md|\.claude/skills/[^:]*):[0-9]+:.*no-bpm-bias-gate' || true)

if [ -n "$hits" ]; then
    echo "::error title=Tempo bias::A tempo prior, preferred tempo, default BPM or a 120 BPM is written here. The tempo comes from the music alone; unknown stays unknown (tools/no-bpm-bias-gate.sh)."
    printf '%s\n' "$hits"
    exit 1
fi
echo "No-BPM-bias gate passed: no tempo prior, preferred tempo, default BPM or 120 BPM."
