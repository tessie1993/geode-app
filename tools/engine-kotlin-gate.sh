#!/usr/bin/env bash
# Fails when a PR grows the Kotlin in the engine. Owner rule (AGENTS.md, ADR 0003, ADR 0006): Kotlin
# is the main-thread shell and Android glue only; engine logic, audio code, params, modulation,
# clocks, render-loop and export maths are C++ in core/. Until the port finishes, the Kotlin still in
# the watched paths below may only shrink: a PR's net added lines across them must be <= 0. A UI or
# glue edit in a watched file still lands when the PR removes as much as it adds there (move the
# logic it needs into core/ and call it through core/api/geode_api.h).
#
# Watched (paths from the repo root; A = app/src/main/java/dev/geode):
#   engine/*/src/main/                 the engine modules
#   A/export/, A/audio/dsp/            export and playback DSP
#   Spec 3.0 (#455, #461), where engine logic must never start in Kotlin:
#   - Record and the timeline:         A/ui/RecordController.kt and A/record/ (the live recording's
#                                      shell; its clock is core/output/Recorder, its frame tap
#                                      core/viz/frame/RecordTap), and the Studio timeline
#                                      A/editor/{Timeline,Keyframes,AnimatableParams}.kt (target:
#                                      core/signal/timeline). A/ui/TakeController.kt and
#                                      A/data/PerformanceTake.kt are gone (#1035); they stay listed
#                                      only until that PR lands, so deleting them counts against
#                                      the Record shell that replaces them. Drop them after.
#   - the Scene model:                 A/scene/ (model types only; the Scene itself is geode::scene)
#   - glTF and pack import:            A/gltf/, any *Gltf*.kt or *Glb*.kt, any *Pack*Import*.kt
#                                      (MilkPackImporter today); import is C++ (cgltf, meshoptimizer,
#                                      Basis Universal)
#   - the render job:                  A/ui/ExportController.kt, the whole file: the render job sits
#                                      beside its UI state there (target: core/output)
# Not watched on purpose: the pack store's networking and billing, storage (RecordFiles,
# RecordPrefs, EditorProjectStore), the Studio project list (StudioProject) and every screen.
#
# Exempt: the two JNI declaration files, which only list `external fun`s for new native calls.
# There is deliberately no other override: write the logic in core/ and call it through the ABI.
#
# Usage: git diff --numstat -M <base>...<head> | tools/engine-kotlin-gate.sh
# Self-test: the "Self-test the engine Kotlin gate" step of .github/workflows/arch-map.yml.
set -euo pipefail

A='app/src/main/java/dev/geode'
watched=(
    'engine/[^/]+/src/main/.*'
    "$A/(export|audio/dsp)/.*"
    "$A/ui/RecordController"
    "$A/ui/TakeController"
    "$A/data/PerformanceTake"
    "$A/editor/(Timeline|Keyframes|AnimatableParams)"
    "$A/record/.*"
    "$A/scene/.*"
    "$A/gltf/.*"
    "$A/(.*/)?[^/]*(Gltf|Glb)[^/]*"
    "$A/(.*/)?[^/]*Pack[^/]*Import[^/]*"
    "$A/ui/ExportController"
)
engine="^($(IFS='|'; printf '%s' "${watched[*]}"))\.kt$"
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
    echo "::error title=Engine Kotlin grew::This PR adds $net net lines of Kotlin to the engine paths. Engine logic is C++ in core/ (AGENTS.md, ADR 0006); call it through core/api/geode_api.h instead."
    printf '%s' "$report"
    exit 1
fi
echo "Engine Kotlin gate passed (net $net lines in engine paths)."
[ -z "$report" ] || printf '%s' "$report"
