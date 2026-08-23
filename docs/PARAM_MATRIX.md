# Param × Scene-Family Matrix

**Generated — do not edit.** `CustomizeSurfaceTest` rewrites this file from
the sources whenever they drift, and fails the build until the new version is
committed. To regenerate deliberately: `./gradlew :app:testDebugUnitTest`.

What every Customize parameter is wired to. The same test enforces the four
columns: a parameter with no control, no roll (and no declared reason), no
preset key or no reader fails the build.

A **·** in a family column means that scene class references the parameter
directly (or through the property that resolves it, e.g. `palette` →
`paletteBase`). A blank is not "does nothing": most of Shape, Color and FX
reach every style through the composite pass, which is its own column.

## Families

| Family | Classes |
|---|---|
| **Shader** | `ShaderScene` |
| **Silk** | `SilkScene` |
| **Life** | `LifeScene` |
| **Mycelium** | `MycoScene` |
| **Acid** | `AcidScene` |
| **MilkDrop** | `MilkdropScene` |
| **Fluid** | `FluidScene` |
| **Curl Flow** | `CurlFlowScene` |
| **Water** | `WaterScene` |
| **Cymatics** | `CymaticsScene` |
| **Beam** | `BeamScene` |
| **Composite** | `VisualizerRenderer`, `CompositeGrade` |
| **Export** | `FxCompositor`, `VideoExporter` |

## Parameters

| Parameter | Tab | Rolled by | Preset key | Shader | Silk | Life | Mycelium | Acid | MilkDrop | Fluid | Curl Flow | Water | Cymatics | Beam | Composite | Export |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `speed` | Motion | `Speed` | `speed` | · | · | · | · | · |   | · | · | · | · |   | · | · |
| `zoom` | Motion | `Zoom` | `zoom` | · |   |   |   | · | · |   |   |   |   |   | · | · |
| `rotation` | Motion | `Rotation` | `rotation` | · |   |   |   | · | · |   | · |   | · |   | · | · |
| `endlessZoom` | Motion | `Endless zoom` | `endlessZoom` | · |   |   |   |   | · |   |   |   |   |   |   |   |
| `endlessZoomSpeed` | Motion | `Dive speed` | `endlessZoomSpeed` | · |   |   |   |   | · |   |   |   |   |   |   |   |
| `sway` | Motion | `Sway` | `sway` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `pulse` | Motion | `Beat pulse` | `pulse` | · |   |   |   |   |   |   | · |   |   |   | · | · |
| `driftX` | Motion | `Drift X` | `driftX` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `driftY` | Motion | `Drift Y` | `driftY` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `shake` | Motion | `Beat shake` | `shake` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `audioDrive` | Behavior | `Audio drive` | `audioDrive` | · | · | · | · | · |   | · | · | · | · | · |   |   |
| `beatResponse` | Behavior | `Beat response` | `beatResponse` | · | · | · | · | · | · | · | · | · | · | · |   |   |
| `turbulence` | Behavior | `Turbulence` | `turbulence` | · |   |   | · | · |   |   | · |   |   |   |   |   |
| `density` | Behavior | `Density` | `density` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `trails` | Behavior | `Trails (particle scenes)` | `trails` |   | · |   |   | · |   |   |   | · |   |   | · | · |
| `trailLength` | Behavior | `Trail length` | `trailLength` |   | · |   |   |   |   |   |   |   |   |   | · | · |
| `trailZoom` | Behavior | `Trail zoom (echo in/out)` | `trailZoom` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `trailWarp` | Behavior | `Trail warp (liquid echo)` | `trailWarp` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `mirror` | Behavior | `Mirror` | `mirror` | · |   |   |   |   | · |   |   | · |   |   | · | · |
| `warp` | Shape | `Domain warp` | `warp` | · |   |   |   | · |   |   |   |   |   |   | · | · |
| `ripple` | Shape | `Ripple` | `ripple` | · |   |   |   |   |   |   |   |   | · |   | · | · |
| `symmetry` | Shape | `Kaleidoscope` | `symmetry` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `kaleidoscope` | Shape | `Kaleidoscope` | `kaleidoscope` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `morph` | Shape | `Morph` | `morph` | · |   |   |   |   |   |   | · |   |   |   | · |   |
| `pixelate` | Shape | `Pixelate` | `pixelate` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `posterize` | Shape | `Posterize` | `posterize` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `particleShape` | Shape | `Particle shape` | `particleShape` |   |   |   |   |   |   | · | · |   |   |   |   |   |
| `particleSize` | Shape | `Particle size` | `particleSize` |   |   |   |   |   |   | · | · |   |   |   |   |   |
| `tile` | Shape | `Tile` | `tile` | · |   |   |   |   |   |   | · |   |   |   | · | · |
| `twist` | Shape | `Twist` | `twist` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `palette` | Color | `Palette` | `palette` | · | · | · | · | · | · | · | · | · | · | · | · |   |
| `palette2` | Color | `Palette 2` | `palette2` | · |   |   |   |   |   |   |   |   |   |   |   |   |
| `paletteMix` | Color | `Palette blend` | `paletteMix` | · |   |   |   |   |   |   |   |   |   |   |   |   |
| `paletteBaseOverride` | Color | — | `paletteBaseOverride` | · | · | · | · | · | · | · | · | · | · | · | · |   |
| `paletteRangeOverride` | Color | — | `paletteRangeOverride` | · | · | · | · | · | · | · | · | · | · |   | · |   |
| `palette2BaseOverride` | Color | — | `palette2BaseOverride` | · |   |   |   |   |   |   |   |   |   |   | · |   |
| `palette2RangeOverride` | Color | — | `palette2RangeOverride` | · |   |   |   |   |   |   |   |   |   |   | · |   |
| `customPaletteId` | Color | — | `customPaletteId` |   |   |   |   |   |   |   |   |   |   |   |   |   |
| `paletteLut` | Color | `Colour map` | `paletteLut` | · |   |   |   |   |   |   |   |   |   |   |   |   |
| `customPalette2Id` | Color | — | `customPalette2Id` |   |   |   |   |   |   |   |   |   |   |   |   |   |
| `milkdropPaletteTint` | Color | `MilkDrop palette tint` | `milkdropPaletteTint` |   |   |   |   |   | · |   |   |   |   |   | · |   |
| `milkdropBlendPresets` | Behavior | `Blend preset changes` | `milkdropBlendPresets` |   |   |   |   |   | · |   |   |   |   |   |   |   |
| `colorShift` | Color | `Hue shift` | `colorShift` | · |   |   |   |   | · |   | · |   |   |   | · | · |
| `hueRange` | Color | `Hue range` | `hueRange` | · | · | · | · | · | · | · | · | · | · |   | · |   |
| `saturation` | Color | `Saturation` | `saturation` | · |   |   |   | · | · |   |   |   |   |   | · | · |
| `brightness` | Color | `Brightness` | `brightness` | · | · |   |   | · | · | · | · | · | · | · | · | · |
| `contrast` | Color | `Contrast` | `contrast` | · |   |   |   |   | · |   |   |   |   |   | · | · |
| `gamma` | Color | `Gamma` | `gamma` | · |   |   |   |   | · |   |   |   |   |   | · | · |
| `colorCycle` | Color | `Color cycle` | `colorCycle` | · |   |   |   |   | · |   |   |   |   |   | · | · |
| `cycleSpeed` | Color | `Cycle speed` | `cycleSpeed` | · |   |   |   |   | · |   |   |   |   |   | · | · |
| `invert` | Color | `Invert` | `invert` | · |   |   |   |   | · |   |   |   |   |   | · | · |
| `intensity` | Color | `Intensity` | `intensity` | · |   |   |   |   | · |   | · | · |   |   | · | · |
| `duotone` | Color | `Duotone` | `duotone` | · |   |   |   |   |   |   |   |   |   |   |   |   |
| `bloom` | Color | `Bloom` | `bloom` | · |   |   |   |   |   | · | · |   |   |   | · | · |
| `temperature` | Color | `Temperature` | `temperature` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `solarize` | Color | `Solarize` | `solarize` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `bassGain` | Behavior | `Bass gain` | `bassGain` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `midGain` | Behavior | `Mid gain` | `midGain` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `trebGain` | Behavior | `Treble gain` | `trebGain` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `flash` | Behavior | `Beat flash` | `flash` | · |   |   |   | · |   | · |   |   |   |   | · | · |
| `chromaAb` | FX | `Chromatic aberration` | `chromaAb` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `vignette` | FX | `Vignette` | `vignette` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `scanlines` | FX | `Scanlines` | `scanlines` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `grain` | FX | `Film grain` | `grain` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `glitch` | FX | `Glitch` | `glitch` |   |   |   |   | · |   |   |   |   |   |   | · | · |
| `fisheye` | FX | `Fisheye` | `fisheye` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `strobe` | FX | `Strobe` | `strobe` |   |   |   |   | · |   |   | · |   |   |   | · | · |
| `paramFadeSec` | FX | — | `paramFadeSec` |   |   |   |   |   |   |   |   |   |   |   | · |   |
| `fluidQuality` | Fluid | — | `fluidQuality` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidAutoQuality` | Fluid | — | `fluidAutoQuality` |   |   |   |   |   |   | · |   | · |   |   |   | · |
| `fluidIterations` | Fluid | `Solver iterations` | `fluidIterations` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidPressure` | Fluid | `Pressure` | `fluidPressure` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidCurl` | Fluid | `Fluid curl` | `fluidCurl` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidVelocityDissipation` | Fluid | `Motion fade` | `fluidVelocityDissipation` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidDensityDissipation` | Fluid | `Fluid fade` | `fluidDensityDissipation` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidChromaticAging` | Fluid | `Chromatic aging` | `fluidChromaticAging` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidSplatRadius` | Fluid | `Fluid splat radius` | `fluidSplatRadius` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidSplatForce` | Fluid | `Fluid splat force` | `fluidSplatForce` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidBeatPattern` | Fluid | `Beat pattern` | `fluidBeatPattern` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidBeatSplats` | Fluid | `Beat splats` | `fluidBeatSplats` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidStirrers` | Fluid | `Stirrers` | `fluidStirrers` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidStirrerSpeed` | Fluid | `Stirrer speed` | `fluidStirrerSpeed` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidBassPump` | Fluid | `Bass pump` | `fluidBassPump` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidPaletteCycleSpeed` | Fluid | `Palette cycle` | `fluidPaletteCycleSpeed` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidSparkle` | Fluid | `Treble sparkle` | `fluidSparkle` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `fluidSpawnPath` | Fluid | `Path` | `fluidSpawnPath` |   |   |   |   |   |   | · | · | · |   |   |   |   |
| `fluidSpawnPoints` | Fluid | `Spawn points` | `fluidSpawnPoints` |   |   |   |   |   |   | · | · | · |   |   |   |   |
| `fluidSpawnProgress` | Fluid | — | `fluidSpawnProgress` |   |   |   |   |   |   | · | · | · |   |   |   |   |
| `fluidCatchPoints` | Fluid | `Catch points` | `fluidCatchPoints` |   |   |   |   |   |   | · | · | · |   |   |   |   |
| `fluidCatchPull` | Fluid | `Catch pull` | `fluidCatchPull` |   |   |   |   |   |   | · | · | · |   |   |   |   |
| `fluidCatchRadius` | Fluid | `Catch radius` | `fluidCatchRadius` |   |   |   |   |   |   | · | · | · |   |   |   |   |
| `fluidParticlesEnabled` | Fluid | — | `fluidParticlesEnabled` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidParticleLife` | Fluid | `Particle life (s)` | `fluidParticleLife` |   |   |   |   |   |   | · | · |   |   |   |   |   |
| `fluidParticleDrag` | Fluid | `Particle drag` | `fluidParticleDrag` |   |   |   |   |   |   | · | · |   |   |   |   |   |
| `fluidParticleBrightness` | Fluid | `Particle brightness` | `fluidParticleBrightness` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidDyeEnabled` | Fluid | — | `fluidDyeEnabled` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidShading` | Fluid | `Shading (embossed ink)` | `fluidShading` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidBloom` | Fluid | `Glow (fluid)` | `fluidBloom` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidBloomIntensity` | Fluid | `Fluid glow` | `fluidBloomIntensity` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidBloomThreshold` | Fluid | `Glow threshold` | `fluidBloomThreshold` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidSunrays` | Fluid | `Sunrays` | `fluidSunrays` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidSunraysWeight` | Fluid | `Sunrays weight` | `fluidSunraysWeight` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidCurlAudio` | Fluid | `Curl from mids` | `fluidCurlAudio` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidBloomAudio` | Fluid | `Glow from loudness` | `fluidBloomAudio` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidFadeAudio` | Fluid | `Fade when quiet` | `fluidFadeAudio` |   |   |   |   |   |   | · |   |   |   |   |   |   |
| `fluidRadiusPulse` | Fluid | `Radius on beat` | `fluidRadiusPulse` |   |   |   |   |   |   | · |   | · |   |   |   |   |
| `flowEnabled` | Fluid | — | `flowEnabled` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `flowStrength` | Fluid | `Flow strength` | `flowStrength` | · |   |   |   |   |   |   |   |   |   |   | · | · |
| `flowForce` | Fluid | `Flow force` | `flowForce` |   |   |   |   |   |   |   |   |   |   |   | · |   |
| `flowCurl` | Fluid | `Flow curl` | `flowCurl` |   |   |   |   |   |   |   |   |   |   |   | · |   |
| `waterWaveSpeed` | Fluid | `Wave speed` | `waterWaveSpeed` |   |   |   |   |   |   |   |   | · |   |   | · | · |
| `waterDamping` | Fluid | `Damping` | `waterDamping` |   |   |   |   |   |   |   |   | · |   |   | · | · |
| `waterRippleStrength` | Fluid | `Ripple strength` | `waterRippleStrength` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `waterDepth` | Fluid | `Depth` | `waterDepth` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `waterSpecular` | Fluid | `Specular` | `waterSpecular` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `waterFlow` | Fluid | `Flow drift` | `waterFlow` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `waterLiquid` | Fluid | `Liquid` | `waterLiquid` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `waterLiquidFlow` | Fluid | `Liquid flow` | `waterLiquidFlow` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `waterLiquidFade` | Fluid | `Liquid fade` | `waterLiquidFade` |   |   |   |   |   |   |   |   | · |   |   |   |   |
| `cymaticsGeometry` | Cymatics | `Geometry` | `cymaticsGeometry` |   |   |   |   |   |   |   |   |   | · | · |   |   |
| `cymaticsFundamental` | Cymatics | `Fundamental (Hz)` | `cymaticsFundamental` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsModes` | Cymatics | `Standing waves` | `cymaticsModes` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsRing` | Cymatics | `Plate ring` | `cymaticsRing` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsFocus` | Cymatics | `Tonal focus` | `cymaticsFocus` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsScale` | Cymatics | `Field scale` | `cymaticsScale` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsFill` | Cymatics | `Fill` | `cymaticsFill` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsLine` | Cymatics | `Nodal lines` | `cymaticsLine` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsGlow` | Cymatics | `Nodal glow` | `cymaticsGlow` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsIridescence` | Cymatics | `Iridescence` | `cymaticsIridescence` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsCaustic` | Cymatics | `Caustic sheen` | `cymaticsCaustic` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsFlow` | Cymatics | `Wave flow` | `cymaticsFlow` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `cymaticsSwirl` | Cymatics | `Field swirl` | `cymaticsSwirl` |   |   |   |   |   |   |   |   |   | · |   |   |   |
| `beamXy` | Shape | `XY plot` | `beamXy` |   |   |   |   |   |   |   |   |   |   | · |   |   |
| `beamWidth` | Shape | `Beam width` | `beamWidth` |   |   |   |   |   |   |   |   |   |   | · |   |   |
| `beamIntensity` | Shape | `Beam brightness` | `beamIntensity` |   |   |   |   |   |   |   |   |   |   | · |   |   |
| `beamTail` | Shape | `Beam tail` | `beamTail` |   |   |   |   |   |   |   |   |   |   | · |   |   |
| `rippleOverlayEnabled` | Fluid | — | `rippleOverlayEnabled` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `rippleOverlayStrength` | Fluid | `Ripple overlay strength` | `rippleOverlayStrength` |   |   |   |   |   |   |   |   |   |   |   | · | · |
| `rippleOverlaySpecular` | Fluid | `Ripple glint` | `rippleOverlaySpecular` |   |   |   |   |   |   |   |   |   |   |   | · | · |

## Never randomized

Declared in `ParamRandomizer.NEVER_ROLLED`; the test checks the list against
the parameters no roll actually writes, in both directions.

| Parameter | Why |
|---|---|
| `paletteBaseOverride` | a user-made palette's own hue |
| `paletteRangeOverride` | a user-made palette's own hue span |
| `palette2BaseOverride` | a user-made palette's own hue (slot 2) |
| `palette2RangeOverride` | a user-made palette's own hue span (slot 2) |
| `customPaletteId` | which saved palette slot 1 uses |
| `customPalette2Id` | which saved palette slot 2 uses |
| `paramFadeSec` | an automation preference, not a look |
| `fluidQuality` | a performance setting, not a look |
| `fluidAutoQuality` | a performance setting, not a look |
| `fluidSpawnProgress` | how much the song itself drives the look |
| `fluidParticlesEnabled` | the fluid particle layer's master switch |
| `fluidDyeEnabled` | the fluid ink layer's master switch |
| `flowEnabled` | the FlowField's master switch |
| `rippleOverlayEnabled` | the water-ripple overlay's master switch |

## Rendered by nothing

Declared in `SceneParams.NOT_RENDERED`; every other parameter has to be read
by a scene, the composite pass or the export compositor.

| Parameter | What it is for |
|---|---|
| `customPaletteId` | which saved palette slot 1 uses; rendering reads the resolved hues |
| `customPalette2Id` | which saved palette slot 2 uses; rendering reads the resolved hues |

## Tabs

Tabs are `render.scene.CustomizeTab`: the panel builds its row from that enum
and "⚄ Randomize <tab>" rolls exactly the keys below it.

| Tab | Controls | Rolled keys |
|---|---|---|
| Motion | 10 | 10 |
| Shape | 15 | 14 |
| Behavior | 14 | 14 |
| Color | 25 | 19 |
| FX | 8 | 7 |
| Fluid | 54 | 47 |
| Cymatics | 13 | 13 |
