# Kotlin target around the fosfora-shaped engine (draft)

Companion to [ADR 0006](../adr/0006-fosfora-aligned-engine.md). Draft of 2026-11-05 at `cf7e88f`; the Customize grouping below will be refined from the fosfora UI study (research session "fosfora params, bindings, modulation, UI grouping").

## Dependency direction

```
:feature:* (Compose)  ─►  :core:domain  ─►  :core:data  ─►  :engine:bridge  ─►  libgeode.so (core/api)
        │                     │                 │
        └──────────────► :core:model ◄──────────┘          (pure Kotlin, JVM)
```

- **`:core:model`** (JVM): `Look` (style id, base params by param id, routes, macros, layers, transition, shader, milk ref), `Route`, `ParamId`, `SourceId`, the Studio document (`editor/*` minus its `R`, Media3 and `data` imports). Value types build only valid values; ranges come from the generated schema.
- **`:core:data`**: `LookRepository` is the only writer of `StateFlow<Look>`. It applies the Pro gate, undo and persistence in that order. Presets are named `Look`s in one versioned format (kotlinx.serialization with golden files). The ~45 stores move here out of `ui/`.
- **`:core:domain`**: use cases with no state: pick the next style (from `AutoVisualsController`), resolve engine settings for a surface (shared by the screen, wallpaper, dream and preview, replacing the copies in `VisualizerWallpaperService.kt:165-178` and `VisualizerDreamService.kt:247-271`), build a `RenderJob`.
- **`:engine:bridge`**: `EngineSink(renderer).bind(look, overlay, settings)` turns flows into typed commands on the one channel ([`core/api/COMMANDS.md`](../../core/api/COMMANDS.md)). Generated encoders. Replaces `ui/EnginePlumbing.kt`'s LaunchedEffects, `VisualizerRenderer`'s `@Volatile` fields and the process-wide pushes.
- **Overlays** (art palette, take replay, touch) are a separate flow that is never saved and never enters undo, the Kotlin side of base vs modulation. Today they write the user's look (`ui/TakeController.kt:254-265` → `ui/VisualSettingsController.kt:94-96`).

`PlayerSession` and `GeodeContainer` dissolve into these (ADR 0001 step 4).

## Customize: grouped by the signal layers, progressively disclosed

Today `CustomizeTab` mixes pages (Macros, Look, Style, Music, Modulate, Advanced) and roll groups (Shape, Colour, Motion, Effects, Sound response) in one enum (`engine/scenes/.../render/scene/CustomizeTab.kt:29-43`), and the deeper pages (`CustomizeSubPage`, `ui/CustomizeAdvanced.kt:122`) are grouped by implementation (camera, liquid, ripples, envelopes, waves).

Target: each level shows one layer of the engine, and each step down shows more of the same thing, never a different thing.

| Level | What the user sees | Engine layer |
|---|---|---|
| 1. Knobs | Six macros and the style | `core/signal/modulators` (macros) |
| 2. Look | The base values of the style's params, grouped by what they change (shape, colour, motion, space), only the ones this style declares | `core/signal/params` (base) |
| 3. Music | What moves each param: moments → actions, Auto (the style's DNA) or Mine | `core/signal/bindings` (routes, ownership) |
| 4. Modulate | The same routes opened up: source, transforms, depth; LFOs and envelopes as sources | `core/signal/modulators` + `bindings` |
| 5. Listening | How the music is heard: sensitivity, bands, silence | `core/sound` (tuning) |
| 6. Layers and code | Layer stack, transitions, shader source | `core/viz/frame` |

Pro gating stays in `pro/FreeTier` and is applied in `LookRepository`, not only in composables.
