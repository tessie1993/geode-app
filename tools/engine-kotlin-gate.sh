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
# Exempt: the JNI declaration file, which only lists `external fun`s for new native calls.
# There is deliberately no other override: write the logic in core/ and call it through the ABI.
#
# The engine modules also have a list (`allowed` below): every Kotlin file still under
# engine/*/src/main/, the debt left to port (docs/agents/engine-kotlin-audit.md). A file there that is
# not on the list fails, so no new one can start; an entry whose file is gone fails, so the list stays
# the honest remainder; and a PR that adds an entry fails, so the list only shrinks. Remove an entry in
# the commit that deletes or moves its file. It ends as the declarations file alone.
#
# Usage: git diff --numstat -M <base>...<head> | tools/engine-kotlin-gate.sh
#        tools/engine-kotlin-gate.sh --files [root]      the tree against the list (a root given: no stale check)
#        git diff <base>...<head> -- tools/engine-kotlin-gate.sh | tools/engine-kotlin-gate.sh --list-grew
# Self-test: the "Self-test the engine Kotlin gate" step of .github/workflows/arch-map.yml.
set -euo pipefail

S='engine/scenes/src/main/kotlin/dev/geode'
C='engine/audio-core/src/main/kotlin/dev/geode/engine'
allowed=(
    "engine/audio-android/src/main/kotlin/dev/geode/engine/audioandroid/PcmTap.kt"
    "$C/audio/SampleRing.kt"
    "$C/audio/SoundCursor.kt"
    "$C/audio/SoundTuning.kt"
    "$C/bridge/GeodeNative.kt"
    "$S/analysis/PlaybackMath.kt"
    "$S/analysis/SearchMatcher.kt"
    "$S/render/Adsr.kt"
    "$S/render/BlendMode.kt"
    "$S/render/EventRecipe.kt"
    "$S/render/FramePacer.kt"
    "$S/render/GyroLook.kt"
    "$S/render/Lfo.kt"
    "$S/render/MilkPresetMemory.kt"
    "$S/render/PerformanceSettings.kt"
    "$S/render/Reactions.kt"
    "$S/render/SceneFactory.kt"
    "$S/render/TouchField.kt"
    "$S/render/TransitionCatalog.kt"
    "$S/render/TransitionStyle.kt"
    "$S/render/UnderlayBlend.kt"
    "$S/render/VisualizerRenderer.kt"
    "$S/render/VisualizerView.kt"
    "$S/render/bridge/ModConfigCodec.kt"
    "$S/render/bridge/NativeViz.kt"
    "$S/render/bridge/SceneParamsCodec.kt"
    "$S/render/bridge/StyleSchedule.kt"
    "$S/render/fluid/FluidQuality.kt"
    "$S/render/fluid/FluidShaderTemplate.kt"
    "$S/render/look/LookSnapshot.kt"
    "$S/render/scene/BuildStyleReactionsUseCase.kt"
    "$S/render/scene/MilkStarterPack.kt"
    "$S/render/scene/ParamFormat.kt"
    "$S/render/scene/ParamKeys.kt"
    "$S/render/scene/ParamSchema.kt"
    "$S/render/scene/SceneParams.kt"
    "$S/util/BestEffort.kt"
)

if [ "${1:-}" = "--files" ]; then
    root="${2:-.}"
    bad=0
    while IFS= read -r f; do
        rel="${f#"$root"/}"
        listed=0
        for a in "${allowed[@]}"; do
            if [ "$a" = "$rel" ]; then listed=1; break; fi
        done
        if [ "$listed" = 0 ]; then
            echo "::error file=$rel,title=New engine Kotlin::$rel is Kotlin in an engine module and not on the gate's list. Engine code is C++ in core/; the UI and Android shell live in :app."
            bad=1
        fi
    done < <(find "$root/engine" -path '*/src/main/*' -name '*.kt' 2>/dev/null | sort)
    if [ -z "${2:-}" ]; then
        for a in "${allowed[@]}"; do
            if [ ! -f "$a" ]; then
                echo "::error title=Stale engine Kotlin entry::$a is gone: remove its entry from tools/engine-kotlin-gate.sh."
                bad=1
            fi
        done
    fi
    [ "$bad" = 0 ] && echo "Engine Kotlin list passed (${#allowed[@]} files left to port)."
    exit "$bad"
fi

if [ "${1:-}" = "--list-grew" ]; then
    added=$(grep -E '^\+[^+]' | grep -E '(\$[SC]|engine/[^/ ]+/src/main)/[^ ]*\.kt' || true)
    if [ -n "$added" ]; then
        echo "::error title=Engine Kotlin list grew::The list of Kotlin left in the engine modules may only shrink. Write the code in core/ instead."
        printf '%s\n' "$added"
        exit 1
    fi
    echo "Engine Kotlin list did not grow."
    exit 0
fi

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
exempt='/GeodeNative\.kt$'

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
