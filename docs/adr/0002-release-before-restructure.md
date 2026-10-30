---
status: accepted
---

# Ship to Play before the module split; only the layer fix goes first

[ADR 0001](0001-target-module-split.md) lists six restructuring steps but no relation to the Play release. Decided with the user on 2026-10-30, after the gap analysis in [`ARCHITECTURE_FINISH_PLAN.md`](../ARCHITECTURE_FINISH_PLAN.md): **only step 1 (fix the reverse dependencies) lands before the release.** Steps 2–6, Navigation 3 included, come after it, in ADR 0001's order. Nothing in them changes what a Play user sees, while each one moves files that the release work (QA fixes, restyle, paywall) is editing at the same time. Pre-release architecture work is limited to what lowers crash or ANR risk: main-thread `contentResolver` calls out of composables, golden-file tests for the preset and template formats, and the startup and stability items in the plan.

Three smaller decisions came with it:

- **`dev.geode.model` is the staging package for `:core:model`.** Pure value types that several layers share (`DeviceTrack`, `LibraryTrack`, `LyricLine`, `PresetLink`) move there now, so step 2 wraps a package in a module instead of renaming everything a second time. Types that touch Android resources or storage go to `dev.geode.data` (`ThemeStore`, `GuiPrefs`, `UserIntent`, `FontColorChoice`, `TrackLibrary`, `DeviceTrackQuery`, `UserDataRepository`). `UserIntent.landingDestination` names a UI destination, so it stays in `ui` as an extension.
- **Code that only means "open the app" uses the package's launch intent** (`dev.geode.appLaunchIntent`), not `MainActivity`. No injected provider: the widget and the services don't inject, and there is only one adapter.
- **The layer rule is enforced in CI, not by review.** `tools/layer-gate.sh` (run by `arch-map.yml`) fails any PR in which code outside `ui/` and `di/` names `dev.geode.ui`. It has no override, like the map gate.

## Consequences

- ADR 0001's warning still holds: `PlayerSession` is dissolved (step 4) before Nav3 (step 5).
- Until step 2, `model` is a package, not a module. Nothing stops it from importing `android.*` except review. When step 2 makes it a JVM module, the compiler takes over.
