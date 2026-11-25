#!/usr/bin/env bash
# Fails when the sound analysis reaches outside its module. core/sound is a library of its own
# (geode_sound in core/CMakeLists.txt): the engine, its detectors, its thread and the reads of its
# published hops. It may include its own headers and the PCM ring it reads, nothing else in core/:
# no renderer, no player, no C interface, no stage. Anything it needs from above is handed to it.
#
# Usage: tools/sound-module-gate.sh [repo-root]
#        tools/sound-module-gate.sh --self-test
set -euo pipefail

if [ "${1:-}" = "--self-test" ]; then
    self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
    t=$(mktemp -d)
    trap 'rm -rf "${t:?}"' EXIT
    mkdir -p "$t/core/sound/hop"
    printf '%s\n' '#include "sound/Fft.hpp"' '#include <vector>' > "$t/core/sound/Clean.cpp"
    printf '%s\n' '#include "audio/ingest/PcmRing.hpp"' > "$t/core/sound/hop/Thread.hpp"
    bash "$self" "$t" >/dev/null || { echo "::error::sound module gate self-test: clean tree failed"; exit 1; }
    plant() {
        printf '%s\n' "$1" > "$t/core/sound/Leak.hpp"
        if bash "$self" "$t" >/dev/null; then echo "::error::sound module gate self-test: passed: $1"; exit 1; fi
        rm "$t/core/sound/Leak.hpp"
    }
    plant '#include "viz/modulation/Reactions.hpp"'
    plant '#include "api/geode_api.h"'
    plant '#include "audio/dsp/GainStage.hpp"'
    plant '#include "stage/StageCore.hpp"'
    echo "sound module gate self-test passed"
    exit 0
fi

root=${1:-.}
cd "$root"

# Every quoted include that names a core folder, less its own and the ring.
hits=$(grep -rnE '#include "[a-z_]+/' --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.inc' core/sound 2>/dev/null \
    | grep -vE '#include "(sound/|audio/ingest/PcmRing\.hpp")' || true)

if [ -n "$hits" ]; then
    echo "::error title=Sound module reaches out::core/sound may include only core/sound and audio/ingest/PcmRing.hpp. Hand it what it needs instead."
    printf '%s\n' "$hits"
    exit 1
fi
echo "Sound module gate passed: core/sound includes only itself and the PCM ring."
