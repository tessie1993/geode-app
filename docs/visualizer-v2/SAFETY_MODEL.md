# Safety model

What the app does about flashing and motion, why each limit exists, and what is still missing.
Required by the V2 master plan (not in this tree) §2.2; the policy is §11.

**Status:** the parameter-level clamp is production, tested, and **unconditional** — there is no
setting, no off switch and no tier. Behind it, the *measured* output limiter (§11.2's temporal
luminance and saturated-red flash counts) now runs on every frame the renderer shows or exports, so
MilkDrop presets, user GLSL and a scene's own brightness swings are held to three flashes a second
too. See §4.

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
GLSL) and a scene's **own internal brightness**. The output limiter closes all three by counting
flashes on the finished frame instead.

`Renderer::composite` draws the frame into `FlashLimitPass`'s own target; the pass measures it on a
16 x 9 tile grid, decides, and draws it on to the real target. It runs on the GPU with no readback,
and since the live view, the wallpaper, the dream and the export all share `Renderer::composite`,
they are limited alike. Only the W00 overlay (the user's own cover art and text) is drawn after it.

What is counted (WCAG 2.3.1, ITU-R BT.1702), per region, on what is **shown**:
- **Luminance:** a transition is a swing of relative luminance (sRGB-decoded, Rec.709) of at least
  0.1 between a maximum and a minimum whose darker side is below 0.8. A flash is an opposing pair.
- **Saturated red:** a pixel with R / (R + G + B) ≥ 0.8 (linear channels) has red value R − G − B,
  others 0; area-averaged, a change of more than 20 on WCAG's 0..320 scale (0.0625 here) is a
  transition. Counted separately from luminance.
- **Regions:** a 3 x 3 grid of overlapping windows, each half the frame wide and half high (8 x 5
  tiles, 28% of the frame), sliding by half. Overlap means any patch up to a quarter of the screen
  lies wholly in one window; averaging means a full-contrast change over about 3% of the screen
  already moves a region by 0.1, which is the order of WCAG's area threshold on a phone, while
  sparkles and moving shapes average out.

What it does: nothing, until a region would show a sixth transition (a fourth flash would begin)
within one second. Then that region is **held** at the luminance it showed last frame (zero change)
and, if it is red that is about to rise, its saturated red is guarded away (pixels pulled toward the
grey of their own luminance, so the guard never makes a luminance flash). When the oldest event
leaves the window, the held tiles ease back to the picture in a quarter of a second. Occasional
kicks, kicks up to 2 Hz (120 bpm), a single hard cut, fades and jitter are passed **bit for bit**;
from 2.5 Hz up, and every strobe, is capped at three flashes a second. A red rise is let through
only when there is also room for the fall that must follow it, since the limiter can remove red but
cannot add it back.

Why five detector events, not six: any run of k alternating transitions inside a window forces at
least k − 1 events inside it (the induction is in `core/viz/FlashLimiter.cpp`), so five events cap
the window at six transitions. `core/tests/flash_limit_test.cpp` checks the shown output with an
exhaustive transition count (the definition, not a detector) for 3–25 Hz strobes, red/grey flashes,
random tiled strobes and noise, portrait and landscape, at 24–120 fps.

What it does not cover: a flicker much smaller than a region (by design: it is below the area that
makes a flash), a pattern that inverts while its region's mean stays the same (a checkerboard
swapping), and the newer CIE-chromaticity form of the red-flash definition. While a region is held,
its tiles carry one transform each, so the hold can show as 16 x 9 blocks; this happens only while
the drawn picture is strobing.

Also absent: the alternating-stripe vector, safety telemetry in debug builds, and the thermal half
of §11.4.
