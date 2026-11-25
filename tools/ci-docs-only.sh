#!/usr/bin/env bash
# Reads a pull request's changed paths on stdin, one per line, and prints "true" when every one is
# a document no build or test reads, "false" otherwise (an empty list is "false").
# A document: anything under docs/, or a *.md file in the repository root.
# Not a document: docs/privacy-policy.md, which PrivacyPolicyTest compares with the app's copy.
# build-lint.yml and native-host-tests.yml skip their heavy job on "true"; arch-map.yml never skips.
#
# Usage: git diff --name-only --no-renames <base> <head> | tools/ci-docs-only.sh
#        tools/ci-docs-only.sh --self-test
set -euo pipefail

docs_only() {
    local seen=false path
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        seen=true
        case "$path" in
            docs/privacy-policy.md) echo false; return ;;
            docs/*) ;;
            */*) echo false; return ;;
            *.md) ;;
            *) echo false; return ;;
        esac
    done
    echo "$seen"
}

if [ "${1:-}" = --self-test ]; then
    expect() {
        local want=$1 got
        shift
        got=$(printf '%s\n' "$@" | docs_only)
        [ "$got" = "$want" ] || { echo "::error::ci-docs-only self-test: wanted $want, got $got for: $*"; exit 1; }
    }
    expect true AGENTS.md
    expect true docs/agents/tests.md docs/site/index.html README.md
    expect false ''
    expect false docs/privacy-policy.md
    expect false docs/agents/tests.md app/src/main/assets/privacy_policy.md
    expect false core/README.md
    expect false THIRD_PARTY_NOTICES
    expect false .github/workflows/build-lint.yml
    expect false docs/agents/tests.md core/viz/params/Params.cpp
    expect false gradle.properties
    echo "ci-docs-only self-test passed"
    exit 0
fi

docs_only
