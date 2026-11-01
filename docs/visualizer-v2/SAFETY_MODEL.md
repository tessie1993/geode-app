# Safety model

What the app does about flashing and motion, why each limit exists, and what is still missing.
Required by the V2 master plan (not in this tree) §2.2; the policy is §11.

**Status:** the parameter-level clamp is production, tested, and **unconditional** — there is no
setting, no off switch and no tier. Behind it, the *measured* output limiter (§11.2's temporal
luminance half) now runs on every frame the renderer shows or exports, so MilkDrop presets, user
GLSL and a scene's own brightness swings are held to three flashes a second too. See §4.

---

## 1. What the app is protecting against

Two different harms, with different controls, deliberately not merged:

| Harm | Control | Governed by |
|---|---|---|
| Photosensitive seizure | flash rate and full-frame luminance depth | WCAG 2.3.1, three flashes per second |
| Vestibular discomfort | speed, drift, shake, rotation, endless zoom | reduced motion |

The flash clamp is not "turn the music response off". Colour, texture, density and slow
structural movement keep reacting; what is bounded is the impulsive full-frame event.

## 2. There is no user choice

The flash clamp is not configurable. `VisualSafety` holds the numbers as constants —
`WCAG_FLASHES_PER_SECOND = 3`, `MAX_FLASH_DEPTH = 0.25` — and every render path applies them the
same way. There is no `SafetyConfig`, no `enabled` flag and no way to construct an unclamped
render. A preview that is safe therefore cannot produce an unsafe file: same clamp, same code,
same numbers, on screen and in export alike.

The first-run notice reflects that. It takes an acknowledgement, not a decision: it says the
visuals are intense and that the rate is limited in the engine by design.

**Reduced motion stays a choice.** It is a vestibular comfort preference, not a photosensitivity
guard, so it remains a switch in Settings while the flash clamp does not.

Migration: anyone who answered the old three-way question has already seen the notice and is not
asked again, and a stored `REDUCED_MOTION` answer carries over to the reduced-motion switch.

## 3. Where the limits are applied

`VisualSafety.apply` runs **last**, after `LfoEngine.apply` and `AdsrEngine.apply`, on the params
a scene is about to receive. Order is the whole design: a safe stored value is worth nothing if a
modulator can push it back into the hazardous range.

| Lever | What it bounds |
|---|---|
| `apply` | strobe, flash, glitch, bloom, brightness, intensity, contrast |
| `strobeHz` | the strobe's oscillator rate — dimming a 9 Hz flicker leaves a 9 Hz flicker |
| `limitLfoRate` | an LFO aimed at a luminance target; the rate is the hazard, not the amplitude |
| `beatMinIntervalMs` | the floor between beat-driven hits |
| `layerMix` | ADD and DIFFERENCE blends, which reach the screen after `apply` |
| `transitionStyle` / `transitionId` | a hard cut becomes a crossfade |
| `FlashBudget` | how *often* the flash may fire — three rising edges per rolling second |
| `FlashLimitPass` | what actually reaches the screen or the file — see §4 |

Inversion and solarize are deliberately **not** forced off. A statically inverted picture is not a
flash; the hazard is toggling it quickly, which the rate limits above already bound. Forcing them
off would only produce a dead control.

## 4. The measured output limiter

Every limit in §3 is computed from parameters. Three paths reach the screen without passing through
them: **projectM** (MilkDrop presets render and can strobe internally), **Shader Studio** (user
GLSL) and a scene's **own internal brightness**. The output limiter closes all three by measuring
the finished frame instead.

`Renderer::composite` draws the frame into `FlashLimitPass`'s own target; the pass measures the
mean relative luminance (sRGB-decoded, Rec.709 weights) of a 16 x 9 grid of tiles, steps a small
per-tile state, and draws the frame on to the real target with each tile's luminance limited. It
runs on the GPU with no readback, and since the live view, the wallpaper, the dream and the export
all share `Renderer::composite`, they are limited alike. Only the W00 overlay (the user's own cover
art and text) is drawn after it.

The per-tile rule (`core/viz/FlashLimiter`, mirrored in `shaders/lib_flash_limit.glsl`): the shown
luminance drags an anchor inside a band of ±0.01, and only the anchor's travel is paid for, out of a
token bucket (0.36, refilled at 0.13/s, plus 0.06/s free). A WCAG transition (an opposing swing of
≥ 0.1) costs the anchor ≥ 0.08, so seven in one second would need 0.56 while the bucket allows 0.55:
at most six transitions, three flashes, in any second. `core/tests/flash_limit_test.cpp` checks it
exhaustively against square waves, sines and noise at 24–120 fps; slow fades, a single beat flash
and sub-band jitter pass unchanged. The arithmetic is in `VisualSafety.hpp` beside the constants.
What a strobe becomes: a 10 Hz black/white strobe is held near a steady gray; a hard cut from black
to white shows its first 0.37 at once and reaches white over about three seconds.

What it does not cover: a flicker confined well inside one tile (a WCAG general flash must cover a
quarter of a 10° field, several tiles on a phone), a pattern that inverts while its tile's mean stays
the same (a checkerboard swapping).

Also absent: the saturated-red analysis, the alternating-stripe and red-transition vectors (both
need frame content), safety telemetry in debug builds, and the thermal half of §11.4.
