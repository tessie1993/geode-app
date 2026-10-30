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
