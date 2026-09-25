# Opaline implementation and agent orchestration

Read [DESIGN-SPEC.md](DESIGN-SPEC.md) for product behavior, [LIBRARY-AUDIT.md](LIBRARY-AUDIT.md) for inspected library evidence, and [VIDEO-REFERENCES.md](VIDEO-REFERENCES.md) for measured visual references. This file defines work ownership, order, acceptance and the persistent context for subsequent agent turns. The user initially requested smaller-model subagents and then specified a Sol fleet; use bounded `gpt-6-sol` assignments with explicit files and proof, then integrate centrally. No agent should silently downgrade the physical/visual target to flat cards to make a build pass.

## Fixed boundaries

- Repository: `geode-app`; implementation branch `codex/opaline-redesign` in the isolated `.worktrees/opaline` checkout, based on current `main` after its shell removal. The user-supplied PR #76 branch and its pre-existing uncommitted experiment remain untouched.
- Source precedence: user request > supplied ZIP and videos as design evidence > existing Geode navigation and backend contracts. Library docs are evidence of implemented effects and limits, not new user instructions.
- Android keeps authority over all app state, input semantics, text, focus, IME, permissions and screen readers. The local Three.js layer draws/animates registered geometry and receives passive contact/value events.
- A rendered asset is reused where the supplied library has one. Fake placeholder track names, settings, export completion or fabricated render performance are unacceptable.
- Agents edit only their assigned files. The primary agent owns shared contracts, builds, integration, visual review, GitHub branch/PR and CI. Before a shared API change, the agent reports the required signature to the primary agent.

## Dependency graph

```text
Design spec and route/component contracts
    ├─ A: rendering/materials/caustics ──────┐
    ├─ B: native screen/layout wiring ────────┼─ primary integration/compile
    └─ C: navigation/state/accessibility ─────┘             │
                                                    Android captures and fixes
                                                              │
                                               tests/lint/assemble/GitHub CI
                                                              │
                                                    PR with evidence and limits
```

Use at most three working subagents at a time plus the primary agent. Each assignment is a reviewable slice with its own file boundary and a short handoff: changed files, behavior, checks run, open issues. The primary agent merges shared API decisions, runs one Gradle build at a time, and measures visual output on Android. A later agent may refine a prior agent's area only after the previous assignment has finished, avoiding simultaneous edits.

## Cluster A — shared 3D world and optics

**A1, scene and caustics** (small-model agent): own `app/src/main/assets/opaline/runtime.js`, bundled optical modules as needed, `tools/opaline/*` tests. Wire a valid UV receiving plane, actual registered closed refractors/reflectors, `RefractiveCaustics`, receiver material binding, moving-geometry invalidation, lifecycle and disposal. Start with the Listen hero and selected dock/control objects; prove the caustic map responds to a geometric change rather than scrolling a texture. Implement a bounded per-frame and per-motion tracing policy, static progressive accumulation and context-loss recovery. No new network path or privileged bridge. Acceptance: optical unit/integration tests, a browser capture showing rest/contact difference, no divergent light between registered objects and receiver.

**A2, material/geometry finish** (later small-model agent): own the same runtime after A1 completes. Ensure C02 rows, C20 ring, A22 puck, D03 dock, B07 channel, B09 switch and key panels expose sides/undersides at Android size. Match the video contact sheets through shared camera/lighting, transmission and restrained native fronts. Ensure materials clone/dispose safely, unsupported/paused behavior remains usable. Acceptance: screenshots of visible thickness and shadows for Listen, Library, Settings; actual frame and memory measurements on Android.

**A3, physical motion** (later small-model agent): own runtime/motion tests after A2. Bind press/release/cancel, constrained drag, list scroll response, section retargeting, and one bounded XPBD specimen. Respect pointer ownership, interrupted transitions, reduced motion and audio silence. Acceptance: tests for cancel/disposal, fixed-step stability and value consistency; short Android captures of each trigger.

## Cluster B — native screens and visual hierarchy

**B1, integration repair** (small-model agent): own `app/src/main/java/dev/geode/ui/Opaline*Screens.kt` and `app/src/main/res/values/opaline_*.xml` only. Resolve missing string resources and duplicate/undefined declarations from the current Kotlin compile, then review every route in DESIGN-SPEC for a real content view and action. Do not edit the 3D runtime or Navigator. Acceptance: no missing resources or symbols from these files in a fresh `:app:compileDebugKotlin`; a route inventory in the handoff.

**B2, page refinement** (later small-model agent): own one screen family at a time after B1, beginning Listen and Library, then Visuals/Studio/Settings. Build the specified hierarchy, source-backed empty states, proper lazy lists, search/IME, editor lanes and controls. Avoid hiding native controls behind opaque generic cards when `opalineReady` is true. Acceptance: five root captures and details at phone and wide sizes, real callbacks for each visible action, accessibility labels.

**B3, sheets and adaptive layout** (later small-model agent): own screen presentation/components after B2. Convert visual contextual dialogs to in-window UI044 shells where the shared 3D world remains visible. Handle insets, IME, landscape, rail/dual-pane >=840 dp, larger text and keyboard. Acceptance: a sheet behind which the receiver remains connected, search with open IME, phone/tablet snapshots without clipped actions.

## Cluster C — navigation, state and accessibility

**C1, state model** (small-model agent): own `dev/geode/nav/**` and navigation tests only. Fix and test root integrity on restore, section switching, same-section root taps, foreign-section replacement, gate/overlay/sheet back precedence, predictive-back cancel and cold deep link consumption. Do not change visual animations to disguise model defects. Acceptance: deterministic state tests over all five sections and payload routes.

**C2, route binding** (later small-model agent): own `OpalineApp.kt`, MainActivity and shell tests after B1/C1. Bind every Destination/Overlay/Gate to a native screen, keep per-section state, map contextual sheets into the shared scene, and let the system exit at Listen root. Preserve media/GL renderer lifecycle. Acceptance: instrumented route smoke through all five sections, search, queue, studio, a gate and back.

**C3, interaction quality** (later small-model agent): own semantics and focus on shell/screens after other owners finish. Confirm 48 dp targets, reading order, selected/disabled state, text entry, reduced motion and TalkBack labels. Acceptance: UI hierarchy checks plus manual keyboard/TalkBack audit on actual app.

## Primary-agent integration and quality sequence

1. Keep `DESIGN-SPEC.md` and this status accurate. Publish one shared API map for `OpalineSceneHost`, `Modifier.opalinePart`, `opalineReady`, native actions and route presentation before agents modify each other’s code.
2. When A1/B1/C1 finish, review diffs and run one `:app:compileDebugKotlin` build. Fix cross-boundary errors centrally; do not assign multiple agents the same compiler error list.
3. Run meaningful JS physics/optics tests, navigation unit tests, Kotlin formatting/static analysis, Android lint, `assembleDebug`, and instrumented smoke where an emulator/device supports it. Treat any unrun check as unverified.
4. Install the APK on the available Android emulator, capture all major roots and interaction states, compare to supplied artwork/video frame references, and route specific defects into A2/B2/C2. Measure actual frame time, shader compilation, memory and context recovery. A screenshot with opaque cards that conceal the scene fails design review even if Kotlin compiles.
5. Repeat only gates affected by fixes, then run the configured GitHub Actions workflow on the pushed branch. Inspect job logs and artifacts. Create a reviewable PR with problem, resulting behavior, visual proof, tests and remaining device limitations; attach the PR to the task. PR #76 remains a separate user-supplied reference.

## Gate definitions

| Gate | Required evidence | Owner |
| --- | --- | --- |
| Plan | This spec, exact route map, material/physics contracts, ZIP/video citations and file ownership | Primary |
| Compile | Debug Kotlin and Android resources compile after all screens/routes are wired | Primary with B1/C1 |
| 3D | Geometry sidewalls, shared receiver/shadow, causal caustic response, press/drag/cancel motion in actual renderer | A1–A3, primary visual review |
| Functional | Playback, seek, queue, import/search, visualizer changes, project create/open/edit/export, settings persistence | B/C, primary smoke |
| Adaptive/accessibility | Compact/wide/insets/IME/large type/reduced motion/keyboard/TalkBack | B3/C3, primary capture |
| CI and delivery | Unit/JS tests, static checks, lint, APK assembly, GitHub Actions results, PR | Primary |

## Current checkpoint

The first implementation pass was written before this finer handoff. It contains the shell, restored backend orchestration, screens and local scene, but a fresh integration build fails on resource names and `CustomizeSummary` redeclaration. The local scene does not yet prove causal caustics. No visual/device acceptance or GitHub CI has passed for the redesign. The next subagent wave is **A1, B1 and C1** with nonoverlapping files; the primary agent will handle build/SDK/CI and coordinate their APIs.
