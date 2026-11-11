#!/usr/bin/env bash
# Fails when a `uses:` in .github/workflows is not pinned to a full 40-hex commit SHA (#880).
# A tag or branch is mutable: a moved or compromised one would run in a job that can write to the
# repo. Local actions and reusable workflows (`./...`) are exempt. Dependabot keeps the pins fresh.
#
# Usage: tools/actions-pin-gate.sh [repo-root]. There is deliberately no override: pin the action.
set -euo pipefail

root=${1:-.}
cd "$root"

hits=$(grep -rnE '^[[:space:]]*(-[[:space:]]+)?uses:' --include='*.yml' --include='*.yaml' .github/workflows 2>/dev/null \
    | grep -vE 'uses:[[:space:]]*"?\./' \
    | grep -vE 'uses:[[:space:]]*"?[^[:space:]@"]+@[0-9a-f]{40}"?([[:space:]]+#.*)?$' || true)

if [ -n "$hits" ]; then
    echo "::error title=Unpinned action::A uses: in .github/workflows is not pinned to a full 40-character commit SHA. Pin it and keep the version in a trailing comment."
    printf '%s\n' "$hits"
    exit 1
fi
echo "Actions pin gate passed: every uses: is a full commit SHA."
