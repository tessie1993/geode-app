#!/usr/bin/env bash
# Fails when a diff changes the project's structure but not AGENTS.md (see its
# "Keeping this map current" section). Reads `git diff --name-status -M` on stdin.
#
# Structure: settings.gradle.kts, any build.gradle.kts, any CMakeLists.txt, and
# any file added, deleted or renamed under app/src/main/java/ or engine/. A plain
# edit inside those trees is not structure. There is deliberately no override.
set -euo pipefail

structural=()
map_touched=0
while IFS=$'\t' read -r status path rename_to; do
    [ -n "$status" ] || continue
    for p in "$path" ${rename_to:+"$rename_to"}; do
        [ "$p" = AGENTS.md ] && map_touched=1
        case "$p" in
            settings.gradle.kts | build.gradle.kts | */build.gradle.kts | CMakeLists.txt | */CMakeLists.txt)
                structural+=("$status $p") ;;
            app/src/main/java/* | engine/*)
                case "$status" in A* | D* | R*) structural+=("$status $p") ;; esac ;;
        esac
    done
done

if [ "${#structural[@]}" -gt 0 ] && [ "$map_touched" -eq 0 ]; then
    echo "::error title=Architecture map not updated::This PR changes structure but not AGENTS.md. Update the map in the same PR."
    printf '  %s\n' "${structural[@]}"
    exit 1
fi
echo "Architecture map gate passed (${#structural[@]} structural change(s), AGENTS.md touched: $map_touched)."
