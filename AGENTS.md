# AGENTS.md: BeatRealms 3D (internal name Geode)

The approach, the rules and the way of working for everyone who changes this repository, people and agents alike.
This file changes only when one of those changes. It holds no map of the code and no log: the structure lives in Graphify, and what changed lives in the commit and the pull request.

## What it is

- Android music player, reactive visualizer and video studio, applicationId `com.beatrealms3d.app`; the unlock is BeatRealms Pro.
- A Kotlin and Compose shell with Hilt, over a C++20 native core built as `libgeode.so`.

## Finding your way: Graphify first

- Ask the Graphify MCP tools (repository `tessie1993/geode-app`) before grep or file search: `graphify_find` for definitions, `query_graph` for "how does X work", `graphify_callers` / `graphify_callees`, `graphify_file_neighbors` for a file's dependencies, `graphify_impact` / `impact_and_risk` before a change, `graphify_tests_for` for tests.
- Use grep only for literal strings, config values and files the graph lacks.
- The graph is built from the default branch at one commit (`graph_stats` names it). Run `git diff --name-status <indexed-commit> HEAD` and read the current file for anything in that list.
- Domain words are in `GLOSSARY.md`.

## Kotlin is the shell, C++ is the app

- Kotlin is the main-thread shell only: Compose UI, ViewModels, Android services and platform calls, stores, and the JNI declarations in `dev.geode.engine.bridge`.
- Everything else is C++ in `core/`: every thread off the main thread, all parameter code, modulation and reactions, the audio engine and DSP, shaders and the renderer.
- PCM data, buffers and samples are never handled in Kotlin. Parameter changes are never computed in Kotlin.
- ExoPlayer stays the player; its tap comes first in the chain and hands PCM to native.
- Why: one implementation for live, wallpaper, dream and export, host tests in `core/tests/`, and no JVM on the drawing or audio threads.
- Engine Kotlin still in the tree is debt to port, never a pattern to copy.

## Rules

- No bias in the audio analysis, ever: it assumes no fixed or preferred value for what it measures; nothing prefers, weights or centres on a tempo; an unheard tempo is unknown (0), never a stand-in number such as 120, tests included. A display rate (fps, Hz) is not a tempo.
- No hard-coded reactions, routes, sources or destinations: what moves on a sound is routing the user can see and edit.
- No colour narrowing: palette, saturation and brightness belong to the style and the user.
- Smallest fix possible.
- Ask why before deleting a caller or a file: understand what it did and what its absence breaks.
- If a build breaks because a file was cut, do not put the file back; change what asked for it.
- Clean room: take no code, names or strings from other projects, and name no such project in the repo.
- Cast is the app's only internet use.

## Way of working

- No building, compiling, Gradle or emulator in an agent container. CI builds, tests and lints. Run `tools/ktlint-changed.sh` before pushing Kotlin.
- Follow Android best practice, and check the Android developer documentation for the area you change.
- One change per pull request, small, stacked when one depends on another.
- A pull request merges only when every check is green at its head.
- Before you push, a reviewer reads the change and checks it against `main` and the other open pull requests for conflicts.
- Bring a branch up to date by merging `main` into it; never rebase or force-push someone else's branch.
- When something is unclear, ask the owner.

## Agent protocols, issues and pull requests

- Every piece of work starts from a GitHub issue and lands through a pull request that closes it (`Closes #N`). The issue holds the why and the checklist; the commits and the PR hold what changed.
- Work in a fresh worktree cut from `origin/main`; remove it once its PR is merged or closed.
- Follow the agent-protocols loop: `/agent-protocols:spec` or `/agent-protocols:plan` for anything new, `/agent-protocols:build` and `/agent-protocols:test` to implement, `/agent-protocols:review` (or the `code-review` skill) before pushing, `/agent-protocols:ship` for the pre-launch checklist.
- `/ship` commits, pushes, opens the PR and watches CI; `/steward` watches an open PR. Neither merges: the owner does.
- Play Store release work carries the `release:play` label and hangs off the release-readiness tracking issue.
- Android tooling on a workstation: the `android` CLI for SDK, docs and artifact inspection (`android analyze` on an AAB), `adb` and the adb MCP for devices. `gh` for everything GitHub.

## The owner's preferences

- Be curious about the why.
- Ask questions.
- Use the command line where possible.
- Consult the relevant documentation.
- No excessive comments in scripts.
