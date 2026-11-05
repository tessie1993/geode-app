#!/usr/bin/env bash
# Fails when a PR grows the Kotlin in the engine. Owner rule (AGENTS.md, ADR 0006): Kotlin is the
# main-thread shell and Android glue only; engine logic, audio code, params, modulation, clocks,
# render-loop and export maths are C++ in core/. Until the port finishes, the Kotlin still in the
# engine paths below may only shrink: a PR's net added lines there must be <= 0.
#
# Exempt: the two JNI declaration files, which only list `external fun`s for new native calls.
# There is deliberately no other override: write the logic in core/ and call it through the ABI.
#
# Usage: git diff --numstat <base>...<head> | tools/engine-kotlin-gate.sh
set -euo pipefail

engine='^(engine/[^/]+/src/main/|app/src/main/java/dev/geode/(export|audio/dsp)/).*\.kt$'
exempt='/(GeodeNative|NativeViz)\.kt$'

net=0
report=""
while IFS=$'\t' read -r add del path; do
    [ -n "${path:-}" ] || continue
    # A rename shows as "old => new" or "dir/{a => b}/f"; keep the new side.
    path=$(printf '%s' "$path" | sed -E 's/\{[^}]* => ([^}]*)\}/\1/; s/.* => //')
    [[ "$path" =~ $engine ]] || continue
    [[ "$path" =~ $exempt ]] && continue
    [ "$add" = "-" ] && continue
    d=$((add - del))
    net=$((net + d))
    report+="$(printf '%+6d  %s' "$d" "$path")"$'\n'
done

if [ "$net" -gt 0 ]; then
    echo "::error title=Engine Kotlin grew::This PR adds $net net lines of Kotlin to the engine. Engine logic is C++ in core/ (AGENTS.md, ADR 0006); call it through core/api/geode_api.h instead."
    printf '%s' "$report"
    exit 1
fi
echo "Engine Kotlin gate passed (net $net lines in engine paths)."
[ -z "$report" ] || printf '%s' "$report"
