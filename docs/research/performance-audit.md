# Performance audit (2026-11-13)

What makes the app slow, leak or hold memory, ranked by likely impact. Prechecked by reading only: nothing was built or run (owner rule).
Out of scope: the heat and thermal throttle (Thermal, Quality, Pacer), and capture from other apps (PlaybackCaptureService, the notification listener, the capture pump).

## Fixed in this change

### 1. Macros tab recomposed whole at about 30 Hz (high, Compose)

- `ui/CustomizeMacros.kt:93-99` (before): `val live by produceState { … delay(33) … }` was read in `MacrosTab`'s own scope.
- Each poll returns a new map, and while a wave or an envelope moves a knob the map differs every tick. Every tick then recomposed the tab: `filter`, `chunked`, every `Row`, every `MacroKnobDial` and its `KnobMenu`.
- Fix: the poll stays as a `State`. Each dial takes a `() -> MacroLiveMod?` and reads it only in its `Canvas` draw. The TalkBack text reads the source kind through `derivedStateOf`, which changes rarely.
- A tick now invalidates only the dials' draw. Behaviour is unchanged: the same arcs and the same spoken text.
- Docs: "Defer reads as long as possible" and lambda-based reads, in [Compose performance best practices](https://developer.android.com/develop/ui/compose/performance/bestpractices#defer-reads); `derivedStateOf` for a value that changes less often than its input, on the same page.

### 2. `HistoryStore` read and parsed its JSON on Main at cold start (medium, main-thread I/O)

- `data/HistoryStore.kt` (before: `init { synchronized(lock) { readLocked() } }`).
- `playback/PlaybackEngine.kt:127` builds it inside `PlaybackSession`. `GeodeContainer.warmPlaybackPrefs` says that constructor runs on Main the first time the UI or the service acquires the player.
- The read was `history.json`, up to 400 entries, read whole and parsed with `org.json` on that thread.
- Fix: the read is queued as the first task on the store's own single writer thread. Every public reader and writer waits for it first (`awaitLoad`), normally long after it has finished. Writes stay ordered after the load because they share that thread.
- Docs: [Keep your app responsive: avoid I/O on the main thread](https://developer.android.com/topic/performance/vitals/anr#main-thread-io); StrictMode `detectDiskReads`.

## Remaining, ranked (proposed, not changed)

| # | Impact | Where | Evidence | Suggested fix |
|---|---|---|---|---|
| 3 | Medium | `data/MacroStore.kt:90` | The same 33 ms poll does a JNI call that returns a text table, `MacroLiveTable.parse`, and builds a new `Map` on Main, 30 times a second while Customize › Macros is open. | Have native fill a reused `FloatArray` (4 floats per knob) through `geode_api.h`, and drop the text round-trip. |
| 4 | Medium | `cast/CastStream.kt:185-228` | The cast render loop is a JVM thread (`cast-render`) making about 8 JNI calls per frame (frame clock, tee draw, `stageRun`, audio pump). This is engine work in Kotlin off Main, against ADR 0003. | Port the loop to `core/output` beside `CastAudioPump`, and keep the encoder hand-off in Kotlin. |
| 5 | Medium | `ui/MyVideosTab.kt:196-205` | `VideoThumbnail` has no cache. Each item that scrolls back into the lazy grid runs `loadThumbnail` again, or a `MediaMetadataRetriever` frame decode, on IO. | Use a small `ByteBudgetCache<String, ImageBitmap>`, as `ArtworkCache` does, and clear it in `di/MemoryTrim`. |
| 6 | Low–medium | `ui/RenderScopePicker.kt:179,230`, `ui/AppShell.kt:1196,1225`, `ui/CustomizePanel.kt:444` | Lazy `items(...)` without `key`: a filter or reorder recomposes by position and loses item state. | Add `key = { it.uri }` or `{ it.name }`. Check first that the keys are unique, because a duplicate key throws. |
| 7 | Low–medium | `stage/Stage.kt:333-353` | While the stage draws, Main posts a Choreographer callback every vsync only to poll `stageTakeEvents` and `stageState` (2 or more JNI calls a frame on Main). | Have native wake Main only when an event bit is set: an `ALooper` fd or a posted callback from the stage thread. |
| 8 | Low | `core/viz/RendererFrame.cpp:287-301` | Each frame, under `stateLock_`, the renderer copies `requestedSceneId_` and `layerSceneId_` into `std::string`s on the GL thread. These are short and usually fit SSO, but not guaranteed. | Compare and copy only on a generation change, as `LiveFrame` does with `Latch<std::string>`. |
| 9 | Low | `data/FavouritesRepository.kt:47` | The prefs listener is never unregistered. SharedPreferences holds it weakly and there is one per `PlayerSession`, so there is no real leak. | Unregister in `PlayerSession.release` for tidiness. |
| 10 | Low | `playback/PlaybackEngine.kt:145` vs `:206` | The `errors` listener is added but not removed in `release()`. This is harmless because `player.release()` follows. | Remove it for symmetry. |

## Checked and found sound (no action)

- Scratch: `GeodeContainer.sweepStaleRenderScratch` covers every cache-dir scratch writer: `geode_aac_` (`export/AudioTranscoder.kt:113,288`), `geode_loop_` (`core/output/android/LoopRenderHost.cpp:44`) and `studio-record-` (`data/RecordFiles.kt:83`). Pending MediaStore rows and the retired `filesDir/analysis` are also swept.
- `cacheDir/share/templates` is overwritten per template, and the OS may reclaim it. The widget art is a single file.
- Program binary cache: capped at 24 MB and 512 entries and pruned once per process (`core/viz/ProgramBinaryCache.cpp:32-34,337`).
- Artwork: `ArtworkCache` is byte-budgeted (an eighth of the heap, at most 24 MB), decodes with at most 2 in parallel, and is cleared on trim. `SessionBitmapLoader` keeps one slot and shuts down its executor. Decodes use `inSampleSize`.
- Listeners and receivers: display, sensor, content observer, audio device, widget screen-on, wallpaper and dream receivers, and `CastMirror.onDirectorMoment` are all paired with removal.
- No JNI global refs in `app/src/main/cpp` or `core/`.
- Position ticks reach Compose as `() -> Long` lambdas (`ui/AppShell.kt:317`). `PlayerSession` polls at 2 Hz; `RecordController` ticks at 4 Hz.
- Native frame path: `stage/CommandQueue.hpp` does not allocate after construction. `PcmHandoff`, `geode_viz_feed_ring` and the `NativeDspProcessor` buffers are reused. Scene residency is trimmed on `memoryTrim`.

## Rule 5 (engine logic in Kotlin)

- No audio analysis or audio-reactive maths was found in Kotlin outside the out-of-scope capture path.
- The one engine-shaped piece of Kotlin found is the cast render loop (#4), noted above for porting.
