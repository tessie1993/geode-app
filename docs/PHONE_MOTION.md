# Phone motion: gravity, shake, twist and parallax

Tilting, shaking and twisting the phone move the visuals. The effects are built in: things fall,
scatter and spin, and the camera leans. The same motions are also sources in the reaction matrix,
so they can drive anything. Each style's DNA picks a couple of these routes of its own.

## Pieces

| Piece | Where | What it does |
|---|---|---|
| Sensor reader | `engine/scenes/.../render/PhoneMotionSensor.kt` (replaces `GyroLook`) | Reads the sensors, remaps to screen axes, hands raw samples to native. No filtering. |
| Motion engine | `core/viz/PhoneMotion.{hpp,cpp}` | Low-passes gravity, detects shakes, integrates the twist into a world spin, springs the parallax lean. Stepped once per frame on the GL thread. |
| Built-in effects | `ParticleLayer`, `Layers`, `CameraRig`, the fluid sim | Particles fall toward the low side and scatter on a shake. Spawned Layers shapes drift downhill. Fluid dye sinks. The camera leans with a lag and rolls with the spin. |
| Shader access | `lib_camera`: `uniform vec4 uMotion` | Gravity in screen space (xy), shake 0..1 (z), spin angle in radians (w). |
| Reaction sources | `ReactSource` `TiltX`, `TiltY`, `Shake`, `Twist` (group MOTION) | Route them like Bass or Kick. |
| Style personality | `core/viz/ReactionDna` motion layer | One or two seeded motion routes per style. |
| Control | Customize › Camera › Phone motion (`SceneParams.motion`, 0..1) | 0 turns the built-in physics off. |

Surfaces: the live view, the live wallpaper and the dream each build a `VisualizerView`, so each one
runs the sensors only while it is visible. An export has no sensor, so it renders with no motion:
no gravity, shake or spin, and the Motion sources read 0.

## Android sensor rules followed

All sources are from the Android Knowledge Base, fetched 2026-10-31 with `android docs`.

- **Sensor choice.** The sensor is `TYPE_GAME_ROTATION_VECTOR`. It is the rotation vector without
  the magnetometer, so relative rotations are more accurate and magnetic fields don't affect it.
  Where it is missing, the code falls back to `TYPE_ACCELEROMETER` and low-passes it into gravity.
  Source: *Position sensors › Use the game rotation vector sensor*,
  <https://developer.android.com/develop/sensors-and-location/sensors/sensors_position>.
- **Check before use.** A device need not have any given sensor, so `getDefaultSensor` can return
  null and both are null-checked. Source: *Sensors overview › Verify sensors before you use them*,
  <https://developer.android.com/develop/sensors-and-location/sensors/sensors_overview>.
- **Foreground only, unregister when hidden.** A paused listener keeps acquiring data and draining
  the battery. Since API 28 a backgrounded app gets no continuous sensor events at all. The sensors
  are registered only while the surface is visible and unregistered the moment it is hidden. Source:
  *Sensors overview › Best practices*.
- **Delay.** Use the largest delay that works. `SENSOR_DELAY_GAME` (20 ms) is the slowest rate that
  still catches a shake. Register through the plain API, which caps at 200 Hz, so no
  `HIGH_SAMPLING_RATE_SENSORS` permission is needed. Source: *Sensors overview › Monitoring sensor
  events*, *Sensor rate-limiting*.
- **No wake-ups.** Use `getDefaultSensor(type, false)`. Continuous sensors are non-wake-up by
  default, and saying so makes it explicit.
- **Keep onSensorChanged light.** The callback only does the remap and writes the atomics. The
  filtering runs on the GL thread in native code. Source: *Sensors overview › Don't block
  onSensorChanged()*.
- **Screen rotation.** The sensor axes follow the device's natural orientation, which can be
  landscape on tablets, and never swap. Map them to the screen with `Display.getRotation()` and the
  `remapCoordinateSystem` axes, even for a portrait-locked app. Source: *Sensors overview › Sensor
  coordinate system*.
- **Gravity and linear acceleration.** Low-pass the accelerometer for gravity and subtract it for
  linear acceleration; the docs' α = t / (t + dT). Source: *Motion sensors › Use the accelerometer*,
  <https://developer.android.com/develop/sensors-and-location/sensors/sensors_motion>.
- **Testing.** The emulator's extended controls (Virtual sensors) can fake rotation and movement.

## Filter numbers

Constants are in `PhoneMotion.hpp`.

| Constant | Value | Why |
|---|---|---|
| Gravity low-pass | 0.12 s | Removes hand tremor and keeps a tilt feeling immediate. |
| Flat dead zone | 0.08 g in-plane | Phone flat on a table: nothing slides. p5.js `setMoveThreshold` uses the same idea. |
| Shake floor / full | 4 / 14 m/s² of linear acceleration | 4 m/s² is a common linear-acceleration shake gate. Square's Seismic triggers at 13 m/s² (11 light, 15 hard) on raw acceleration. |
| Shake envelope | instant rise, 0.6 s fall | TouchDesigner's Lag CHOP pattern: fast rise, slow fall. |
| Shake hit | 0.5, re-arm below 0.25 | A Trigger-CHOP-style threshold with hysteresis, so one shake is one hit. |
| Parallax spring | 0.35 s, damping 0.6 | "Inertia" parallax: overshoots and settles. A spring, not a lerp. |
| Twist dead zone | 2.5 rad/s | Turning the phone in the hand is not a flick. |
| Spin friction | 2.5 s | The world spins down like a flicked wheel. |

## Ideas borrowed

- **TouchDesigner.** Particle SOP's external force for gravity and turbulence driven by shake energy;
  Lag, Trigger and Count CHOPs for envelopes, hits and counted states; Speed CHOP to integrate a rate
  into an angle (our Spin mode). <https://derivative.ca/UserGuide/Particle_SOP>,
  <https://docs.derivative.ca/Lag_CHOP>, <https://docs.derivative.ca/Trigger_CHOP>.
- **MilkDrop / MilkDrop 3.** Tilt as `dx`/`dy` (a feedback image drifting downhill) and `cx`/`cy`
  (the zoom centre sliding toward the low side); a yaw rate as `rot`; a shake as a `warp` burst.
  These are what the per-style motion routes reach for. MilkDrop 3 feeds its shaders the mouse each
  frame, which is the same role a tilt plays here.
  <https://forge.light.kow.is/dkowis/winamp/src/branch/community/Src/resources/data/milkdrop_preset_authoring.html>.
- **Snow globes and fluid toys** (Snowglobe, RealSnowGlobe, Bloom). Flakes drift to the low side and
  a shake throws them up; a fluid that takes tilt, shake and twist at once; a shake that wipes the
  scene clean (our Shatter and Freeze targets).
- **Parallax wallpapers** (parallax.js, FloatDeck). Lean = tilt × depth, calibrated to the held pose,
  with spring inertia. <https://github.com/wagerfield/parallax>.
- **ZIG SIM / TDInput.** Motion exposed as plain channels next to everything else, which is why the
  four motions are reaction sources and not only built-in effects.
