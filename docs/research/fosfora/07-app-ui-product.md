# 07: fosfora as a product and as an application layer, and what Geode should take

Structure claims were checked against the Graphify index of the fork (`tessie1993/fosforatess` @ 2e85d3b). It confirms that `draw_shell` has one production caller, `FosforaApp::window_event` (`main.rs:1097`), and it lists the six `write_frame` implementers: the NDI, Spout, Syphon and v4l2 sinks, a test `CountingSink` and the headless `ClipEncoder`. The graph has no edges for `App::update`'s caller or for the harness's `draw_shell` call. Text search confirms both: `main.rs:376` and `ui/shell_harness.rs:315`.

Scope: what a user does in fosfora, how its app layer is built (App, main.rs, the egui shell, I/O subsystems), what its docs do, and what fits Geode, an offline Android app. Fosfora checkout @ `2e85d3b` (paths below are relative to it unless they start with `app/`, `core/` or `docs/`, which are Geode's). The engine side (hop analysis, schema, bus, frame graph) is covered in Geode's `docs/research/2026-11-05-fosfora-structure-plan.md` and is not repeated here.

---

## 1. The product

**Who uses it.** It's built for a desktop VJ or performer at a gig or on a stream, plus TouchDesigner and Resolume rig builders. The README pitch is "built for actually performing with … nothing to code, no account … open it, play music, and it reacts" (`README.md:19-23`). Its user is a person at a laptop with a projector, a MIDI controller, OBS or a video mixer. It ships 58 effects (`docs/GALLERY.md:11`), an 8-layer stack with 20 blend modes, node chains ("trama"), 85 audio features, and four ways out (NDI, Spout, Syphon, v4l2), plus recording.

**The main user journeys** follow the three workspaces of the v2.0 shell (`CHANGELOG.md:261-321`, `docs/QUICK-REFERENCE.md:1-25`):

1. **First 60 seconds.** A visual is already running when the window opens. The UI fades in after a second or two (`ui/overlay.rs:141 update_auto_show`), and the First-run tour starts once (`ui/tour.rs:1141 should_auto_start`). The README's three steps are "open, point it at your audio, play" (`README.md:75-91`). The one FAQ that matters is "the visuals aren't reacting", answered per OS (`README.md:205-216`). The tutorial's loop is "open → play music → pick an effect → fullscreen" (`docs/TUTORIALS.md:31-41`).
2. **Build a look (Build workspace).** You work in a layer stack drawn as pictures, each row being "the stack blended up to that layer", with Master on top. An inspector shows the selection, and a catalog drawer of effect thumbnails sits along the bottom with families, search and favourites. You can drag a picture onto the stack (`CHANGELOG.md:273-279`). The `assets/media/ui.webp` screenshot shows it: stack left, inspector with sliders each carrying **Bind / M / O**, output and audio meters right, catalog bottom, and a status strip with BPM, AUD, MIDI, OSC, WEB, V4L, NDI and FPS. A look is saved as a preset (the whole stack), and switching presets uses Cut, Dissolve or Morph plus "Keep moving" (`docs/QUICK-REFERENCE.md:52-56`).
3. **Perform (Perform workspace).** You get presets and layers on the left and the output in the centre with BPM. The second-display button goes to a borderless output window (`ui/shell.rs:1137-1192`). **Scenes** are a cue strip: each cue is a preset's picture with how long it holds, and the transitions are the joints between cards. A cue advances by hand (Space), on a timer, or every N beats, from MIDI clock or the audio beat (`scene/types.rs:46-54`, `ui/panels/cue_strip.rs:1-11`). You control it live with MIDI-learn ("click M, wiggle the knob"), OSC-learn, a phone browser remote, and the **binding matrix** (B), a full-screen patch bay from source to transforms to target (`README.md:150-182`).
4. **Set up I/O (Setup workspace).** Setup is a page list (Audio, Control, Outputs and streams, Sync, Appearance, Tutorials, General). Each device page shows its **state as a mark plus words** (a filled dot means on and working, a ring means on and waiting, a dash means off) and has an On/Off switch over its settings (`ui/setup/mod.rs:1-8, 88-113, 151-156`, `ui/setup/kit.rs:27-45`).
5. **Export.** There are two paths. (a) Live recording of the window to H.264, HEVC or AV1 in MP4 or MKV through an `ffmpeg` child process, with audio muxed through a FIFO (`recording/mod.rs:34-80`, `recording/encoder.rs:98-124,204`). (b) **Loop export** (`--render-loop spec.loop.json`): a mathematically seamless, beat-locked clip. The BPM snaps to an integer frame count, `frames = round(bars·4·60/bpm·fps)`. Audio is either `none` or `synthetic`, in which case kick envelopes are synthesized from phase arithmetic so frame 0 ≡ frame N bit-exactly. The codecs are HAP and HAP Alpha, ProRes 4444 and H.264/HEVC (`docs/loop-spec.md`, `docs/loop-export.md`). A spec is a few hundred bytes, so "sharing one IS sharing the loop" (`examples/loops/README.md`).
6. **Integrate.** `fosfora --signal` runs headless and broadcasts beats, bars, sections and key over OSC. Ready-made TouchDesigner `.tox` components consume it (`integrations/touchdesigner/`, `docs/TOUCHDESIGNER.md`).

**What users asked for** (the `CHANGELOG.md` arc, 57 releases from v1.0.0 on 2026-02-27 to v2.8.1 on 2026-11-01): v1.x piled on engine features such as particles, lattice, splats, NDI, Spout, Syphon, depth and trama. **v2.0 was the reckoning with the interface**: workspaces, pictures instead of lists, Setup as status pages, per-control Bind, four tours, interface scale, WCAG themes, "no state shown by colour alone", "no text smaller than 12 px" (`CHANGELOG.md:261-321`). Since then the work has been beat accuracy measured against datasets (v2.5–v2.8), TouchDesigner integration, and in Unreleased tempo-synced LFOs as binding sources and an A/V sync offset (`CHANGELOG.md:6-25`). The pattern: features first, then a forced UI rebuild once the product was too dense to learn. That is the same path Geode's ADR 0005 is on.

**What is distinctive.**
- **Pictures everywhere.** Catalog thumbnails, stack rows rendered as the blend so far, and cue cards are all live or rendered images rather than names.
- **Measured honesty.** The changelog quotes accuracy deltas ("Harmonix beat accuracy 0.731 to 0.737, 3 tracks fixed, none broken", `CHANGELOG.md:43-50`). `catalog/README.md` states which effects are near-black at their defaults.
- **Deterministic loops as a product feature**, not just a render setting.
- **Every control is bindable**, and a bound control names its source on its row ("◀ Kick", `CHANGELOG.md:288`).

---

## 2. App-layer architecture

### Ownership: one `App`, one `FosforaApp`
- `crates/fosfora-app/src/lib.rs:1-2` says "the engine as a library … the `fosfora` desktop binary (`main.rs`) is a thin shell over it". **It is not thin**: `main.rs` is 5,295 lines and `app.rs` 5,073.
- `struct App` (`app.rs:99-296`) has **74 `pub` fields**. Together they hold every subsystem and every bit of loose state: `gpu`, `audio`, `egui_overlay`, `midi` and `pending_midi_triggers`, `osc` and `pending_osc_triggers`, `web` and `pending_web_triggers`, `binding_bus`, `preset_store`, `layer_stack`, `compositor`, `post_process`, `master_postprocess(_previous,_modulation)`, `ndi`, `spout`, `syphon`, `link`, `recording`, `scene_store`, `timeline`, `transition_renderer`, `staged_switch`, `retiring`, `midi_clock*`, `shader_editor`, `trama`, `binding_matrix`, `quit_requested`, `status_error`, five webcam fields, `splat_loader`, `depth_thread` and more. `App::new(window)` (`app.rs:298-745`) builds all of it with a non-optional surface. `ARCHITECTURE-NOTES.md:88-97` names this as the coupling that blocks headless use.
- `FosforaApp` (`main.rs:47-60`) is the winit `ApplicationHandler`. It holds `Option<App>`, two file-dialog receivers and a debounced-save timer.

### The frame loop (`main.rs:375-4660`, one match arm of `window_event`, about 4,280 lines)
1. `app.update()` (`main.rs:376` → `app.rs:927-2016`, about 1,090 lines). The drain order is written in comments and is **last-write-wins by position**: audio features are drained first, then a watchdog and reconnect. After that comes **MIDI** (`app.rs:1061`), then **OSC** "runs after MIDI" (`:1075`), then **WebSocket** "runs after OSC" (`:1223`), then the **binding bus** "overrides direct mappings" (`:1319`). Next are async preset decodes and the staged switch, MIDI clock and Link, the **timeline advance** (`:1426`), OSC TX and the web 10 Hz state, media, webcam and depth frames, trama modulation, then `gpu::frame_prep::prepare_effect_layers` (`:1615`; Graphify confirms its only other callers are the headless `SceneRenderer::step`, `headless/scene_renderer.rs:764`, and the loop exporter's `LoopSession::render_frame_at`, `headless/loop_driver.rs:371`, so live, scene renders and `--render-loop` share one per-frame prep; `gpu/frame_prep.rs:1-9`), and last shader and `.pfx` hot-reload (`:1732`).
2. **Read snapshots.** DTOs are built inline in `main.rs`, for example a `ParticleInfo` literal of roughly 70 fields assembled from `ps.*` (`main.rs:407-~600`), plus `LayerInfo`, `SceneInfo` (`app.rs:4266`) and `postfx_matches`.
3. **Draw UI.** The code builds `ShellState { … }` (`main.rs:1057`) and calls `draw_shell` (`main.rs:1097`). It then draws the shader editor, the trama canvas, the binding matrix (`main.rs:1158`, with a closure building `BindingPanelInfo`), the tour (`main.rs:1209`) and the quit dialog, which is drawn inline in `main.rs`. The step ends with `end_frame` (`main.rs:1325`).
4. **Drain UI requests.** **150 `remove_temp(egui::Id::new("…"))` blocks** run from `main.rs:1326` to `main.rs:4624`, for example `"confirm_quit"`, `"open_shader_editor"`, `"recording_toggle"`, `"rec_codec_change"`, `"pending_effect"` and `"select_layer"`. The panels post them with `ctx.data_mut(|d| d.insert_temp(Id::new("…"), v))`. `particle_panel.rs` alone has 37 such posts, `layer_panel.rs` 24 and `obstacle_panel.rs` 23.
5. `app.render()` (`main.rs:4625` → `app.rs:4413-4749`).

**Input threads.** OSC, web, MIDI and audio reach the render thread through `inbound::bounded` queues that **drop the oldest message**, so the end of a fader drag survives a shader-compile stall (`inbound.rs:1-8, 43-58`, tested with concurrent producers at `:89-112`).

### The UI DTO and request pattern
- **`ShellState<'a>`** (`ui/shell.rs:70-118`) is "Everything the shell draws, in one struct rather than thirty positional arguments". It is **not a pure snapshot**. It carries `&'a mut AudioSystem`, `&mut MidiSystem`, `&mut OscSystem`, `&mut WebSystem`, `&mut ParamStore` and `&mut PostProcessDef`, so a panel can mutate a live subsystem directly, beside the request channel. The `Workspace` enum (`shell.rs:36-67`) is kept in egui temp data as a `u8` under `"v2_workspace"`.
- **Panels are pure-ish draw functions** (`fn draw_x(ui, &Info)`). Reads come through DTOs and writes through the temp-data requests, and only the shell knows geometry (`ARCHITECTURE-NOTES.md:173-186`). The notes give the cost of adding a setting: "panel fn + a place for it in the shell or a Setup page + settings field + **drain block in `main.rs`**" (`:185-186`).
- **The counter-example inside the same crate** is `ui/tour.rs`. Its `Anchor` is an enum, "so a step cannot name something nothing draws: the compiler holds the names and the shell tests hold the rest" (`tour.rs:6-10, 41-86`). Tour keys are stable strings, "never change one" (`tour.rs:99-108`).

### Feature gating (`lib.rs`, `Cargo.toml:191-246`)
- `default = ["desktop"]`. `desktop` pulls in winit, egui-winit, the snarl canvas, the code editor, notify, midir, tungstenite, rfd and ureq. `app`, `ui`, `midi`, `osc`, `web`, `recording` and `output_window` are gated at their `mod` lines (`lib.rs:21-80`). Without `desktop` only `ui::theme` survives, because settings persist a `ThemeMode` (`lib.rs:72-76`).
- `release` is exactly what the downloads ship (`Cargo.toml:217`). `link` is excluded because it makes the binary GPL, and CI checks that it is absent from `release` (`ARCHITECTURE-NOTES.md:165-171`).
- `--no-default-features --features decode` builds the lib for `aarch64-linux-android` for an XR frontend, and CI clippy-checks exactly that ("xr-lib" job, `ARCHITECTURE-NOTES.md:16-20`).
- The CLI knows which flags were compiled out: `KNOWN_FLAGS` and `validate_args_against` make "a compiled-out flag name the missing feature" (`main.rs:4703, 4879`, tests at `:5248-5295`). The non-window modes `--signal`, `--render-loop` and `--audio-test` exit before `EventLoop::new()` (`main.rs:4940-4990`).

### I/O subsystems (the pattern only)
- **Config per subsystem.** Each subsystem has `src/<mod>/types.rs` with an `XConfig` and its own `load()/save()` JSON under `paths::config_root()`. Version fields are never read; forward compatibility comes from `#[serde(default)]` (`ARCHITECTURE-NOTES.md:125-129`).
- **Outputs.** The `FrameSink` trait is deliberately not `Send`. A sink is built *inside* its sender thread by a `make_sink` factory, so slow SDK initialization never blocks the render thread and thread-confined handles need no `unsafe Send` (`output/sink.rs:40-60`). `OutputPipeline` owns the capture target, a bounded channel, the thread, an error channel, a frame counter and **dropped-frame counting**, with health polled into an `OutputState::{Idle,Running,Error}` that the UI shows (`output/pipeline.rs:14-56`). Recording follows the same shape with `RecordingState` (`recording/mod.rs:20-49`).
- **MIDI** is input only with clock (`midi/clock.rs`). **OSC** has an RX thread and fire-and-forget TX at 30 Hz. **Web** runs HTTP and WS on 9002 with an access key and QR code. **Media** decodes in the background with progress and Cancel (`CHANGELOG.md:309-310`, `app.rs:2305-2390`).

### The rest of the UI system
- **`ui/shell_harness.rs`** draws the whole shell headless, with no audio device, socket or GPU, "in the order `main.rs` does" (`:1-12`). Setup tests use it to assert, at 1206×760 and 1920×1080, that **every page's text stays inside its column and never over the output** (`ui/setup/tests.rs:20-55`).
- **The tour** has a spotlight with the anchor live and the rest dimmed and click-blocked, so a click elsewhere cannot change what the note describes. A `gate` drops requests that would leave the view, and every shortcut except Esc is ignored (`tour.rs:11-33, 646`). It has 25 tests, including `every_step_lights_its_anchor_on_screen` (`:1196`), `nothing_in_a_lit_area_leaves_the_step` (`:1275`) and `the_note_goes_where_there_is_room_and_never_over_the_anchor` (`:1507`).
- **Theme.** `tokens.rs` sets a 12 px text floor. A test **scans the UI source for literal `.size(N)` values** below it (`ui/theme/tokens.rs:16-80`). `palette.rs` runs WCAG 2.2 `contrast_problems()` over seven pairs: 4.5:1 for text, 3:1 for outlines. These are tested for every built-in theme and reported in the Appearance panel for custom themes (`ui/theme/palette.rs:186-205, 348-366`, `theme/custom.rs:99`).
- **Accessibility.** Reduced motion is detected per OS (`ui/accessibility/motion.rs`). It sets `animation_time = 0` and a **strict flash budget** (`app.rs:587-593, 4419-4422`). The focus ring follows WCAG 2.4.11 (`accessibility/focus.rs`). Status lights have a shape per state.

---

## 3. Excellent vs weak

**Excellent**
- **Product clarity after v2.0.** The three workspaces map to three intents (play, make, configure). Setup shows state in words. Pictures replace names. "Editing a cue never cuts to it" (`cue_strip.rs:9-11`): what you edit is kept apart from what is on screen.
- **Tests on the UI's *shape*.** The shell harness at two window sizes, tour anchors that must land on screen, a font-size source scan and a contrast matrix. These catch the bugs the owner actually complained about ("Kevin's pain was layout: rows cut off", `setup/tests.rs:20`).
- **Small seams that are well chosen.** `FrameSink` built on its own thread, `inbound::DropOldestSender`, and `frame_prep` shared by live and headless with the reason given ("frames that render without error and are silently wrong", `gpu/frame_prep.rs:5-9`).
- **Loop export as a contract.** A versioned spec, BPM snapping, synthetic beat-periodic audio, and best-effort crossfade modes that are opt-in and labelled `~xfade` (`loop-export.md:37-53`).
- **Docs as a product.** README with a FAQ and the first 60 seconds. A GALLERY of every effect at defaults on *the same* synthesized passage. A 17-chapter TUTORIALS, a QUICK-REFERENCE, and DIAGRAMS generated light and dark from `scripts/diagrams.py`. ARCHITECTURE-NOTES "Corrections to README/docs claims" (`:24-36`).

**Weak (frankly)**
- **`main.rs` is the god object's mouth.** One match arm runs about 4,280 lines, and 150 stringly-typed request drains plus inline DTO construction make up most of the file. Nothing checks that a request posted under `"rec_fps_change"` has a handler, or that the payload types match (`Option<u32>` vs `Option<u8>` is decided at each `remove_temp`). A typo is a silent no-op. Every new setting edits `main.rs`.
- **`App` is a 74-field bag of pubs.** Subsystems were extracted (`RecordingSystem`, `OutputPipeline`, `WebSystem`), but orchestration, cross-subsystem rules (lock checks, dirty marking, autosave) and loose flags (`quit_requested`, `status_error`, `pending_*_triggers`) stay on `App` or in the `main.rs` drains. Webcam logic alone takes about 15 methods on `App` (`app.rs:2426-2853`).
- **`ShellState` leaks.** It holds `&mut` live subsystems, so "panels are relocatable" is true for geometry but not for side effects. The UI can mutate `AudioSystem` directly, in parallel with the drain.
- **UI state lives in egui temp data under string ids** (`"v2_workspace"` as a `u8`, `"v2_setup_page"`, `"v2_stack_alone"`). This is a typeless global blackboard.
- **Hidden ordering.** Last-write-wins between MIDI, OSC, web and bindings is an order of statements, documented only in comments and in `TUTORIALS.md:1368-1374`. The tutorial says Web wins, which omits that the binding bus overrides all three (`app.rs:1319`).
- **Doc drift.** TUTORIALS still sends users to "the left sidebar" and "right sidebar" for Scenes, MIDI, OSC, Web, Outputs and Audio (`docs/TUTORIALS.md:301, 529, 792, 1088, 1145, 1232, 1278`), a layout removed in v2.1. ARCHITECTURE-NOTES says TECHNICAL.md has a stale feature count (`:26-28`).
- **Some accessibility code is dead.** `accessibility/keyboard.rs` (the `Shortcuts` legend) is `#[allow(dead_code)]` (`accessibility/mod.rs`), and the Linux reduced-motion detection shells out to `gsettings` at startup.

---

## 4. Geode today in comparison

**Journeys.** Geode is a music *player* first. Its tabs are Player, Library, Visuals, Studio and Settings (`app/src/main/java/dev/geode/ui/GeodeAppState.kt:20-35`). Its user is a listener on a phone who then makes a video for TikTok, Shorts or a Feed. The mapping to fosfora:

| fosfora | Geode | Note |
|---|---|---|
| Build workspace (stack, inspector, catalog) | Visuals sheet: Styles, Customize (7 pages, sub-pages), Presets, MilkDrop (`ui/VisualsSheet.kt`, `CustomizePanel.kt`) | Geode has no layer stack; Styles is a thumbnail list (`assets/thumbs/<id>.webp`) |
| Perform workspace + cue strip | Visuals full screen with gestures (`ui/VisualGestures.kt`), Auto director, visual playlist (`AutoVisualsController.kt:177 onDirectorMoment`), MacrosDrawer, take recording | Geode's "perform" is automatic (director) or gestural; there is no cue list with per-cue transitions or hold |
| Setup pages with status | Settings tabs (`AppSettingsTab.kt`, about 18 `*Settings.kt`) | No per-group state summary |
| Recording + loop export | Studio › Render, takes, `LoopRender` (`export/LoopRender.kt:41-73`: `LoopSpec(loopMs, crossfadeMs, fps)`) | Geode's loop is fosfora's *best-effort* mode, time-based crossfade only. It has no BPM or bar snap and no phase lock |
| Tours | `TutorialHost` + `ui/tour` (`TourSpot` enum, `Modifier.tourTarget`) | Same spotlight idea, also enum-typed; no test that each spot is marked |
| WCAG themes, font floor | `GeodeTheme.kt:86-95,152` contrast helpers; `test/.../ui/theme/SecondaryTextTest.kt` | One theme; no source scan for type sizes |
| Reduced motion | `GeodeMotionScheme` follows the system's "Remove animations" live; `core/viz/VisualSafety.cpp:25-33 apply(p, reducedMotion)` | **But the in-app renderer gets only the app toggle**: `EnginePlumbing.kt:123` sets `reducedMotion = gui.reducedMotion`, while the system value is combined only for the UI (`Crystal.kt:71`) |
| Docs: README, GALLERY, TUTORIALS, QUICK-REF | `Settings › Help` has 5 topics (`HelpSettings.kt`); `pages.yml` publishes `docs/help/` "when it exists", and it doesn't | Geode has no user manual, gallery or quick reference |

**App layer.**
- **`PlayerSession`** (`ui/PlayerSession.kt`, 1,370 lines, about 260 members) is Geode's `App`. It builds 16 controllers by hand, 12 of them through anonymous `Host` objects (for example `TakeController` at `:418-455` and `AutoVisualsController` at `:563-590`), and it owns repositories, `storeScope` and `shutdown` (`:1192-1212`). It is better factored than fosfora's `App`: the controllers own their logic, and the Host interfaces are typed seams rather than 74 public fields. But it has the same disease. **Cross-controller writes funnel through one `MutableStateFlow<VizUiState>`** (`PlayerModels.kt:27-47`), which controllers mutate via `Host.updateViz { … }` (`PlayerSession.kt:567`) or `visual.setSceneParams`. So who wins on a conflict is decided by call order, as in fosfora's drain.
- **Composables hold live engine handles**, as `ShellState` does: `CustomizePanel(viewModel: PlayerViewModel, visualizerView: VisualizerView, nav)` (`CustomizePanel.kt:111-115`) also pulls two more ViewModels inside itself.
- **One-shot events** `vizApply` and `morphFade` are `SharedFlow`s (AGENTS.md "breaks the guide"). This is Geode's version of the temp-data request.
- **The GL handoff is already newest-wins.** `VisualizerRenderer`'s `@Volatile` published fields (`engine/scenes/.../VisualizerRenderer.kt:23-82`) give fosfora's drop-oldest property by construction.
- **Navigation.** Visuals already has a typed Nav3 history (`ui/VisualsNavigation.kt`, `NavHistory`), which is better than fosfora's untyped temp-data `Workspace` and `Page`.
- **Layout tests.** The Compose UI tests are journeys and specific tabs (`androidTest/.../journey/*`, `ui/ReactivityTabTest.kt`). Nothing like the shell harness renders Visuals at several widths and checks bounds.

---

## 5. Recommendations for Geode

Costs: S ≤ 1 day, M a few days, L a week or more. Priorities: P1 do in wave 1, P2 next, P3 later.

### Product

**R1. Beat-locked loops (P1, M).** Let Studio › Render › Length › Loop offer **"N bars at this song's tempo"**: 1, 2, 4 or 8 bars, snapped to an integer frame count. The maths is fosfora's `frames = round(bars·4·60/bpm·fps)` and `effective_bpm = bars·240·fps/frames` (`docs/loop-spec.md:22-29`). Show "requested 128.0 → 127.98 BPM (450 frames @ 60 fps)". Take the BPM from native `Rhythm` (it is already in `GeodeSoundFrame`), and start at a downbeat (`barCount` steps) so the loop starts on "one". Keep today's crossfade as the labelled fallback, as fosfora does with `~xfade`.
- Files: `export/LoopRender.kt` (`LoopSpec` gains `bars`/`bpm`), `ui/LoopRenderSheet.kt`, `editor/StudioProject` (loop as a project kind per ADR 0005).
- A pure JVM test of the snapping goes in `app/src/test`.
- A later step is a **synthetic-groove loop** (no song: beat-periodic accents), reusing `androidTest/.../thumbs/SyntheticGroove.kt`'s idea in native code for a seamless, beat-locked wallpaper or loop. That needs a phase-locked contract in `core/viz` (P3, L).

**R2. Cue list on the visual playlist (P2, M).** Today the playlist steps on an interval or on director moments. Make it a **strip of cards** (style thumbnail, name, hold), with a **transition per joint** (Geode already has `TransitionStyle` and durations in `VizUiState`) and advance modes **Manual / Timer / Every N bars**, where bars come from native `barCount` (Geode's analogue of fosfora's MIDI-clock BeatSync).
- Also take the cue strip's rule: **selecting a cue to edit never cuts to it**; double-tap or "Play from here" jumps.
- This is Geode's "Perform" for a phone, and Auto's section moments can be one more advance source.
- Files: `ui/AutoVisualsController.kt`, `ui/VisualsQuickSettings.kt`, `data/AutoVisualsPrefsStore`.

**R3. "What drives this" on every Customize row (P2, S–M).** fosfora's bound slider shows "◀ Kick" and a small triangle at the bound value, and every row has a **Bind** that opens the matrix with that control picked (`CHANGELOG.md:286-288`, `QUICK-REFERENCE.md:66-68`). Geode's routes and DNA already know each target's source (`core/viz/Reactions`, `ReactionDna`). Expose "driven by: Bass (DNA)" on `CustomizeTabs` rows, and a row action "React to music…" that opens the Music or Reactions sub-page with that target preselected. This fits `docs/research/MOD_MATRIX_UX.md`.

**R4. Settings groups say their state in words (P2, S).** Copy Setup's `Status{mark, words}` (`ui/setup/kit.rs:27-45`). Examples: "Audio: Bit-perfect · USB DAC", "Wallpaper: On · still without Pro", "Capture: Off", "Auto-director: On". Use a shape per state, never colour alone. Files: `ui/AppSettingsTab.kt` (`SettingsGroup`).

**R5. Safety: feed the system's reduced motion to the renderer (P1, S).** `EnginePlumbing.kt:123` and `VisualizerWallpaperService.kt:166` pass only `gui.reducedMotion`. Pass `gui.reducedMotion || systemReducedMotion`, the value `Crystal.kt:71` already computes, as fosfora does at startup (`app.rs:587-593`), and consider a stricter flash budget under it (`app.rs:4419-4422`). A photosensitivity setting that ignores the OS switch is the kind of thing Play reviewers and users notice.

**R6. Docs a user can read offline (P1 for Help, P2 for the rest; S–M).** Geode has no INTERNET permission, so help must ship in the APK. Follow fosfora's structure:
- **First 60 seconds and the one FAQ** ("the visual isn't reacting": music access, capture permission for other apps, mic) as the top Help topics in `ui/HelpSettings.kt`.
- **QUICK-REFERENCE**: one page for the gesture map (`BehaviourPrefs.actionFor`), sheet heights, and what Free vs Pro gets (`pro/FreeTier`). Generate it from the enums, so it cannot drift as fosfora's TUTORIALS did.
- **GALLERY**: extend `render-thumbs.yml` and `StyleThumbnailRenderTest` to also write a 2-bar animated webp per style on the same synthetic passage, which is fosfora's "identical passage" trick (`docs/GALLERY.md:3-5`). Use it for `docs/site` and the Play listing.
- **DIAGRAMS**: generate light and dark SVGs from one script, as `scripts/diagrams.py` does, for the audio chain and render loop in `docs/AUDIO_CHAIN.md`.
- Add a **"Corrections / sources of truth"** section to AGENTS.md, like `ARCHITECTURE-NOTES.md:24-36`.

**R7. Measured style catalogue (P3, M).** fosfora's `catalog/effects/*.md` come from headless renders over a quiet → loud track, with motion and luma stats. They found six effects near-black at their defaults (`catalog/README.md:22-27`). Geode's thumbnail job already renders every style against a groove. Have it also emit per-style mean luma and motion into a JSON, and fail or flag styles that are near-black or static at defaults. The auto-director and "Auto's sources" can use it to avoid dead looks.

**R8. Name unknown saved ids (P2, S).** fosfora logs a misspelled binding target by name and `--validate` lists the valid ones (`CHANGELOG.md:24`). Geode's route and preset codecs (`render/ReactionRouteCodec`, `PresetStore`) should `RingLog` an unknown target or style id rather than drop it silently, and keep it in the file as written.

### App-layer patterns to untangle `PlayerSession`

**P-A. One typed command channel per state owner, not stringly requests (P1 design, M to apply to Visuals).** Take fosfora's *idea* (the UI posts requests; one place applies them in a fixed, documented order) without its *mistake* (150 string keys). For Visuals:
- Write `sealed interface VisualCommand { SelectStyle, SetParams, ApplyPreset, ApplyMilk, StepPlaylist, DirectorMoment, … }` and one `VisualStateHolder.dispatch(cmd)`. It is the only writer of `VizUiState`, with the precedence rules in code: the user's hand beats Auto, Auto beats the playlist timer, the paywall gate comes before any of them.
- Controllers stop calling `Host.updateViz {}` and dispatch commands instead. An exhaustive `when` is the compile-time version of fosfora's tour `Anchor` enum. A JVM test of `dispatch` replaces "order of statements in `main.rs`".
- This is also the shape ADR 0001 step 4 needs: `VisualStateHolder` becomes the `@Singleton` repository.
- Files: `ui/PlayerSession.kt:418-590` (the Host wiring), `ui/PlayerModels.kt`, `ui/AutoVisualsController.kt`, `ui/TakeController.kt`, `ui/TrackColorController.kt`.

**P-B. Screens take state DTOs and action lambdas, never live engine objects (P2, M).** `ShellState`'s `&mut AudioSystem` is the leak to avoid. Make `CustomizePanel` take a `CustomizeUiState` plus an `onAction: (CustomizeAction) -> Unit`. Hoist the `VisualizerView` calls (`CustomizePanel.kt:111-115`) into `EnginePlumbing` or the ViewModel. This also makes the panel testable without a GL surface, which is what fosfora's harness achieves by drawing "without a GPU".

**P-C. Move the writes when you dissolve the god object, not only the reads (P1, as guidance for ADR 0001 step 4; S to record).** fosfora shows the failure mode. v2.0 moved *reads* into `ShellState` (`ARCHITECTURE-NOTES.md:197-198`, "ShellState replaced the old positional-argument draw_panels"), yet the *writes* stayed as a growing drain in `main.rs`. Record this in ADR 0001 § Consequences: each controller that becomes a repository takes its commands and its precedence rules with it, and `PlayerSession` must lose `Host` lambdas, not gain forwarding functions (`PlayerSession.kt:592-598` already has `fun addToVizPlaylist(...) = autoVisuals.addToVizPlaylist(...)`, the same forwarding shape).

**P-D. A subsystem template: Config + System + State + panel (P2, S to document).** fosfora's rule (`ARCHITECTURE-NOTES.md:125-136`) has three parts: `XConfig` with its own file, an `XSystem{config, pipeline}`, and a lifecycle `State::{Idle, Running, Error(msg)}` the UI reads. Geode's capture, bit-perfect USB, wallpaper source, export and second screen would each benefit from an explicit `State` with words, which feeds R4. For the export encoder, fosfora's "construct the sink on its own thread; report health; count dropped frames" (`output/sink.rs:40-60`, `output/pipeline.rs:14-37`) is a good checklist for `EncoderSurface` and `ExportRun`. Write the template into `docs/ANDROID_ARCHITECTURE_GUIDE.md`.

**P-E. A shell harness for Compose (P1 for the tour test, S; P2 for layout, M).**
- (a) A test that each `TourSpot` is marked by its screen: launch each destination in a journey and assert `TourTargets` holds a rect on screen for every spot of that chapter. Today an unmarked spot silently degrades to a card (`ui/tour/TourTargets.kt:17-24`). Mirrors `tour.rs:1196 every_step_lights_its_anchor_on_screen`.
- (b) Render `VisualsHub` portrait and landscape and the Customize sheet at compact, medium and expanded widths, and assert no text node's bounds leave its container or overlap the stage icons. Mirrors `setup/tests.rs:20-55`.
- (c) A JVM source scan for literal `.sp` sizes below the type floor, plus a contrast test over every `GeodeTokens` text-on-surface pair. Mirrors `tokens.rs:27-80` and `palette.rs:348`.
- Files: `app/src/androidTest/.../journey/`, `app/src/test/.../ui/theme/`.

**P-F. Write the precedence order down where it is enforced (P2, S).** fosfora's MIDI → OSC → web → bindings order is at least written at each drain site. Geode's equivalent precedence is user hand → Music hand mask → DNA → director (`AGENTS.md` data-flow §3, `core/viz/Reactions` `hand::`). It lives in native code. The *Kotlin* precedence (Auto vs playlist vs take replay vs paywall preview, `PlayerSession.leaveScenePreview`) is implicit. Put it in P-A's `dispatch` KDoc and a test.

**P-G. Keep the lib honest with CI builds per target (P3, S).** fosfora clippy-checks its no-desktop Android lib build on every PR (`ARCHITECTURE-NOTES.md:16-20`). When ADR 0001 step 2 creates `:core:model` as JVM, add a CI step that builds it alone with no Android on the classpath. `tools/layer-gate.sh` covers imports but not the build.

---

## 6. Do not copy

- **Network I/O of any kind.** That covers the OSC RX and TX (`osc/`), the web remote and its QR access key (`web/`, `assets/web/control.html`), the `--signal` broadcast and the TouchDesigner integration, NDI, the RTMP and network camera streams, Ableton Link, and `download.rs` (ureq HTTP downloads of depth models and splat demos). Geode removes INTERNET in the manifest by design (`AGENTS.md` Conventions). A phone-as-remote doesn't apply: Geode *is* the phone.
- **Desktop-only plumbing.** That means Spout, Syphon, the v4l2 virtual camera, the borderless second-window manager (Geode has `SecondScreenCanvas` through Android Presentation already), keyboard-shortcut-centric UX (D, F, B, C, [ ], Space, T), `rfd` file dialogs, and shelling out to `gsettings` or `date`/`mkfifo`.
- **The recording architecture.** An `ffmpeg` child process with an audio FIFO (`recording/encoder.rs:98-124, 204`) doesn't fit: Geode uses MediaCodec, MediaMuxer and Media3 Transformer. HAP, HAP Alpha and ProRes are desktop VJ codecs. Keep H.264/HEVC, plus alpha only if a real Android target needs it.
- **egui temp-data requests and `main.rs`'s drain shape.** Take the request idea (P-A), not its string keys, inline DTO literals or the 4,000-line match arm.
- **A 74-field `pub` App.** Geode's Host interfaces are already better seams; finish dissolving them per ADR 0001.
- **The patch-bay binding matrix and the trama node canvas.** A phone isn't a patch bay; `docs/research/MOD_MATRIX_UX.md` already chose rows.
- **An 8-layer stack with 20 blend modes as the main way to make a look.** Geode's DNA, macros and director carry that weight on a phone.
- **The screenplay pipeline** (`docs/EXPERIMENTAL.md`): it calls a paid cloud API.
