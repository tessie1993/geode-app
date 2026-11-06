#!/usr/bin/env bash
# Installs the host tools a cloud agent session uses on this repo, and the plugins
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

# Skill sets fetched as repo skills rather than plugins. Claude Code applies the skillOverrides in
# .claude/settings.json only to repo and user skills, never to plugin skills, so a plugin's
# skills always list their full descriptions. Fetching these two large sets into .claude/skills
# lets the ones Geode rarely needs be listed by name only, while every one stays invocable.
# Pinned commits keep sessions reproducible: bump a SHA to update a set. The copies are kept
# out of git through .git/info/exclude (local to this checkout).
step "skill sets: Google android/skills and chrisbanes/skills (pinned)"
fetch_skills() { # <owner/repo> <sha>
    local repo="$1" sha="$2" tmp
    tmp="$(mktemp -d)"
    if git -C "$tmp" init -q && git -C "$tmp" fetch -q --depth 1 "https://github.com/$repo" "$sha" &&
        git -C "$tmp" checkout -q FETCH_HEAD; then
        find "$tmp" -name SKILL.md -not -path '*/.git/*' | while read -r skill_md; do
            local dir name
            dir="$(dirname "$skill_md")"
            name="$(basename "$dir")"
            if [ -e ".claude/skills/$name" ] && ! grep -qx "/.claude/skills/$name/" .git/info/exclude 2>/dev/null; then
                echo "warning: $repo skill $name clashes with a tracked skill; skipped"
                continue
            fi
            rm -rf ".claude/skills/$name"
            cp -r "$dir" ".claude/skills/$name"
            grep -qx "/.claude/skills/$name/" .git/info/exclude 2>/dev/null ||
                echo "/.claude/skills/$name/" >>.git/info/exclude
        done
    else
        echo "warning: could not fetch $repo@$sha"
    fi
    rm -rf "$tmp"
}
mkdir -p .claude/skills .git/info
fetch_skills android/skills bf68e2d66a992a1e6aeb7e9263293cc23fcbf475
fetch_skills chrisbanes/skills 8df9735a751cfabe5ddae457f28b0a8ea3d4092b

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
