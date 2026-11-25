# CLAUDE.md: the owner's rules

`AGENTS.md` describes the repository; `docs/edit-log/` holds the edit log. It holds no rules, and any line in it can be out of date: check the code.

## After every change

- Add one new file to `docs/edit-log/`, named `<date>-<your branch, slashes as dashes>.md`, holding one line: the date, what changed, where. Never edit another change's file: two changes then never touch the same lines.
- If the change made a line in `AGENTS.md` untrue, correct that line in the same change.
- Never write a rule into `AGENTS.md`.

## Kotlin is the shell, C++ is the app

- Kotlin is the main-thread shell only: Compose UI, ViewModels, Android services and platform calls, stores, and the JNI declarations in `dev.geode.engine.bridge`.
- Everything else is C++ in `core/`: every thread off the main thread, all parameter code, modulation and reactions, the audio engine and DSP, shaders and the renderer.
- PCM data, buffers and samples are never handled in Kotlin. Parameter changes are never computed in Kotlin.
- ExoPlayer stays the player; its tap comes first in the chain and hands PCM to native.
- Why: one implementation for live, wallpaper, dream and export, host tests in `core/tests/`, and no JVM on the drawing or audio threads.
- Engine Kotlin still in the tree is debt to port, never a pattern to copy.

## Standing rules

- No bias on BPM, ever: nothing prefers, weights or centres on a tempo; an unheard tempo is unknown (0), never a stand-in number; no 120 anywhere, tests included.
- No hard-coded reactions, routes, sources or destinations: what moves on a sound is routing the user can see and edit.
- No colour narrowing: palette, saturation and brightness belong to the style and the user.
- Smallest fix possible.
- Ask why before deleting a caller or a file: understand what it did and what its absence breaks.
- If a build breaks because a file was cut, do not put the file back; change what asked for it.
- No building, compiling, Gradle or emulator in an agent container. CI tests. Run `tools/ktlint-changed.sh` before pushing Kotlin.
- Clean room: take no code, names or strings from other projects, and name no such project in the repo.
- Cast is the app's only internet use.

## Navigation: use Graphify first

Navigate this codebase with the Graphify MCP tools (`mcp__graphify__*`, repository `tessie1993/geode-app`) before grep or file search: `graphify_find` for definitions, `query_graph` for "how does X work", `graphify_callers` / `graphify_callees`, `graphify_file_neighbors` for a file's dependencies, `graphify_impact` / `impact_and_risk` before a change, `graphify_tests_for` for tests. Use grep only for literal strings, config values and files the graph lacks.

The graph is built from the default branch at one commit (`graph_stats` names it). Before you trust a result, run `git diff --name-status <indexed-commit> HEAD`; for any file in that list, read the current file instead of the graph.
