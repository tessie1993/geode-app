#!/usr/bin/env bash
# Fails when code below the UI layer names the UI layer (AGENTS.md, "Where the code breaks the
# architecture guide"; ADR 0001 step 1). Only `ui` itself and `di`, which wires everything, may
# refer to `dev.geode.ui`. Everything else would have to drag `ui` along when it moves into a
# :core module, so the dependency has to point down, never up.
#
# Usage: tools/layer-gate.sh [repo-root]. Scans Kotlin sources under app/src/main and engine/.
# There is deliberately no override: move the type down instead (to dev.geode.model if it is
# pure, dev.geode.data if it touches storage).
set -euo pipefail

root=${1:-.}
cd "$root"

hits=$(grep -rnE '\bdev\.geode\.ui\b' --include='*.kt' app/src/main/java engine 2>/dev/null \
    | grep -vE '^app/src/main/java/dev/geode/(ui|di)/' || true)

if [ -n "$hits" ]; then
    echo "::error title=Layer rule broken::Code outside ui/ and di/ refers to dev.geode.ui. Move the type down (dev.geode.model or dev.geode.data)."
    printf '%s\n' "$hits"
    exit 1
fi
echo "Layer gate passed: nothing outside ui/ and di/ refers to dev.geode.ui."

# One owner of the prefs files (#1009, PR 2): `GeodePrefsFiles` names every file the app keeps and
# GeodeContainer builds it once per process; everything else reads `context.geodeContainer.prefsFiles`.
# Only the data layer and the container may open a prefs file or build the registry.
prefs_hits=$(grep -rnE 'getSharedPreferences\(|GeodePrefsFiles\(' --include='*.kt' app/src/main/java 2>/dev/null \
    | grep -vE '^app/src/main/java/dev/geode/(data/|GeodeContainer\.kt:)' || true)

if [ -n "$prefs_hits" ]; then
    echo "::error title=Prefs file opened outside data/::Read the file through context.geodeContainer.prefsFiles (add it to GeodePrefsFiles if it is new)."
    printf '%s\n' "$prefs_hits"
    exit 1
fi
echo "Prefs gate passed: only data/ and GeodeContainer open a prefs file."
