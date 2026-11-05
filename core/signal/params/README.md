# core/signal/params (skeleton, empty)

Part of [`core/signal`](../README.md). Nothing here yet.

**Owns**
- `ParamSchema.inc`: one row per param: stable ASCII id, type, default, min, max, lerp rule, scope (global, per-style clock, band…), roll group, uniform name. English labels are not ids; Kotlin maps id → string resource.
- `ParamStore`: `base[]` (what the user, a preset or a keyframe set; saved) and `mod[]` (this frame's modulation; never saved, cleared each frame). `effective()` = base ⊕ modulation, clamped once by the schema range.

**Generates from the schema (replacing hand copies, as of `cf7e88f`)**
- `core/viz/Params.hpp` struct, `ParamsFields.cpp` wire order, the lerp list and `set`/`get` in `Params.cpp`
- `engine/scenes/.../render/scene/SceneParams.kt` defaults and `render/bridge/SceneParamsCodec.kt` (`pack` writes 165 hand indices today)
- the scope and range rows in `core/viz/Catalogue.cpp` (keyed by English label today: `{"Speed", Scope::SceneClock}`, `:180`) and `ParamRandomizer`'s keys
- Customize slider ranges (literals in `ui/CustomizeTabs.kt`, `ui/CustomizeAdvanced.kt`) and `editor/AnimatableParams.kt`
- the uniform upload in `core/viz/scenes/ShaderScene.cpp` (`set1f("uSpeed", p.speed)` by hand)

**fosfora:** `params/types.rs` (`ParamDef`), `params/store.rs` (`values` vs `modulated`, `clear_modulation`, `effective`, pack in definition order).

**Test that proves it:** every generated file matches the schema; `effective()` of base + a +∞ modulation equals the max, and a later −∞ can still pull it back (today an early clamp cannot be undone by a later stage).
