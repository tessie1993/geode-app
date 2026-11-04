#!/usr/bin/env bash
# Installs the host tools a Claude cloud session uses on this repo, and the plugins
# .claude/settings.json enables. Idempotent: safe to run at the start of every session.
#
# Cloud environment setup script (it starts one directory above the repo):
#   cd geode-app && bash tools/agent-setup.sh
#
# None of this is needed to build the app. CI builds it; these tools only let an agent
# check work on the host before pushing.
set -uo pipefail

cd "$(dirname "$0")/.."

step() { printf '\n== %s\n' "$1"; }

# clangd reads the native core through compile_flags.txt; glslc runs tools/check_shaders.py
# (CI's shader gate); the GLES and EGL headers let clangd parse the GL files.
step "host packages: clangd, glslc, GLES and EGL headers"
if command -v apt-get >/dev/null 2>&1; then
    missing=()
    for pkg in clangd glslc libgles-dev libegl-dev; do
        dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        apt-get install -y -q "${missing[@]}" >/dev/null 2>&1 ||
            { apt-get update -q >/dev/null 2>&1 && apt-get install -y -q "${missing[@]}" >/dev/null 2>&1; } ||
            echo "warning: could not install ${missing[*]}"
    fi
fi

# Google's Android CLI (the android-cli skill drives it). The installer only downloads one
# binary into ~/.local/bin and adds that folder to PATH in the shell profile.
step "Android CLI"
if ! command -v android >/dev/null 2>&1 && [ ! -x "$HOME/.local/bin/android" ]; then
    curl -fsSL https://dl.google.com/android/cli/latest/linux_x86_64/install.sh | bash >/dev/null 2>&1 ||
        echo "warning: Android CLI install failed"
fi

# The plugins themselves. Claude Code also offers these from .claude/settings.json; installing
# here means a fresh container has them before the first prompt.
step "Claude Code plugins (project scope)"
if command -v claude >/dev/null 2>&1 && command -v jq >/dev/null 2>&1; then
    jq -r '.extraKnownMarketplaces | to_entries[] | .value.source.repo' .claude/settings.json |
        while read -r repo; do claude plugin marketplace add "$repo" --scope project >/dev/null 2>&1; done
    jq -r '.enabledPlugins | to_entries[] | select(.value == true) | .key' .claude/settings.json |
        while read -r plugin; do
            claude plugin install "$plugin" --scope project >/dev/null 2>&1 || echo "warning: $plugin did not install"
        done
fi

step "check"
for tool in clangd glslc; do command -v "$tool" >/dev/null 2>&1 && echo "ok   $tool" || echo "MISS $tool"; done
{ command -v android >/dev/null 2>&1 || [ -x "$HOME/.local/bin/android" ]; } && echo "ok   android" || echo "MISS android"
exit 0
