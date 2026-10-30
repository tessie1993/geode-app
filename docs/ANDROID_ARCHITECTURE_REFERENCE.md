# Android architecture reference (detailed)

This is the "why and how" behind [ANDROID_ARCHITECTURE_GUIDE.md](ANDROID_ARCHITECTURE_GUIDE.md).
The guide holds the rules. This file holds the patterns, trade-offs and code shapes from
developer.android.com, paraphrased (fetched 2026-10-30 with `android docs fetch kb://...`).
Read the section you need; don't read the whole file. Notes marked **(derived)** apply the
docs to this repo and aren't official text. Re-fetch a `kb://` URL when you need exact
API signatures.

---

## Layers & UDF
Sources: `kb://android/topic/architecture/index`, `kb://android/topic/architecture/recommendations`

- **Why layers.** The OS can destroy or launch components (Activity, Service) at any time and out
  of order, so app data and state never live in them. Separation of concerns is the main
  principle; the classic mistake is putting everything in an `Activity`.
- **Drive UI from data models**, ideally persistent ones, so the user loses nothing when the
  process dies.
- **SSOT.** Every data type has one owner, the only thing that mutates it. The owner exposes an
  immutable type and takes changes through functions. It can be a DB, a repository cache, or (for
  screen state) a ViewModel.
- **UDF.** State flows down from higher-scoped types; events flow up to the SSOT. At least two
  layers (UI and data), plus an optional domain layer. Diagram arrows mean "depends on".
- Only app components touch `Context` and `Toast`. **Types own their concurrency policy**, so
  every public API is main-safe.
- **Adaptive.** Derive state from `currentWindowAdaptiveInfo()`; `NavigationSuiteScaffold`
  switches bar, rail and drawer. Preserve UI state across resize, fold and rotation.
- **DI.** Hilt with constructor injection pays off with many ViewModel screens, WorkManager, or
  **ViewModels scoped to the back stack**. Scope only shared-mutable or expensive, widely used
  types.
- **Models per layer** in complex apps: map remote and DAO models to slimmer app models.

## UI layer
Sources: `kb://android/topic/architecture/ui-layer/index`, `.../ui-layer/stateholders`,
`.../ui-layer/state-production`, `kb://android/topic/architecture/recommendations`

### UI state
- `XUiState` (*functionality* + `UiState`) is an **immutable** snapshot. The UI never mutates it
  unless the UI is the data's only source. A composable flipping `bookmarked` locally now competes
  with the data layer for the truth.
- **Screen UI state** (what to show, usually app data) vs **UI element state** (scroll, drawer;
  hoisted into `ScaffoldState`, `LazyListState`).
- **One stream for related state**, because two streams can go out of sync. Derive facts as
  properties (`val canBookmark get() = isSignedIn && isPremium`). Split only **unrelated** data or
  a field that updates far more often. Every emission re-renders, so add `distinctUntilChanged()`
  where it helps.
- Loading is a boolean. Errors are data (`userMessages: List<Message>`) so they can carry text and
  a retry action. `PagingData` isn't immutable, so expose it as its own `Flow`.
- State-driven navigation (`isSignedIn = true`) is consumed like other state, and the navigation
  layer acts on it. Collect with `collectAsStateWithLifecycle()`.

### State holders
- **Business-logic holder** = a ViewModel. It produces screen state from the data and domain
  layers, survives recreation, and lives while its destination is on the back stack. It belongs
  to one destination. Logic shared across destinations goes into the data or domain layer, never
  into a shared ViewModel.
- **UI-logic holder** = a plain `@Stable` class from `rememberXState(...)`. It may hold `Resources`
  and `WindowSizeClass` (same lifecycle as the UI), doesn't survive recreation (it delegates to
  `rememberSaveable`), and is reusable. Example: `NiaAppState` decides bar vs rail.
- **Business logic before UI logic.** A holder depends only on holders with an **equal or
  shorter** lifetime, so one screen ViewModel depending on another is a smell. A plain holder gets
  the `StateFlow` slice and lambdas, never the ViewModel.
- **Never pass a ViewModel down.** State goes down, event lambdas go up to the composable that
  owns it. ViewModels only at screen level; `rememberViewModelStoreOwner()` scopes one to a
  complex composable's call site.

### State production
- "State is; events happen." Events are inputs and state is the output. Pipelines are
  **lifecycle-aware** (no work while invisible unless required). Pick the API by input:

| Input | Technique | Output |
|---|---|---|
| One-shot (suspend/sync) | `MutableStateFlow.update { it.copy(...) }` or `mutableStateOf` | `StateFlow` / Compose `State` |
| Streams | `combine(...)` + `stateIn` | `StateFlow` |
| Both | Turn one-shots into private `MutableStateFlow`s (or `snapshotFlow`), then `combine` | `StateFlow` |

- `stateIn(started = WhileSubscribed(5_000))` runs the pipeline only while the UI collects
  lifecycle-aware. `Lazily` keeps it hot while the destination is on the back stack or in an
  off-screen tab.
- Produce state on Main. Off Main, use `withContext`, and wrap Compose-state writes in
  `Snapshot.withMutableSnapshot`. Catch `Exception` around repository calls but **rethrow
  `CancellationException`**.
- `viewModelScope` work runs to completion even when the UI is hidden. Work of 5 s or more goes to
  WorkManager.
- **No async work in `init` or the constructor** (it leaks a half-built object; with Compose
  state it can throw `IllegalStateException`). Prefer lazy `stateIn`. Otherwise use an idempotent
  `@MainThread fun initialize()`.

```kotlin
class TrackDetailViewModel(trackId: String, private val repo: TracksRepository) : ViewModel() { // Nav3: id comes from the key
    private val deleted = MutableStateFlow(false)                 // one-shot input as a stream
    val uiState: StateFlow<TrackDetailUiState> =
        combine(repo.getTrackStream(trackId), deleted) { t, d -> TrackDetailUiState(t, d) }
            .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), TrackDetailUiState())
    fun delete() = viewModelScope.launch { /* repo.delete(...) */ deleted.update { true } }
}
```

### Events
- **Don't send one-shot events from the ViewModel to the UI** (strongly recommended). Handle the
  event in the ViewModel at once and record the result in state: `isTaskSaved = true`, or an entry
  in `userMessages`.
- UI-only events (scrolling, showing a snackbar from UI state, navigating on click) stay in the
  UI or a plain holder.

## Data layer
Source: `kb://android/topic/architecture/data-layer/index`

- **Repositories** (*data type* + `Repository`) expose data, centralize changes, resolve source
  conflicts, hide sources and **hold business logic**. One per data type, even with a single
  source. They may depend on other repositories (some teams call those `…Manager`).
- **Data sources** (*type* + *Local/Remote* + `DataSource`) each wrap exactly one source: file, DB,
  network, DataStore, or a WorkManager task set (`XTasksDataSource`). Don't name them after the
  technology (`UserSharedPreferencesDataSource`), except temporarily during a migration behind an
  interface.
- **Only repositories are entry points.** ViewModels and use cases never depend on a data source.
- **API shape.** `suspend fun` for one-shot CRUD, `Flow` for changes. Exposed data is
  **immutable**, so it's thread-safe. Each repository has one SSOT (a DB is preferred for
  offline-first, or a cache, or the network) and exposes only what comes from it.
- **Threading.** Main-safe. Inject a `CoroutineDispatcher` and `withContext(io)` around blocking
  APIs. Expensive list filtering belongs here too.
- **Operation lifetime decides the scope:**
  - *UI-oriented* (only while on screen): runs in the caller's scope (`viewModelScope`).
  - *App-oriented* (while the app is alive, such as filling a cache): the repository gets an
    **injected external `CoroutineScope`** (`SupervisorJob() + Dispatchers.Default`) and does
    `externalScope.async { … }.await()`. The caller can cancel its `await`, but the work and the
    cache write still finish.
  - *Business-oriented* (must survive process death): WorkManager. The worker calls the
    repository, and the repository schedules through a `XTasksDataSource`. Trigger startup work
    from an App Startup `Initializer`.
- **Caches**: a `Mutex`-guarded field in the repository or data source is fine.
- **Instance lifetime.** Scope to `Application` (`@Singleton`) only what the whole app needs as one
  instance. Scope flow-only state to the flow's owner: an Activity, or **a back stack through a
  `NavEntryDecorator`**.
- **Storage.** Room for large or relational data. **DataStore** for small key-value or typed data,
  split by area because `data` emits on every change to that store. Files for blobs, with explicit
  thread switching. Map models whenever a source's model doesn't match what the app needs.
- **Errors.** `try/catch` around `suspend` calls and `.catch {}` on flows; the UI layer handles
  what's thrown. Custom exceptions (`UserNotAuthenticatedException`) are fine. Use `Result<T>`
  when callers must handle *known* failures.

```kotlin
class PresetsRepository @Inject constructor(
    private val local: PresetsLocalDataSource,            // internal to :core:data
    @ApplicationScope private val externalScope: CoroutineScope,
) {
    val presets: Flow<List<Preset>> = local.presetsStream()            // stream from the SSOT
    suspend fun importPack(uri: String) = externalScope.async { local.import(uri) }.await() // outlives screen
}
```

## Domain layer
Source: `kb://android/topic/architecture/domain-layer`

The guide has the rules (optional, naming, `invoke`, stateless, fresh instance, main-safe). The
nuances behind them:
- Benefits: no duplication, smaller ViewModels, logic testable in isolation. Use cases can call
  other use cases (several levels is normal). `invoke` may be overloaded.
- Other consumers reuse them too: services, `Application`, other form factors' UIs (here, the
  wallpaper and dream).
- **Where heavy work goes.** A use case can move work to `defaultDispatcher` (for example mapping
  an unbounded list). But if the result is cached or reused across screens, it belongs in the
  **data layer**. If a single query (a Room relation) can produce the data, write a repository
  like `NewsWithAuthorsRepository`, not a use case.
- `Util` objects lose to use cases: they're hard to discover, and use cases can share threading
  and error handling.
- **Forcing all UI access through use cases** stops the UI from bypassing logic (analytics, for
  example) but breeds pass-through use cases. Default to "add when needed". Make them mandatory
  only once nearly all access already goes through them.
- Test with fake repositories.

```kotlin
class MapAudioToSceneParamsUseCase @Inject constructor(
    private val settings: VisualSettingsRepository,
    @DefaultDispatcher private val default: CoroutineDispatcher,
) {
    suspend operator fun invoke(frame: AudioFeatures): SceneParams = withContext(default) { /* pure mapping */ TODO() }
}
```

## Modularization
Sources: `kb://android/topic/modularization/index`, `kb://android/topic/modularization/patterns`,
`kb://android/guide/navigation/integrations/multi-module`

- **Why.** Reuse, strict visibility (`internal`), Play Feature Delivery; plus scalability,
  ownership, encapsulation, testability and incremental or parallel builds.
- **Granularity.** Too fine means build overhead; too coarse means a new monolith. If two modules
  lean on each other's internals, merge them. If two parts of one module never interact, split
  them.

### Module types
| Type | Contains | Public API | Depends on |
|---|---|---|---|
| Data (`:core:data`, `:data:x`) | repositories, data sources, models for one domain | repositories only; sources `internal` | other data/core modules |
| Feature (`:feature:x`) | screens and ViewModels for one feature (often one bottom-bar tab or flow) | entry points / nav keys | data modules, core UI. **Never other features** |
| App (`:app`) | Activity, root navigation, DI wiring | none | all features; picks implementations |
| Common/core | `core:ui`/designsystem, analytics, network, utilities | small helpers | nothing feature-level |
| Test | shared fakes, fixtures, integration tests | test-only | the APIs it fakes |

- One **app module per device type** (phone, Auto, TV, Wear) keeps platform dependencies apart.
  Build variants come from one app module.

### Module-to-module communication
- Features can't depend on each other (cycles), so the **app module mediates** because it owns
  navigation. **Pass IDs, not objects.** The destination loads the object from the shared data
  module, which keeps coupling low and the SSOT intact. In Nav3 the ID arrives in the key.
- From the Nav2 multi-module page, still valid: the app knows only each feature's **entry point**.
  Destinations shared by several features (login, paywall) aren't copied into each feature.

### Dependency inversion (api/impl)
- An **abstraction module** holds interfaces and models. **Implementation modules** depend on it.
  High-level modules depend only on the abstraction. The **app module binds the implementation**
  through DI and can vary it per variant or test set
  (`releaseImplementation(":x:impl:real")`, `androidTestImplementation(":x:impl:fake")`).
- Split API from implementation when there are several implementations (the docs' own example is
  **a renderer that uses OpenGL or Vulkan**, or Play vs in-house billing), several apps or
  platforms, independent teams, or a large codebase. Bonus: changing an impl module doesn't
  recompile the API module's consumers.
- Steps: create the API module, create impl modules that depend on it, point consumers at the API,
  and provide the impl in `:app`.

### Build hygiene
- Keep configuration consistent with a **version catalog** and **convention plugins**. The docs
  only name them; the usual shape is an included `build-logic` build with `x.android.library` /
  `x.android.feature` plugins **(derived)**.
- **`implementation` over `api`.** `api` leaks transitive dependencies and widens rebuilds.
- **Prefer Kotlin/Java (JVM) library modules** wherever there are no resources, manifest or
  Android APIs. Android modules carry overhead.

## Navigation 3
Sources: `kb://android/guide/navigation/navigation-3/basics`, `.../save-state`, `.../modularize`,
`.../migration-guide`, `.../naventrydecorators`, `.../scenes/index`, `.../deep-links/index`,
`.../recipes/modular-hilt`, `.../recipes/multiple-backstacks`, `.../recipes/passingarguments`,
`.../recipes/sharedviewmodel`, `.../recipes/common-ui` (all under `kb://android/guide/navigation/`;
there's also the `navigation-3` skill).

**Dependencies (as of fetch):** `androidx.navigation3:navigation3-runtime`/`-ui` `1.2.0`,
`androidx.lifecycle:lifecycle-viewmodel-navigation3` `2.11.0`, and optionally
`androidx.compose.material3.adaptive:adaptive-navigation3`, plus the KotlinX Serialization plugin.
Needs **compileSdk 36, minSdk 23**. Compose-only; Views go through interop.

### Keys and the back stack you own
- The back stack holds **keys** (references to content), never content. A key is usually a
  `@Serializable data object` or `data class` implementing the marker interface `NavKey`. If the
  keys are serializable, the stack can be saved across configuration changes and process death.
- **You own the stack.** It's a snapshot-state `List<T>`: push is `add`, pop is
  `removeLastOrNull()`. `NavDisplay` only observes it. There's no `NavController` and no graph.
- **Arguments are key fields.** `entry<TrackDetail> { key -> … key.id }`. Keep them to IDs and
  primitives.
- A sealed app-wide key type keeps typing:
  `@Serializable sealed interface AppKey : NavKey`, with
  `rememberSerializable(serializer = serializer()) { NavBackStack<AppKey>(*start) }`.

### entryProvider and NavDisplay
- An **entry provider** maps a key to a `NavEntry` (key, content, `metadata`). Write it as a
  `when` lambda (needs an `else`) or, preferably, the **`entryProvider { entry<K> { … } }` DSL**,
  which throws on unknown keys. `NavDisplay(backStack, onBack, entryProvider, entryDecorators,
  sceneStrategies)` shows the top entry in a single pane, or takes pre-decorated `entries = …`.
- **Entry lifecycle.** Each entry gets its own `LocalLifecycleOwner`, capped at `RESUMED` when
  settled with no overlay, `STARTED` during transitions or under a dialog or sheet, and `CREATED`
  once popped but still animating out. Use `collectAsStateWithLifecycle()` / `LifecycleStartEffect`
  inside the entry; no owner needs passing. The recipes wrap navigating clicks in
  `dropUnlessResumed { … }` to ignore taps during transitions.

### Entry decorators and ViewModel scoping
- `NavEntryDecorator(decorate = { entry -> …; entry.Content() }, onPop = { contentKey -> … })`
  wraps every entry: per-entry dependencies, objects shared by several entries, logging, cleanup
  on pop. **Not** for single-entry dependencies or anything wider than the back stack.
- **`rememberSaveableStateHolderNavEntryDecorator()` goes first** (unless another decorator provides
  a `SaveableStateProvider`). It makes in-entry `rememberSaveable` survive config change and death.
- **`rememberViewModelStoreNavEntryDecorator()`** gives each entry its own `ViewModelStore`, keyed
  by `contentKey`. A ViewModel is created when its key is pushed and **cleared when it's popped**.
  Without this decorator, ViewModels fall back to the Activity store and live forever.
- **Hilt plus a key argument.** Use assisted injection:
  `@HiltViewModel(assistedFactory = F::class) class VM @AssistedInject constructor(@Assisted key: K)`,
  then in the entry `hiltViewModel<VM, VM.Factory>(creationCallback = { it.create(key) })`. The
  ViewModel-store decorator already keys one instance per key instance, so no manual `key=`.
- **Sharing a ViewModel across entries** (parent and child in one flow): a custom
  `SharedViewModelStoreNavEntryDecorator` (recipe). The child declares its parent's `contentKey`
  in metadata and calls `viewModel(viewModelStoreOwner = LocalSharedViewModelStoreOwner.current)`.
  Use it only for genuine flows; app-wide shared data still belongs in a repository.
- **Results.** `rememberResultEventBusNavEntryDecorator()` (1.2.0). The sender calls
  `LocalResultEventBus.current.sendResult<T>(…)`; the receiver uses `ResultEffect<T> { … }` or
  `conflateAsState()`. **It's in-memory and doesn't survive process death.** To persist a result,
  put it in the key, `rememberSaveable`, or a ViewModel's `SavedStateHandle`.

```kotlin
NavDisplay(
    backStack = backStack,
    onBack = { backStack.removeLastOrNull() },
    entryDecorators = listOf(
        rememberSaveableStateHolderNavEntryDecorator(),   // first
        rememberViewModelStoreNavEntryDecorator(),        // per-entry ViewModelStore, cleared on pop
    ),
    entryProvider = entryProvider {
        entry<TrackDetail> { key ->
            val vm = hiltViewModel<TrackDetailViewModel, TrackDetailViewModel.Factory>(
                creationCallback = { it.create(key) })
            TrackDetailScreen(vm.uiState.collectAsStateWithLifecycle().value, onBack = { backStack.removeLastOrNull() })
        }
    },
)
```

### Save state and process death
- `rememberNavBackStack(start)` is saved across configuration changes **and process death**.
  Every key must be `NavKey` **and** `@Serializable`. If you keep the stack in a ViewModel or
  another holder instead, **you** serialize and restore the keys yourself.
- Hilt recipe caveat **(derived)**: the modular-Hilt recipe's `@ActivityRetainedScoped Navigator`
  holds `mutableStateListOf(start)`. That survives rotation but **not process death**. Back an
  injected navigator with a serializable stack, or restore it from saved state.
- Per-entry UI state uses `rememberSaveable` (with the saveable decorator). Business state is
  reloaded from the data layer by the ID in the key.

### Multiple top-level back stacks (bottom bar / rail)
- The guide and recipe pattern is UDF for navigation: a **`NavigationState`** (state only) plus a
  **`Navigator`** (events only).
  - `topLevelRoute` uses `rememberSerializable(serializer = MutableStateSerializer(NavKeySerializer()))`.
    The docs tell agents not to "fix" this to `rememberSaveable`. `backStacks =
    topLevelRoutes.associateWith { rememberNavBackStack(it) }` gives one saved stack per tab.
  - **Exit through home**: the visible entries are the start stack plus at most the current tab's
    stack. Each stack has its **own** decorators via `rememberDecoratedNavEntries(...)`, passed to
    `NavDisplay(entries = …)`.
- `Navigator.navigate(route)`: if `route` is a top-level key, switch tabs; otherwise push onto the
  current tab's stack. `goBack()`: at a tab's root, return to the start tab; otherwise pop.
- Bar selection: `selected = key == navigationState.topLevelRoute`. Reselecting a tab can go
  through a `SharedFlow` event that the tab root collects to scroll to top. This is UI-to-UI,
  which is allowed.
- Simpler alternative (`common-ui` recipe): a `TopLevelBackStack` that flattens the tab stacks.
  A tab's history is **discarded** when the user backs out of it.
- Not covered by the guide or recipes: more than one level of nested navigation, and destinations
  that **move between stacks**.

### Scenes (dialogs, sheets, multi-pane)
- A `Scene` renders **one or more** entries (`key`, `entries`, `previousEntries` for predictive
  back, `content`, `metadata`). `SceneStrategy.calculateScene(entries)` returns a `Scene` or `null`
  to pass. `NavDisplay(sceneStrategies = …)` tries strategies in order, then falls back to
  `SinglePaneSceneStrategy`.
- Custom scenes **must implement `equals`/`hashCode`** over key, entries and previousEntries,
  without callbacks (a `data class` usually works). Equality drives transitions, lifecycle and
  overlays.
- Entries opt into a layout through **metadata**: a `NavMetadataKey<T>` plus `metadata { put(K, v) }`,
  wrapped in helpers such as `listPane()`.
- Dialogs: `entry<K>(metadata = DialogSceneStrategy.dialog())` plus `DialogSceneStrategy()`.
  Bottom sheets: copy `BottomSheetSceneStrategy` from the recipe (it's not in the core library).
- Material adaptive (`adaptive-navigation3`): `rememberListDetailSceneStrategy()` with
  `listPane(detailPlaceholder = …)` / `detailPane()` / `extraPane()`. Key a list-detail scene by
  the list's `contentKey` so a detail swap recomposes instead of animating.
- **Put custom scenes in their own module.** Features need the metadata helpers; `:app` needs the
  strategies.

### Modular entry builders, Hilt @IntoSet, injected Navigator
- Split each feature into **`:feature:x:api`**, which holds the **keys** (and deep-link URI
  constants), and **`:feature:x:impl`**, which holds the screens, ViewModels, entry builders and
  DI. An `impl` may depend on another feature's `api` to navigate there, never on its `impl`.
- An entry builder is an extension, `fun EntryProviderScope<NavKey>.playerEntries(...)`. With few
  features `:app` calls each one. At scale, each impl contributes through **Dagger `@IntoSet`** in
  `ActivityRetainedComponent`, and `:app` iterates the set.
- Entries that navigate get an **injected navigator** (`@ActivityRetainedScoped`) through the
  `@Provides` parameter. Screens themselves get lambdas (`onTrackClick = { navigator.goTo(Detail(it)) }`),
  not the navigator.
- A shared `:core:navigation` module (the recipe calls it `common`) holds the `Navigator`, the
  `EntryProviderInstaller` typealias and the matcher types. `:app` provides `startDestination: NavKey`.

```kotlin
typealias EntryProviderInstaller = EntryProviderScope<NavKey>.() -> Unit   // :core:navigation

@Module @InstallIn(ActivityRetainedComponent::class)                       // :feature:library:impl
object LibraryNavModule {
    @IntoSet @Provides
    fun entries(navigator: Navigator): EntryProviderInstaller = {
        entry<LibraryKey> { LibraryScreen(onTrackClick = { id -> navigator.goTo(TrackDetail(id)) }) }
    }
}
// :app  @Inject lateinit var installers: Set<@JvmSuppressWildcards EntryProviderInstaller>
//       NavDisplay(..., entryProvider = entryProvider { installers.forEach { it() } })
```

### Deep links (Nav3 1.2.0+)
- Deep links are **separate from the UI**. Declare `<intent-filter>`s in the manifest. In
  `onCreate` **and `onNewIntent`**, build `DeepLinkRequest(intent)`, match it, and **build the
  initial back stack** from the result.
- Matchers: `UriDeepLinkMatcher(DeepLinkUri("…/track/{id}"), serializer<TrackDetail>())` (URI
  pattern to key fields), `StaticKeyDeepLinkMatcher(key, filters)`, and
  `.withBackStack { listOf(Home, it.key) }` (a `BackStackMatcher`) for a **synthetic back stack**.
  Custom matchers and `DeepLinkSerializer` handle custom argument types.
- `MatchResult` is `Comparable`, so take `matchers.mapNotNull { it.match(req) }.maxOrNull()`.
  `null` → default stack, `BackStackMatchResult` → its `backStack`, otherwise `listOf(result.key)`.
- Modularized: each feature contributes its matchers through `@IntoSet`
  (`DeepLinkMatcher<*, *>` or `BackStackMatcher<*, NavKey>`), and `:app` or the `Navigator`
  collates them (`Navigator.handleDeepLink(intent)` clears the stack and adds the match).

### Migration steps
From **Nav2** (`migration-guide`), as **one atomic change** (no coexistence):
(1) add deps; (2) routes become `@Serializable … : NavKey` (string routes go type-safe first);
(3) copy `NavigationState` + `Navigator` from the guide, same scope as the old `NavController`;
(4) `navigate` → `navigate`, `popBackStack` → `goBack`, `currentBackStackEntry` →
`backStacks[topLevelRoute].last()`, bar selection → `topLevelRoute`, `savedStateHandle` results →
`ResultEventBus`, lifecycle code moves into the entry; (5) `composable<T>` → `entry<T>`,
`dialog<T>` → `entry<T>(metadata = DialogSceneStrategy.dialog())`, delete `navigation<Base>`
wrappers and repoint them to the first child, `NavGraphBuilder.x()` → `EntryProviderScope<NavKey>.x()`;
(6) `NavHost` → `NavDisplay(entries = state.toEntries(provider), onBack = navigator::goBack)`;
(7) deep links → matchers; (8) remove Nav2.
The guide's **agent instructions**: check its assumptions first (a stack per top-level route,
state kept across switches, exit through home, atomic). For recipe-covered features (sheets,
injected destinations, ViewModel arguments, results), read the recipe and **confirm a plan with
the user**. For unsupported ones (nesting deeper than one level, destinations shared across
stacks, custom destination types), **stop and ask**. Have navigation tests in place first.

From **state-based navigation** (an enum plus `when`, no `NavHost`) **(derived)**: steps 3–4 have
no `NavController` to replace, so it's simpler.
1. Turn each enum value into a `@Serializable data object/class : NavKey`, putting any ID it
   carries into the key.
2. Swap the `var screen by remember/mutableStateOf(Enum)` for `rememberNavBackStack(start)` (or
   `NavigationState` if there are tabs).
3. The `when (screen)` branches become `entry<K> { … }` blocks, one-for-one.
4. Turn `screen = X` assignments into `navigator.navigate(X)` and hand-written back handling into
   `onBack`.
5. Add the two decorators so each screen's ViewModel is scoped to its entry.
6. Only then extract entry builders into `:feature:*:impl` modules.

## KMP-ready module structure
Sources: `kb://JetBrains/kotlin-multiplatform-dev-docs/topics/development/multiplatform-project-recommended-structure`,
`kb://android/topic/modularization/patterns`

- **App entry points live in their own modules** and depend on shared modules. With **AGP 9+**,
  separating the Android app entry point from common code is **mandatory** for KMP. Shared
  Android-side modules use `com.android.kotlin.multiplatform.library` with a `kotlin { androidLibrary { … } }`
  block instead of `android {}` / `androidTarget {}`.
- Split shared code into **`sharedLogic`** (business logic, no Compose) and **`sharedUI`** (Compose
  Multiplatform) when any target might use native UI. Add a **`core`** module for code shared with
  a server (models, validation). A single `shared` module is enough only if every app shares both.
- **What qualifies as shared logic:** code that is both **UI-independent and
  platform-independent**. The docs' test case: a pure time-calculation function moves, but a data
  class that references a Compose `DrawableResource` can't, because it's UI-bound.
- **Applied to this repo (derived)**, candidates for pure-Kotlin modules that could move to
  commonMain (plain JVM libraries today; the guide records the decision): domain models
  (`Preset`, `VisualSettings`, `AudioFeatures`, `SceneParams`), repository and data-source
  **interfaces**, use cases, pure audio→visual mapping math, settings and preset serialization
  (`kotlinx.serialization`), and feature `api` modules holding `NavKey`s.
- **Stays in Android modules** behind those interfaces: Media3, MediaStore/ContentResolver,
  Oboe/JNI, GLES/EGL, wallpaper and dream services, Billing, and anything using `Context`,
  `Uri` or `R`.
- KMP-safe libraries: coroutines, serialization, DataStore, ViewModel, Navigation 3 (its runtime
  lives in `navigation3-runtime/src/commonMain`; `DeepLinkUri` is a typealias of `android.net.Uri`
  only on Android). **Verify** the artifact publishes a JVM target before making a
  `:feature:*:api` a Kotlin/JVM module; otherwise keep `api` a thin Android library.
- Keep `android.*`, `Context`, `R` and Compose types out of models and keys. Use `String` or `Long`
  IDs (not `Uri` or `Parcelable`). Hilt `@Module`s stay on the Android side.
