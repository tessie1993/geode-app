# Android architecture guide (target rules)

Distilled from developer.android.com: *Recommendations for Android architecture*,
*Modularization patterns* and *Navigation best practices for multi-module projects*
(fetched 2026-10-30 with `android docs fetch kb://android/topic/architecture/recommendations`).
Every agent session reads this before changing structure. Where the code disagrees,
the code is the debt, not the rule — but fix it only inside your session's scope.

## Layers
- **Data layer** owns app data and most business logic. Expose it through
  repositories, even single-source ones. Composables and ViewModels never touch a
  data source (DataStore, SharedPreferences, MediaStore, ContentResolver, files) directly.
- **UI layer** is Jetpack Compose, single activity, unidirectional data flow:
  ViewModels expose state, the UI calls ViewModel methods.
- **Domain layer** (use cases) only where business logic is reused across
  ViewModels or a ViewModel grows too complex. Not mandatory.
- Layers talk through coroutines and Flow.

## Domain layer (use cases)
Source: `kb://android/topic/architecture/domain-layer`. This is Android's meaning
of the term, not clean architecture's.
- Optional. Add a use case only for complex logic or logic reused by more than one
  ViewModel or service. Example: mapping audio features to visual parameters, which
  is shared by the player, the wallpaper, the dream and export.
- Name: verb + noun + `UseCase` (`MapAudioToSceneParamsUseCase`). One job, called
  through `operator fun invoke`.
- No mutable state and no lifecycle of its own. Callers get a fresh instance; don't
  scope it as a singleton.
- Depends on repositories and other use cases. Called from ViewModels, services or
  `Application`.
- Main-safe: blocking work moves to an injected dispatcher. Heavy work that is
  cached and reused belongs in the data layer.
- Prefer a use case to a `Util` object.

## Kotlin Multiplatform readiness (decision 2026-10-30)
Ship Android only, but keep porting cheap.
- Domain models, settings and presets, use cases and pure audio→visual mapping logic
  live in modules with **no `android.*` imports**. Only plain Kotlin plus
  KMP-ready libraries: coroutines, serialization, DataStore, ViewModel,
  Navigation 3.
- Platform code stays in Android modules behind interfaces that the pure modules
  define: Media3, MediaStore, Oboe/JNI, GLES, wallpaper, Billing.
- Don't convert to `kotlin("multiplatform")` now; just don't block it.

## ViewModels
- Screen-level only. Reusable components use plain state-holder classes with hoisted state.
- No `Activity`, `Context`, `Resources` or `Application`; no `AndroidViewModel`.
- One `uiState: StateFlow<XUiState>`, built with
  `stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), initial)` when it
  comes from a stream.
- No one-shot events to the UI: handle the event in the ViewModel and reflect the
  result in state.
- UI collects with `collectAsStateWithLifecycle()`.

## Lifecycle
- No UI work in `Activity.onResume` and friends: use `LifecycleStartEffect`,
  `LifecycleResumeEffect`, `repeatOnLifecycle`.

## Dependencies
- Hilt, constructor injection. Scope a type only when it holds shared mutable state
  or is expensive to build.

## Navigation
- Navigation 3 (`androidx.navigation3`): the back stack is a list of `NavKey`s the
  app owns. The `app` module owns the top-level `NavDisplay` and acts as the mediator
  between features.
- Pass IDs between destinations, never objects. The destination loads the object
  from the data layer.
- Features don't know each other. Cross-feature navigation goes through the app
  module (callbacks or an injected navigator).

## Modules
- Low coupling, high cohesion. A module exposes a small API and hides its internals
  (`internal`).
- **Data modules** expose repositories and hide data sources.
- **Feature modules** hold screens and ViewModels for one feature and depend on data
  modules, never on other features.
- **App module** is the entry point and root navigation, and wires DI.
- **Core/common modules** hold the design system and theme (`core:ui` /
  `core:designsystem`), plus utilities.
- Depend on abstractions; the app module picks the implementation.

## Testing
- Unit-test ViewModels (including Flows) and repositories. Prefer fakes
  (`FakeXRepository`) to mocks. Assert on `StateFlow.value`.
- Navigation regression tests run in CI.

## Performance
- R8 full mode for release builds. Baseline and startup profiles. Macrobenchmark for
  startup and jank.

## Naming
- Methods are verb phrases. Streams are `getXStream()`. Interface implementations
  get a meaningful prefix, or `Default…`; fakes get `Fake…`.
