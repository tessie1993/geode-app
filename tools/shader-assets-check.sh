#!/usr/bin/env bash
# The GLSL lives in core/viz/styles/glsl/<group>/shaders and app/build.gradle.kts merges every group into the
# APK's flat assets/shaders/. This confirms the merged list is the one the folder had before the move.
#
#   tools/shader-assets-check.sh tree [BASE]      no Gradle: BASE (default origin/main) had app/src/main/assets/shaders/<name>;
#                                                 here every <name> is in exactly one group, with the same bytes, and nothing else is
#   tools/shader-assets-check.sh apk OLD.apk NEW.apk   after `./gradlew :app:assembleDebug` on main and on this branch: the two APKs hold
#                                                 the same assets/shaders/ names with the same CRCs
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-}" in
  tree)
    base="${2:-origin/main}"
    old=$(git ls-tree -r "$base" -- app/src/main/assets/shaders | awk '{print $3 "  " $4}' | sed 's|app/src/main/assets/shaders/||' | sort -k2)
    new=$(for f in core/viz/styles/glsl/*/shaders/*; do printf '%s  %s\n' "$(git hash-object "$f")" "$(basename "$f")"; done | sort -k2)
    dups=$(printf '%s\n' "$new" | awk '{print $2}' | sort | uniq -d)
    [ -z "$dups" ] || { echo "in more than one group: $dups" >&2; exit 1; }
    diff <(printf '%s\n' "$old") <(printf '%s\n' "$new") && echo "shader-assets-check: $(printf '%s\n' "$new" | wc -l) names, same bytes as $base"
    ;;
  apk)
    [ $# -eq 3 ] || { echo "usage: $0 apk OLD.apk NEW.apk" >&2; exit 2; }
    list() { unzip -v "$1" 'assets/shaders/*' | awk '$NF ~ /^assets\/shaders\// {print $7 "  " $NF}' | sort -k2; }
    diff <(list "$2") <(list "$3") && echo "shader-assets-check: $(list "$3" | wc -l) packaged shaders, same names and CRCs"
    ;;
  *)
    echo "usage: $0 tree [BASE] | apk OLD.apk NEW.apk" >&2
    exit 2
    ;;
esac
