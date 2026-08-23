package dev.geode.render.scene

data class SceneParams(
    val speed: Float = 1f,
    val zoom: Float = 1f,
    val rotation: Float = 0f,
    val endlessZoom: Boolean = false,
    val endlessZoomSpeed: Float = 0.3f,
    val sway: Float = 0f,
    val pulse: Float = 0f,
    val driftX: Float = 0f,
    val driftY: Float = 0f,
    val shake: Float = 0f,
    val audioDrive: Float = 1f,
    val beatResponse: Float = 1f,
    val turbulence: Float = 0f,
    val density: Float = 1f,
    /**
     * March-step budget for the raymarched fragment styles, 0.25..1.5.
     *
     * This was `hyperDetail` and belonged to the Hyperspace family, which is gone. The
     * control outlived it because VANISHING, MORPHOGEN, NEBULA, NONEUCLID and KIFS are all
     * distance-marched and all need somewhere to spend or save steps; naming it after the
     * technique rather than a style means the next marched style inherits it for free.
     * [MarchBudget.forDetail] turns it into the `uSteps` uniform every one of them breaks on.
     */
    val marchDetail: Float = 1f,
    val trails: Boolean = false,
    val trailLength: Float = 0.5f,
    val trailZoom: Float = 0f,
    val trailWarp: Float = 0f,
    val mirror: Boolean = false,
    val warp: Float = 0f,
    val ripple: Float = 0f,
    val symmetry: Int = DEFAULT_SYMMETRY_FOLDS,
    val kaleidoscope: Boolean = false,
    val morph: Float = 0f,
    val pixelate: Float = 0f,
    val posterize: Float = 0f,
    val particleShape: Int = 0,
    val particleSize: Float = 1f,
    val tile: Float = 1f,
    val twist: Float = 0f,
    val palette: Int = 0,
    val palette2: Int = 1,
    val paletteMix: Float = 0f,
    val paletteBaseOverride: Float = UNSET_OVERRIDE,
    val paletteRangeOverride: Float = UNSET_OVERRIDE,
    val palette2BaseOverride: Float = UNSET_OVERRIDE,
    val palette2RangeOverride: Float = UNSET_OVERRIDE,
    val customPaletteId: String = NO_CUSTOM_PALETTE,
    val paletteLut: Int = NO_PALETTE_LUT,
    val customPalette2Id: String = NO_CUSTOM_PALETTE,
    val milkdropPaletteTint: Float = 0f,
    val milkdropBlendPresets: Boolean = false,
    val colorShift: Float = 0f,
    val hueRange: Float = 1f,
    val saturation: Float = 1f,
    val brightness: Float = 1f,
    val contrast: Float = 1f,
    val gamma: Float = 1f,
    val colorCycle: Boolean = false,
    val cycleSpeed: Float = 0.1f,
    val invert: Boolean = false,
    val intensity: Float = 1f,
    val duotone: Boolean = false,
    val bloom: Float = 0f,
    val temperature: Float = 0f,
    val solarize: Boolean = false,
    val bassGain: Float = 1f,
    val midGain: Float = 1f,
    val trebGain: Float = 1f,
    val flash: Float = 0f,
    val chromaAb: Float = 0f,
    val vignette: Float = 0f,
    val scanlines: Float = 0f,
    val grain: Float = 0f,
    val glitch: Float = 0f,
    val fisheye: Float = 0f,
    val strobe: Float = 0f,
    val paramFadeSec: Float = 0f,
    val fluidQuality: Int = 2,
    val fluidAutoQuality: Boolean = true,
    val fluidIterations: Int = 20,
    val fluidPressure: Float = 0.8f,
    val fluidCurl: Float = 30f,
    val fluidVelocityDissipation: Float = 0.2f,
    val fluidDensityDissipation: Float = 1f,
    val fluidChromaticAging: Float = 0.3f,
    val fluidSplatRadius: Float = 0.12f,
    val fluidSplatForce: Float = 1f,
    val fluidBeatPattern: Int = 1,
    val fluidBeatSplats: Int = 3,
    val fluidStirrers: Int = 2,
    val fluidStirrerSpeed: Float = 1f,
    val fluidBassPump: Boolean = false,
    val fluidPaletteCycleSpeed: Float = 0.5f,
    val fluidSparkle: Boolean = true,
    val fluidSpawnPath: Int = 1,
    val fluidSpawnPoints: Int = 3,
    val fluidSpawnProgress: Float = 1f,
    val fluidCatchPoints: Int = 2,
    val fluidCatchPull: Float = 1f,
    val fluidCatchRadius: Float = 0.12f,
    val fluidParticlesEnabled: Boolean = true,
    val fluidParticleLife: Float = 6f,
    val fluidParticleDrag: Float = 0.5f,
    val fluidParticleBrightness: Float = 1f,
    val fluidDyeEnabled: Boolean = true,
    val fluidShading: Boolean = true,
    val fluidBloom: Boolean = true,
    val fluidBloomIntensity: Float = 0.8f,
    val fluidBloomThreshold: Float = 0.6f,
    val fluidSunrays: Boolean = true,
    val fluidSunraysWeight: Float = 1f,
    val fluidCurlAudio: Float = 0.5f,
    val fluidBloomAudio: Float = 0.5f,
    val fluidFadeAudio: Float = 0.6f,
    val fluidRadiusPulse: Float = 0.4f,
    val flowEnabled: Boolean = false,
    val flowStrength: Float = 0.35f,
    val flowForce: Float = 1f,
    val flowCurl: Float = 25f,
    val waterWaveSpeed: Float = 1f,
    val waterDamping: Float = 0.985f,
    val waterRippleStrength: Float = 1f,
    val waterDepth: Float = 0.6f,
    val waterSpecular: Float = 0.7f,
    val waterFlow: Float = 0.3f,
    val waterLiquid: Float = 0.85f,
    val waterLiquidFlow: Float = 1.4f,
    val waterLiquidFade: Float = 0.35f,
    val cymaticsGeometry: Int = 0,
    val cymaticsFundamental: Float = 110f,
    val cymaticsModes: Int = 5,
    val cymaticsRing: Float = 0.35f,
    val cymaticsFocus: Float = 0.7f,
    val cymaticsScale: Float = 3.2f,
    val cymaticsFill: Float = 0.45f,
    val cymaticsLine: Float = 1f,
    val cymaticsGlow: Float = 1f,
    val cymaticsIridescence: Float = 0.5f,
    val cymaticsCaustic: Float = 0.8f,
    val cymaticsFlow: Float = 0.35f,
    val cymaticsSwirl: Float = 0.05f,
    val beamXy: Boolean = false,
    val beamWidth: Float = 1f,
    val beamIntensity: Float = 1f,
    val beamTail: Float = 0.35f,
    val rippleOverlayEnabled: Boolean = false,
    val rippleOverlayStrength: Float = 0.4f,
    val rippleOverlaySpecular: Float = 0.3f,
) {
    companion object {
        const val UNSET_OVERRIDE: Float = -1f

        const val NO_CUSTOM_PALETTE: String = ""

        const val NO_PALETTE_LUT: Int = -1

        val CYCLIC_PALETTES: List<String> = dev.geode.render.CyclicPalettes.NAMES

        val DEFAULT: SceneParams = SceneParams()

        val NOT_RENDERED: Map<String, String> =
            mapOf(
                "customPaletteId" to "which saved palette slot 1 uses; rendering reads the resolved hues",
                "customPalette2Id" to "which saved palette slot 2 uses; rendering reads the resolved hues",
            )

        val PALETTES: List<Triple<String, Float, Float>> =
            listOf(
                Triple("Spectrum", 0.0f, 1.0f),
                Triple("Neon", 0.5f, 0.45f),
                Triple("Fire", 0.0f, 0.14f),
                Triple("Ocean", 0.5f, 0.2f),
                Triple("Mono", 0.6f, 0.02f),
                Triple("Candy", 0.85f, 0.5f),
                Triple("Forest", 0.33f, 0.18f),
                Triple("Aurora", 0.45f, 0.7f),
                Triple("Sunset", 0.05f, 0.3f),
                Triple("Ice", 0.55f, 0.15f),
                Triple("Vapor", 0.78f, 0.35f),
                Triple("Toxic", 0.25f, 0.25f),
                Triple("Royal", 0.7f, 0.25f),
                Triple("Blush", 0.93f, 0.12f),
                Triple("Copper", 0.07f, 0.1f),
                Triple("Mint", 0.4f, 0.12f),
                Triple("Galaxy", 0.65f, 0.5f),
                Triple("Cherry", 0.97f, 0.08f),
                Triple("Cyan", 0.5f, 0.08f),
                Triple("Magenta", 0.833f, 0.08f),
                Triple("Yellow", 0.167f, 0.08f),
            )

        val PARTICLE_SHAPES: List<String> = listOf("Dot", "Ring", "Star", "Square", "Spark", "Hex", "Bubble")

        val SYMMETRY_FOLDS: List<Int> = listOf(0, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16)

        const val DEFAULT_SYMMETRY_FOLDS: Int = 6

        val FLUID_PATTERNS: List<String> = listOf("Center", "Ring", "Random", "Spectrum")

        val FLUID_PATHS: List<String> = listOf("Orbit", "Lissajous", "Rose", "Bloom", "Drift")

        val CYMATICS_GEOMETRIES: List<String> = listOf("Water dish", "Chladni plate")
    }

    val usesCustomPalette: Boolean
        get() = paletteBaseOverride >= 0f || paletteRangeOverride >= 0f

    val usesCustomPalette2: Boolean
        get() = palette2BaseOverride >= 0f || palette2RangeOverride >= 0f

    val paletteBase: Float
        get() = if (paletteBaseOverride >= 0f) paletteBaseOverride else PALETTES[palette.coerceIn(0, PALETTES.size - 1)].second

    val paletteRange: Float
        get() = if (paletteRangeOverride >= 0f) paletteRangeOverride else PALETTES[palette.coerceIn(0, PALETTES.size - 1)].third

    val palette2Base: Float
        get() = if (palette2BaseOverride >= 0f) palette2BaseOverride else PALETTES[palette2.coerceIn(0, PALETTES.size - 1)].second

    val palette2Range: Float
        get() = if (palette2RangeOverride >= 0f) palette2RangeOverride else PALETTES[palette2.coerceIn(0, PALETTES.size - 1)].third

    fun withoutCustomPalette(second: Boolean = false): SceneParams =
        if (second) {
            copy(
                palette2BaseOverride = UNSET_OVERRIDE,
                palette2RangeOverride = UNSET_OVERRIDE,
                customPalette2Id = NO_CUSTOM_PALETTE,
            )
        } else {
            copy(
                paletteBaseOverride = UNSET_OVERRIDE,
                paletteRangeOverride = UNSET_OVERRIDE,
                customPaletteId = NO_CUSTOM_PALETTE,
            )
        }
}

fun applyBandGains(
    f: dev.geode.analysis.AudioFeatures,
    p: SceneParams,
): dev.geode.analysis.AudioFeatures {
    if (p.bassGain == 1f && p.midGain == 1f && p.trebGain == 1f) return f
    return f.copy(
        bass = (f.bass * p.bassGain).coerceIn(0f, 2f),
        mid = (f.mid * p.midGain).coerceIn(0f, 2f),
        treble = (f.treble * p.trebGain).coerceIn(0f, 2f),
    )
}
