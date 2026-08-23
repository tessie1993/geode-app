# Safety model

What the app does about flashing and motion, why each limit exists, and what is still missing.
Required by [`MASTER_PLAN.md`](MASTER_PLAN.md) §2.2; the policy is §11.

**Status:** the parameter-level clamp is production, tested, and **unconditional** — there is no
setting, no off switch and no tier. The *measured* global limiter of §11.2 is still not built, so
three render paths remain outside it. See "What this does not cover".

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

Inversion and solarize are deliberately **not** forced off. A statically inverted picture is not a
flash; the hazard is toggling it quickly, which the rate limits above already bound. Forcing them
off would only produce a dead control.

## 4. What this does not cover

**No frame is measured.** Every limit above is computed from parameters — what the app is about to
ask the shader to do. §11.2's limiter is defined over *measured* temporal luminance and
saturated-red change, which needs a downsampled target and an asynchronous readback.

Three paths therefore still reach the screen outside all of it, because none passes through
`uPostFlash` or `SceneParams`:

- **projectM** renders MilkDrop presets itself and can strobe internally;
- **Shader Studio** runs user GLSL;
- a scene's **own internal brightness** swings.

Making the clamp unconditional closed the *opt-out*; it did not close these. Until a measured
final-frame limiter exists, an imported `.milk` preset can still flash outside the limit, so the
product promise that "no imported file can exceed the flash limit" is **not yet true**. That work
is **V2-0-02c**.

Also absent: the saturated-red analysis, the alternating-stripe and red-transition vectors (both
need frame content), safety telemetry in debug builds, and the thermal half of §11.4.
