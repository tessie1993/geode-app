#!/usr/bin/env bash
# CI's `./gradlew ktlintCheck`, without Gradle, on the Kotlin files changed against a base ref.
#
#   tools/ktlint-changed.sh [-F] [base]   base defaults to origin/main; -F also fixes what it can
#   tools/ktlint-changed.sh --fetch       only download the ktlint jar (tools/agent-setup.sh)
#   tools/ktlint-changed.sh --all         every file the Gradle tasks lint (about a minute)
#
# Same rules as CI: the ktlint Gradle plugin 14.2.0 (gradle/libs.versions.toml) runs engine
# 1.5.0 when no version is set, its `android` flag reaches no rule, and there is no baseline
# or filter, so the rules are the standard set under .editorconfig. Bump VERSION and SHA256
# together with the plugin. It lints what the Gradle tasks do: each module's src/ and the
# *.kts in the module's folder; the root and build-logic scripts are not linted in CI either.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

VERSION=1.5.0
SHA256=a27854622198800f50971a049dcdba38f2105d47e7e7258786c7d28045c5735d
JAR="${XDG_CACHE_HOME:-$HOME/.cache}/geode-tools/ktlint-cli-$VERSION-all.jar"
URL="https://repo1.maven.org/maven2/com/pinterest/ktlint/ktlint-cli/$VERSION/ktlint-cli-$VERSION-all.jar"
LINTED='^(app|engine/[^/]+)/(src/.*\.kts?|[^/]*\.kts)$'

if ! echo "$SHA256  $JAR" | sha256sum -c --status 2>/dev/null; then
    mkdir -p "$(dirname "$JAR")"
    curl -fsSL -o "$JAR.part" "$URL"
    echo "$SHA256  $JAR.part" | sha256sum -c --status || { echo "ktlint jar: sha256 mismatch" >&2; exit 2; }
    mv "$JAR.part" "$JAR"
fi
[ "${1:-}" = "--fetch" ] && exit 0

format=()
[ "${1:-}" = "-F" ] && { format=(-F); shift; }
if [ "${1:-}" = "--all" ]; then
    files="$(git ls-files | grep -E "$LINTED" || true)"
else
    base="$(git merge-base "${1:-origin/main}" HEAD)"
    # Committed, staged, unstaged and untracked, less what was deleted.
    files="$({ git diff --name-only --diff-filter=ACMR "$base"; git ls-files --others --exclude-standard; } |
        grep -E "$LINTED" | sort -u || true)"
fi
[ -n "$files" ] || { echo "ktlint: no changed Kotlin files"; exit 0; }

# JAVA_TOOL_OPTIONS (the agent proxy's) only adds a banner line; ktlint needs no network.
echo "$files" | tr '\n' '\0' |
    JAVA_TOOL_OPTIONS='' xargs -0 java -jar "$JAR" --relative "${format[@]}" 2> >(grep -v '^Picked up' >&2)
echo "ktlint: clean ($(echo "$files" | wc -l) files)"
