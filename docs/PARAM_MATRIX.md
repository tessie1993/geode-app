# Param × Scene-Family Matrix

**Maintained by hand.** There is no test that generates or enforces this
file; when a parameter is added to the code, this table needs a matching row
added by hand in the same change.

What every Customize parameter is wired to: a control, a roll (or a declared
reason not to roll it), a preset key, and a reader.

A **·** in a family column means that scene class references the parameter
directly (or through the property that resolves it, e.g. `palette` →
`paletteBase`). A blank is not "does nothing": most of Shape, Color and FX
reach every style through the composite pass, which is its own column.

## Families

| Family | Source |
|---|---|
| **Shader** | `core/viz/scenes/ShaderScene.cpp` |
| **Silk** | `core/viz/scenes/SilkScene.cpp`, `core/viz/compute/SimPass*` |
| **Life** | `core/viz/scenes/LifeScene.cpp` |
| **Mycelium** | `core/viz/scenes/MycoScene.cpp` |
| **Acid** | `core/viz/scenes/AcidScene.cpp` |
| **MilkDrop** | `core/viz/scenes/MilkdropScene.cpp` |
| **Fluid** | `core/viz/scenes/FluidScene.cpp`, `core/viz/scenes/FluidSceneBase.cpp`, `core/viz/fluid/*` |
| **Curl Flow** | `core/viz/scenes/CurlFlowScene.cpp`, `core/viz/fluid/*` |
| **Water** | `core/viz/scenes/WaterScene.cpp`, `core/viz/fluid/*` |
| **Cymatics** | `core/viz/scenes/CymaticsScene.cpp`, `core/viz/scenes/CymaticsMath.cpp` |
| **Composite** | `core/viz/CompositePass.cpp` |
| **Export** | `VideoExporter.kt` |

## Parameters

| Parameter | Tab | Rolled by | Preset key | Shader | Silk | Life | Mycelium | Acid | MilkDrop | Fluid | Curl Flow | Water | Cymatics | Composite | Export |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `speed` | Motion | `Speed` | `speed` | · | · | · | · | · |   | · | · | · | · | · | · |
| `zoom` | Motion | `Zoom` | `zoom` | · |   |   |   | · | · |   |   |   |   | · | · |
| `rotation` | Motion | `Rotation` | `rotation` | · |   |   |   |   | · |   |   |   |   | · | · |
| `endlessZoom` | Motion | `Endless zoom` | `endlessZoom` | · |   |   |   |   | · |   |   |   |   |   |   |
| `endlessZoomSpeed` | Motion | `Dive speed` | `endlessZoomSpeed` | · |   |   |   |   | · |   |   |   |   |   |   |
| `sway` | Motion | `Sway` | `sway` | · |   |   |   |   |   |   |   |   |   | · | · |
| `pulse` | Motion | `Beat pulse` | `pulse` | · |   |   |   |   |   |   | · |   |   | · | · |
| `driftX` | Motion | `Drift X` | `driftX` | · |   |   |   |   |   |   |   |   |   | · | · |
| `driftY` | Motion | `Drift Y` | `driftY` | · |   |   |   |   |   |   |   |   |   | · | · |
| `shake` | Motion | `Beat shake` | `shake` | · |   |   |   |   |   |   |   |   |   | · | · |
| `audioDrive` | Reactivity | `Audio drive` | `audioDrive` | · | · | · | · | · |   | · | · | · | · |   |   |
| `formDrive` | — | — | `formDrive` | · | · | · | · | · | · | · | · | · | · | · | · |
| `beatResponse` | — | — | `beatResponse` | · | · | · | · | · | · | · | · | · | · |   |   |
| `turbulence` | Motion | `Turbulence` | `turbulence` | · |   |   | · | · |   |   | · |   |   |   |   |
| `density` | Shape | `Density` | `density` |   |   |   |   |   |   | · |   |   |   |   |   |
| `marchDetail` | Shape | `Detail` | `marchDetail` | · |   |   |   |   |   |   |   |   |   |   |   |
| `trails` | FX | `Trails` | `trails` |   | · |   |   |   |   |   |   |   |   | · | · |
| `trailLength` | FX | `Trail length` | `trailLength` |   | · |   |   |   |   |   |   |   |   | · | · |
| `trailZoom` | FX | `Trail zoom (echo in/out)` | `trailZoom` |   |   |   |   |   |   |   |   |   |   | · | · |
| `trailWarp` | FX | `Trail warp (liquid echo)` | `trailWarp` |   |   |   |   |   |   |   |   |   |   | · | · |
| `mirror` | Shape | `Mirror` | `mirror` | · |   |   |   |   | · |   |   |   |   | · | · |
| `warp` | Shape | `Domain warp` | `warp` | · |   |   |   |   |   |   |   |   |   | · | · |
| `ripple` | Shape | `Ripple` | `ripple` | · |   |   |   |   |   |   |   |   |   | · | · |
| `symmetry` | Shape | `Kaleidoscope` | `symmetry` | · |   |   |   |   |   |   |   |   |   | · | · |
| `kaleidoscope` | Shape | `Kaleidoscope` | `kaleidoscope` | · |   |   |   |   |   |   |   |   |   | · | · |
| `morph` | Shape | `Morph` | `morph` | · |   |   |   |   |   |   |   |   |   | · |   |
| `pixelate` | Shape | `Pixelate` | `pixelate` | · |   |   |   |   |   |   |   |   |   | · | · |
| `posterize` | Shape | `Posterize` | `posterize` | · |   |   |   |   |   |   |   |   |   | · | · |
| `particleShape` | Shape | `Particle shape` | `particleShape` |   |   |   |   |   |   | · | · |   |   |   |   |
| `particleSize` | Shape | `Particle size` | `particleSize` |   |   |   |   |   |   | · | · |   |   |   |   |
| `tile` | Shape | `Tile` | `tile` | · |   |   |   |   |   |   |   |   |   | · | · |
| `twist` | Shape | `Twist` | `twist` | · |   |   |   |   |   |   |   |   |   | · | · |
| `palette` | Color | `Palette` | `palette` | · | · | · | · | · | · | · | · | · | · | · |   |
| `palette2` | Color | `Palette 2` | `palette2` | · |   |   |   |   |   |   |   |   |   |   |   |
| `paletteMix` | Color | `Palette blend` | `paletteMix` | · |   |   |   |   |   |   |   |   |   |   |   |
| `paletteBaseOverride` | Color | — | `paletteBaseOverride` | · | · | · | · | · | · | · | · | · | · | · |   |
| `paletteRangeOverride` | Color | — | `paletteRangeOverride` | · | · | · | · | · | · | · | · | · | · | · |   |
| `palette2BaseOverride` | Color | — | `palette2BaseOverride` | · |   |   |   |   |   |   |   |   |   | · |   |
| `palette2RangeOverride` | Color | — | `palette2RangeOverride` | · |   |   |   |   |   |   |   |   |   | · |   |
| `customPaletteId` | Color | — | `customPaletteId` |   |   |   |   |   |   |   |   |   |   |   |   |
| `paletteLut` | Color | `Colour map` | `paletteLut` | · |   |   |   |   |   |   |   |   |   |   |   |
| `customPalette2Id` | Color | — | `customPalette2Id` |   |   |   |   |   |   |   |   |   |   |   |   |
| `milkdropPaletteTint` | Color | `MilkDrop palette tint` | `milkdropPaletteTint` |   |   |   |   |   | · |   |   |   |   | · |   |
| `milkdropBlendPresets` | Scene | `Blend preset changes` | `milkdropBlendPresets` |   |   |   |   |   | · |   |   |   |   |   |   |
| `colorShift` | Color | `Hue shift` | `colorShift` | · |   |   |   |   | · |   |   |   |   | · | · |
| `hueRange` | Color | `Hue range` | `hueRange` | · | · | · | · | · | · | · | · | · | · | · |   |
| `saturation` | Color | `Saturation` | `saturation` | · |   |   |   | · | · |   |   |   |   | · | · |
| `brightness` | Color | `Brightness` | `brightness` | · |   |   |   |   | · | · | · |   | · | · | · |
| `contrast` | Color | `Contrast` | `contrast` | · |   |   |   |   | · |   |   |   |   | · | · |
| `gamma` | Color | `Gamma` | `gamma` | · |   |   |   |   | · |   |   |   |   | · | · |
| `colorCycle` | Color | `Color cycle` | `colorCycle` | · |   |   |   |   | · |   |   |   |   | · | · |
| `cycleSpeed` | Color | `Cycle speed` | `cycleSpeed` | · |   |   |   |   | · |   |   |   |   | · | · |
| `invert` | Color | `Invert` | `invert` | · |   |   |   |   | · |   |   |   |   | · | · |
| `intensity` | Color | `Intensity` | `intensity` | · |   |   |   |   | · |   |   |   |   | · | · |
| `duotone` | Color | `Duotone` | `duotone` | · |   |   |   |   |   |   |   |   |   |   |   |
| `bloom` | Color | `Bloom` | `bloom` | · |   |   |   |   |   | · | · |   |   | · | · |
| `temperature` | Color | `Temperature` | `temperature` | · |   |   |   |   |   |   |   |   |   | · | · |
| `solarize` | Color | `Solarize` | `solarize` | · |   |   |   |   |   |   |   |   |   | · | · |
| `bassGain` | Reactivity | `Bass gain` | `bassGain` |   |   |   |   |   |   |   |   |   |   | · | · |
| `midGain` | Reactivity | `Mid gain` | `midGain` |   |   |   |   |   |   |   |   |   |   | · | · |
| `trebGain` | Reactivity | `Treble gain` | `trebGain` |   |   |   |   |   |   |   |   |   |   | · | · |
| `flash` | — | — | `flash` | · |   |   |   |   |   |   |   |   |   | · | · |
| `chromaAb` | FX | `Chromatic aberration` | `chromaAb` |   |   |   |   |   |   |   |   |   |   | · | · |
| `vignette` | FX | `Vignette` | `vignette` |   |   |   |   |   |   |   |   |   |   | · | · |
| `scanlines` | FX | `Scanlines` | `scanlines` |   |   |   |   |   |   |   |   |   |   | · | · |
| `grain` | FX | `Film grain` | `grain` |   |   |   |   |   |   |   |   |   |   | · | · |
| `fisheye` | FX | `Fisheye` | `fisheye` |   |   |   |   |   |   |   |   |   |   | · | · |
| `strobe` | FX | `Strobe` | `strobe` |   |   |   |   |   |   |   |   |   |   | · | · |
| `paramFadeSec` | FX | — | `paramFadeSec` |   |   |   |   |   |   |   |   |   |   | · |   |
| `fluidQuality` | Fluid | — | `fluidQuality` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidAutoQuality` | Fluid | — | `fluidAutoQuality` |   |   |   |   |   |   | · |   | · |   |   | · |
| `fluidIterations` | Fluid | `Solver iterations` | `fluidIterations` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidPressure` | Fluid | `Pressure` | `fluidPressure` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidCurl` | Fluid | `Fluid curl` | `fluidCurl` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidVelocityDissipation` | Fluid | `Motion fade` | `fluidVelocityDissipation` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidDensityDissipation` | Fluid | `Fluid fade` | `fluidDensityDissipation` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidChromaticAging` | Fluid | `Chromatic aging` | `fluidChromaticAging` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidSplatRadius` | Fluid | `Fluid splat radius` | `fluidSplatRadius` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidSplatForce` | Fluid | `Fluid splat force` | `fluidSplatForce` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidBeatPattern` | Fluid | `Beat pattern` | `fluidBeatPattern` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidBeatSplats` | Fluid | `Beat splats` | `fluidBeatSplats` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidStirrers` | Fluid | `Stirrers` | `fluidStirrers` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidStirrerSpeed` | Fluid | `Stirrer speed` | `fluidStirrerSpeed` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidBassPump` | Fluid | `Bass pump` | `fluidBassPump` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidPaletteCycleSpeed` | Fluid | `Palette cycle` | `fluidPaletteCycleSpeed` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidSparkle` | Fluid | `Treble sparkle` | `fluidSparkle` |   |   |   |   |   |   | · |   | · |   |   |   |
| `fluidSpawnPath` | Fluid | `Path` | `fluidSpawnPath` |   |   |   |   |   |   | · | · | · |   |   |   |
| `fluidSpawnPoints` | Fluid | `Spawn points` | `fluidSpawnPoints` |   |   |   |   |   |   | · | · | · |   |   |   |
| `fluidSpawnProgress` | Fluid | — | `fluidSpawnProgress` |   |   |   |   |   |   | · | · | · |   |   |   |
| `fluidCatchPoints` | Fluid | `Catch points` | `fluidCatchPoints` |   |   |   |   |   |   | · | · | · |   |   |   |
| `fluidCatchPull` | Fluid | `Catch pull` | `fluidCatchPull` |   |   |   |   |   |   | · | · | · |   |   |   |
| `fluidCatchRadius` | Fluid | `Catch radius` | `fluidCatchRadius` |   |   |   |   |   |   | · | · | · |   |   |   |
| `fluidParticlesEnabled` | Fluid | — | `fluidParticlesEnabled` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidParticleLife` | Fluid | `Particle life (s)` | `fluidParticleLife` |   |   |   |   |   |   | · | · |   |   |   |   |
| `fluidParticleDrag` | Fluid | `Particle drag` | `fluidParticleDrag` |   |   |   |   |   |   | · | · |   |   |   |   |
| `fluidParticleBrightness` | Fluid | `Particle brightness` | `fluidParticleBrightness` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidDyeEnabled` | Fluid | — | `fluidDyeEnabled` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidShading` | Fluid | `Shading (embossed ink)` | `fluidShading` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidBloom` | Fluid | `Glow (fluid)` | `fluidBloom` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidBloomIntensity` | Fluid | `Fluid glow` | `fluidBloomIntensity` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidBloomThreshold` | Fluid | `Glow threshold` | `fluidBloomThreshold` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidSunrays` | Fluid | `Sunrays` | `fluidSunrays` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidSunraysWeight` | Fluid | `Sunrays weight` | `fluidSunraysWeight` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidCurlAudio` | Fluid | `Curl from mids` | `fluidCurlAudio` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidBloomAudio` | Fluid | `Glow from loudness` | `fluidBloomAudio` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidFadeAudio` | Fluid | `Fade when quiet` | `fluidFadeAudio` |   |   |   |   |   |   | · |   |   |   |   |   |
| `fluidRadiusPulse` | Fluid | `Radius on beat` | `fluidRadiusPulse` |   |   |   |   |   |   | · |   | · |   |   |   |
| `flowEnabled` | Fluid | — | `flowEnabled` |   |   |   |   |   |   |   |   |   |   | · | · |
| `flowStrength` | Fluid | `Flow strength` | `flowStrength` |   |   |   |   |   |   |   |   |   |   | · | · |
| `flowForce` | Fluid | `Flow force` | `flowForce` |   |   |   |   |   |   |   |   |   |   | · |   |
| `flowCurl` | Fluid | `Flow curl` | `flowCurl` |   |   |   |   |   |   |   |   |   |   | · |   |
| `waterWaveSpeed` | Fluid | `Wave speed` | `waterWaveSpeed` |   |   |   |   |   |   |   |   | · |   | · | · |
| `waterDamping` | Fluid | `Damping` | `waterDamping` |   |   |   |   |   |   |   |   | · |   | · | · |
| `waterRippleStrength` | Fluid | `Ripple strength` | `waterRippleStrength` |   |   |   |   |   |   |   |   | · |   |   |   |
| `waterDepth` | Fluid | `Depth` | `waterDepth` |   |   |   |   |   |   |   |   | · |   |   |   |
| `waterSpecular` | Fluid | `Specular` | `waterSpecular` |   |   |   |   |   |   |   |   | · |   |   |   |
| `waterFlow` | Fluid | `Flow drift` | `waterFlow` |   |   |   |   |   |   |   |   | · |   |   |   |
| `waterLiquid` | Fluid | `Liquid` | `waterLiquid` |   |   |   |   |   |   |   |   | · |   |   |   |
| `waterLiquidFlow` | Fluid | `Liquid flow` | `waterLiquidFlow` |   |   |   |   |   |   |   |   | · |   |   |   |
| `waterLiquidFade` | Fluid | `Liquid fade` | `waterLiquidFade` |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsGeometry` | Cymatics | `Geometry` | `cymaticsGeometry` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsFundamental` | Cymatics | `Fundamental (Hz)` | `cymaticsFundamental` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsModes` | Cymatics | `Standing waves` | `cymaticsModes` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsRing` | Cymatics | `Plate ring` | `cymaticsRing` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsFocus` | Cymatics | `Tonal focus` | `cymaticsFocus` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsScale` | Cymatics | `Field scale` | `cymaticsScale` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsFill` | Cymatics | `Fill` | `cymaticsFill` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsLine` | Cymatics | `Nodal lines` | `cymaticsLine` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsGlow` | Cymatics | `Nodal glow` | `cymaticsGlow` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsIridescence` | Cymatics | `Iridescence` | `cymaticsIridescence` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsCaustic` | Cymatics | `Caustic sheen` | `cymaticsCaustic` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsFlow` | Cymatics | `Wave flow` | `cymaticsFlow` |   |   |   |   |   |   |   |   |   | · |   |   |
| `cymaticsSwirl` | Cymatics | `Field swirl` | `cymaticsSwirl` |   |   |   |   |   |   |   |   |   | · |   |   |
| `rippleOverlayEnabled` | Fluid | — | `rippleOverlayEnabled` |   |   |   |   |   |   |   |   |   |   | · | · |
| `rippleOverlayStrength` | Fluid | `Ripple overlay strength` | `rippleOverlayStrength` |   |   |   |   |   |   |   |   |   |   | · | · |
| `rippleOverlaySpecular` | Fluid | `Ripple glint` | `rippleOverlaySpecular` |   |   |   |   |   |   |   |   |   |   | · | · |
| `motionAmount` | Reactivity | `Motion amount` | `motionAmount` | · |   |   |   |   |   |   |   |   |   | · | · |
| `motionBreath` | Reactivity | `Breath` | `motionBreath` | · |   |   |   |   |   |   |   |   |   | · | · |
| `motionDrift` | Reactivity | `Spin drift` | `motionDrift` | · |   |   |   |   |   |   |   |   |   | · | · |
| `motionHue` | Reactivity | `Key colour` | `motionHue` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camFly` | Motion | `Fly speed` | `camFly` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camPath` | Motion | `Camera path` | `camPath` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camOrbit` | Motion | `Swing` | `camOrbit` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camFov` | Motion | `Field of view` | `camFov` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camShake` | Motion | `Handheld shake` | `camShake` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camPush` | Motion | — | `camPush` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camYaw` | Motion | — | `camYaw` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camPitch` | Motion | — | `camPitch` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camRoll` | Motion | — | `camRoll` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camSmooth` | Motion | — | `camSmooth` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camTouch` | Motion | — | `camTouch` | · |   |   |   |   |   |   |   |   |   | · | · |
| `camGyro` | Motion | — | `camGyro` | · |   |   |   |   |   |   |   |   |   | · | · |
| `reactAmount` | Reactivity | — | `reactAmount` | · |   |   |   |   |   |   |   |   |   | · | · |
| `reactSeed` | Reactivity | `Reaction DNA` | `reactSeed` | · |   |   |   |   |   |   |   |   |   | · | · |

## Never randomized

Declared in `randomizer::neverRolled()` (`core/viz/ParamRandomizer.cpp`). `core/tests/params_test.cpp`
checks that every field is rolled by exactly one control or is on this list, and that
these fields never change on a roll.

The camera and motion rows above are read by `core/viz/CameraRig.cpp` and
`core/viz/MotionField.cpp` for every family: 3D styles read the rig through
`lib_camera`, every other scene is moved by the composite's flat projection.
`pulse`, `shake`, `beatResponse`, `flash`, `strobe`, `formDrive` and
`motionOrbit` have no reader and no control any more; their rows are history.

| Parameter | Why |
|---|---|
| `paramFadeSec` | an automation preference, not a look |
| `fluidQuality` | a performance setting, not a look |
| `fluidAutoQuality` | a performance setting, not a look |
| `fluidSpawnProgress` | how much the song itself drives the look |
| `fluidParticlesEnabled` | the fluid particle layer's master switch |
| `fluidDyeEnabled` | the fluid ink layer's master switch |
| `flowEnabled` | the FlowField's master switch |
| `rippleOverlayEnabled` | the water-ripple overlay's master switch |
| `marchDetail` | a performance budget, not a look |
| `camPush` | a modulation offset the reaction routes drive |
| `camYaw` | a modulation offset the reaction routes drive |
| `camPitch` | a modulation offset the reaction routes drive |
| `camRoll` | a modulation offset the reaction routes drive |
| `camSmooth` | a comfort preference, not a look |
| `camTouch` | an input preference, not a look |
| `camGyro` | an input preference, not a look |
| `reactAmount` | how much the user wants the music to move things |
| `pulse` | inert since wave three |
| `shake` | inert since wave three |
| `beatResponse` | inert since wave three |
| `flash` | inert since wave three |
| `strobe` | inert since wave three |
| `formDrive` | inert since wave three |
| `motionOrbit` | no native reader |

## Rendered by nothing

Declared in `SceneParams.NOT_RENDERED`; every other parameter has to be read
by a scene, the composite pass or the export compositor.

| Parameter | What it is for |
|---|---|
| `customPaletteId` | which saved palette slot 1 uses; rendering reads the resolved hues |
| `customPalette2Id` | which saved palette slot 2 uses; rendering reads the resolved hues |

## Tabs

Tabs are `render.scene.CustomizeTab`, in the order the panel shows them. "Randomize <tab>"
rolls exactly the fields of that tab's roll keys (`randomizer::rolledKeys()` in
`core/viz/ParamRandomizer.cpp`, which is also where the counts below come from).

| Tab | Roll keys | Fields rolled |
|---|---|---|
| Macros | 0 | 0 (six knobs outside the params) |
| Events | 0 | 0 (event recipes, no params of its own) |
| Shape | 11 | 12 |
| Colour | 20 | 24 |
| Motion | 14 | 14 |
| FX | 10 | 10 |
| Reactivity | 9 | 9 |
| Scene | 1 | 1 |
| Fluid | 47 | 47 |
| Cymatics | 13 | 13 |

The other 24 fields are never rolled (above).
