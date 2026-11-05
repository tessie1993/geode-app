# core/signal/modulators (skeleton, empty)

Part of [`core/signal`](../README.md). Nothing here yet.

**Owns:** every thing that moves a param, rebuilt as either a **source** (a value per frame the bus can route) or a **route producer** (default routes the bus merges). None writes `SceneParams` directly any more.

| Today (`cf7e88f`) | Becomes |
|---|---|
| `core/viz/Lfo` (3 slots, `applyLfoTarget` clamps per target, `Lfo.cpp:54-60`) | LFO sources + user routes |
| `core/viz/Adsr` | envelope sources + user routes |
| `core/viz/Macros` (six knobs; process-wide `macros::global`; also rewrites sound tuning) | macro sources + macro route set; tuning becomes its own command, not a side effect |
| `core/viz/Director` (lerps the look, its own route engines, camera moves; owns `Emotion`) | route producer + camera commands; `Emotion` moves to `core/sound/hop` |
| `core/viz/NoteResponse` (multiplies zoom and brightness unclamped, `NoteResponse.cpp:502,514`; drives `uKnob` through a host callback) | sources (note colour, knobs) + routes |
| `core/viz/MotionField` (two copies: renderer's and each `ShaderScene`'s, fed different frames) | one source set, computed once, + default routes |
| `core/viz/TouchField`, Liquid gestures, `GyroLook` | touch and motion sources |
| `CameraRig` reading the sound frame itself | reads sources like everything else |

Process-wide globals (`macros::global`, `director::global`, `note_response::globalEnabled`, `hand::global`) become per-renderer config sent through [`core/api`](../../api/COMMANDS.md), so an export snapshots them once.

**fosfora:** `bindings/lfo.rs`, `bindings/sources.rs` (`collect_audio` and the other families).

**Test that proves it:** each modulator alone, routed to one param, produces the same curve it does today (golden), and two modulators on one param sum before the single clamp.
