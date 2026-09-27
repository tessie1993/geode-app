# Liquid glass design system

> The implementation this describes (`app/src/main/java/dev/geode/ui/glass/`) has been removed;
> the shell is being rebuilt on `dev.geode.nav` (see `docs/NAVIGATION.md`). The references and
> the notes below stay as source material for the design system that replaces it.

Source of truth for the "liquid glass" UI (`app/src/main/java/dev/geode/ui/glass/`). Ten still
references (`ref-01.jpg` … `ref-10.jpg`) and four video frame strips (`video-v1.jpg` …
`video-v4.jpg`) are committed alongside this file so every worker and reviewer sees the same
material. `ref-05` is the target phone layout; `ref-06`–`ref-10` show the touch behaviours;
`ref-04` and the video strips show the water background; `ref-01`–`ref-03` are the control
catalogue.

## Tokens (`GlassTheme.kt`)

`GlassPalette`: `base` (`#8C95BB`), `baseLight` (`#A4ACCB`), `baseShadow` (`#6F78A3`), `mint`
(`#BFEBD8`), `lavender` (`#CDBDF0`), `peach` (`#F6CDB2`), `pink` (`#F3BFD8`), `sky` (`#BFD8F2`),
`glassFill` (white α 0.32), `glassRim` (white α 0.55), `glassShadow` (`baseShadow` α 0.35),
`textPrimary` (white α 0.92), `textSecondary` (white α 0.66).

`GlassShapes`: `pill` (50%), `bubble` (circle), `tile` (28 dp), `sheet` (32 dp top corners), plus
`materialShapes()` for the M3 `Shapes` used by `GlassMaterialTheme`.

`GlassMotion`: `PRESS_SCALE` 0.96, `SPRING_STIFFNESS` (`Spring.StiffnessMediumLow`),
`RIPPLE_DURATION_MS` 900, `HOLD_THRESHOLD_MS` 350, `INK_DURATION_MS` 1600, `DROP_STRETCH_MAX`
1.35, `BUBBLE_DRIFT_PERIOD_MIN_MS`/`MAX_MS` 12000–20000.

`GlassElevation`: `rimWidth` 1 dp, `shadowBlur` 18 dp, `glowBlur` 24 dp, `SPECULAR_ALPHA` 0.5.

`GlassTypography`: one family (`MaliFamily`/`MysteryQuestFamily` from `res/font`, the same
resources `StoneTheme.kt` loads — no new fonts added), light/normal weights, titles 28 sp body
16 sp labels 13 sp, scaled by `GuiPrefs.textScale`.

`GlassMaterialTheme(gui, content)` builds one M3 `darkColorScheme` from these tokens (so every
stock M3 component — dialogs, text fields, snackbars — reads correctly even before a screen is
rewritten onto a dedicated `Glass*` primitive) and provides `LocalGlass` with the per-viewer
`reducedMotion`/`glassTint`/`glassOpacity`/`bubbleDensity`/`liquidMotion` settings from
`GuiPrefs`.

## The background is water, not a marble swirl

The background is a simulated water surface (`WaterField.kt` + `LiquidBackground.kt`), driven by
the four reference videos (`video-v1..v4.jpg`): pastel periwinkle base with caustic light lines
that drift, ripple rings from taps that bob and tilt every floating control, ink that spreads and
swirls with the flow on a hold, and drag/scroll/tilt coupling.

**`WaterField`** is one shared low-res simulation per screen, provided through `LocalWaterField`:

- **Height field**: a 96×192 grid (rescaled to the window aspect), the 2D wave equation
  (`h' = 2h - h_prev + c²·∇²h`, `c² = 0.45²`, damping 0.985), stepped once per frame. `tap(x, y,
  strength)` adds a localised impulse (press, and a half-strength impulse on release).
- **Velocity + dye**: a 64×128 "stable-fluids-lite" grid — semi-Lagrangian self-advection, one
  Jacobi pressure-projection pass, dissipation 0.995 (velocity) / 0.992 (dye). `splat(x, y, dx,
  dy, colour)` adds a velocity + dye impulse (hold, drag, scroll); dye colour cycles through the
  pastel palette per splat.
- **Tilt**: `SensorManager` gravity (falling back to the accelerometer) drives a slow global flow
  bias and the caustic light direction; registered only while the lifecycle is resumed.
- **Sampling**: `heightAt`, `gradientAt`, `flowAt` (all in root-relative pixels).

Measured grid sizes: height 96×~171 (portrait phone aspect), dye 64×128 — chosen to keep the
per-frame cost (wave step + one fluid step + one Jacobi pass, all flat array loops with no
per-cell allocation) within the ~2 ms/frame budget on a mid-range phone; the sim pauses whenever
`liquidMotion == 0`, under reduced motion, or while the app is not resumed (no frame callbacks
arrive).

**`LiquidBackground`** renders the field every frame: a small ARGB caustic bitmap is computed from
the height field's normals (`brightness = base + k·max(0, n·L)²`, `L` from the tilt vector) over
the pastel base, the dye field is blended on top, and the result is drawn scaled with bilinear
filtering (API 26+). On API ≥ 33 an AGSL `RuntimeShader` (`ShaderBrush`) takes the same bitmap as
a `shader` uniform for a refraction pass; if shader compilation fails on a given device the plain
bitmap draw is used instead (never crashes). Bubbles and droplets drift on `flowAt` and bob on
`heightAt`. Must fill its screen's root at (0, 0): touch/float coupling samples the field in
root-relative pixels, so the background and the sampled coordinate space have to line up.

## Touch (`TouchEffects.kt`)

`Modifier.waterTouch(enabled, onClick, onLongPress, drag)` (aliased as `glassTouch` — the name
screens call):

1. **press** → a `tap` impulse into the height field, a local glow bloom, three staggered ripple
   rings (900 ms).
2. **hold ≥ 350 ms** → a rainbow ink swirl drawn locally, and (with a shared `WaterField`) a
   dye `splat` repeated every ~90 ms while held.
3. **drag** (`drag = true`) → the element stretches toward the finger (scale up to 1.35× with a
   spring wobble back on release) and, with a field, splats ink along the drag path.
4. **release** → one last ripple ring / a half-strength `tap`.

Reduced motion (`GuiPrefs.reducedMotion` via `LocalGlass`): ripple only — no ink, no drag stretch,
no field coupling. The existing haptic plays on press (`performStoneHaptic(TAP)`,
`ui/theme/StoneHapticCue.kt`; not copied).

`Modifier.floatOnWater(strength = 1f)`: samples `heightAt`/`gradientAt` at the element's centre
every frame and applies a `graphicsLayer` translation (± `gradient × 6 dp × strength`), rotation
(± 2°) and scale (`1 + 0.02·height`) — a no-op with no `LocalWaterField` or under reduced motion.
Every `Glass*` component applies it by default.

`Modifier.waterScroll()`: a `NestedScrollConnection` that injects a `splat` along the scroll
direction, capped by distance, wherever a list is dragged.

A screen root shares one simulation with the background by wrapping its content:

```kotlin
val field = rememberWaterField(gui.liquidMotion, gui.reducedMotion)
CompositionLocalProvider(LocalWaterField provides field) {
    Box {
        LiquidBackground(Modifier.fillMaxSize(), motion = gui.liquidMotion, ...)
        // screen content — GlassButton / GlassSlider / … here share the same field
    }
}
```

Without a provided field, `glassTouch`'s ripple/glow/ink effects still draw locally (they do not
need the field), and `floatOnWater`/`waterScroll` are no-ops.

## Component catalogue (`GlassButton.kt`, `GlassSlider.kt`, …)

`GlassButton` (pill, text + optional icon), `GlassBubbleButton` (circle icon button, `size`
param), `GlassTile` (rounded square), `GlassPlayButton` (88 dp bubble) — `GlassButton.kt`.
`GlassSlider` — same signature shape as `CrystalSlider` (`ui/CrystalControls.kt:127`) so a screen
can swap the call 1:1. `GlassToggle` (`checked`/`onCheckedChange`). `GlassKnob` (rotary dial, drag
to turn). `GlassLinearProgress` / `GlassCircularProgress`. `GlassSheet` (`ModalBottomSheet` with
the glass container colour and a pearl drag handle). `GlassListRow` (leading art/icon, title,
subtitle, trailing). `GlassTopBar` (close ×, centred title, ⋮ menu, ref-05). `GlassTransportBar`
(library, previous, big play/pause, next, profile bubbles, ref-05). `GlassVerticalTabs` /
`GlassHorizontalTabs`. `GlassSegmented`. `GlassNavBar` (`GlassNavItem(label, icon)` — the same
shape as `CrystalNavItem` so `AppShell` can swap it 1:1). `GlassTextField` (pearl outlined field).
`GlassDialog` (glass card over `Dialog`). `GlassIcons` (outlined glyph set — every glyph the
references show already matches a stock `Icons.Outlined.*`, so this aliases those rather than
drawing new paths; see the KDoc on `GlassIcons` for the full mapping). `GlassGallery` — every
primitive rendered once, for a screen unit to check its work against; not wired to a route.

## `GuiPrefs` additions

`glassTint: Float = 0.5f`, `glassOpacity: Float = 0.34f`, `bubbleDensity: Float = 0.5f`,
`liquidMotion: Float = 1f`, persisted by `ThemeStore` next to the existing `gui_*` keys.
