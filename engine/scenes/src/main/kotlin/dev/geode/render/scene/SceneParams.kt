package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative

data class SceneParams(
    val speed: Float = 1f,
    val zoom: Float = 1f,
    val rotation: Float = 0f,
    val endlessZoom: Boolean = false,
    val endlessZoomSpeed: Float = 0.3f,
    val sway: Float = 0f,
    val driftX: Float = 0f,
    val driftY: Float = 0f,
    val audioDrive: Float = 1f,
    val turbulence: Float = 0f,
    val density: Float = 1f,
    /** March-step budget for the raymarched styles, 0.25..1.5: native ShaderScene's `uSteps`. */
    val marchDetail: Float = 1f,
    val trails: Boolean = true,
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
    val chromaAb: Float = 0f,
    val vignette: Float = 0f,
    val scanlines: Float = 0f,
    val grain: Float = 0f,
    val fisheye: Float = 0f,
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
    val rippleOverlayEnabled: Boolean = false,
    val rippleOverlayStrength: Float = 0.4f,
    val rippleOverlaySpecular: Float = 0.3f,
    // Wave three: the continuous motion system (core/viz/MotionField).
    val motionAmount: Float = 0.7f,
    val motionBreath: Float = 0.5f,
    val motionDrift: Float = 0.5f,
    val motionHue: Float = 0.4f,
    // The camera rig (core/viz/CameraRig); push/yaw/pitch/roll are the offsets routes and LFOs write.
    val camFly: Float = 0.5f,
    val camPath: Int = 0,
    val camOrbit: Float = 0.35f,
    val camPush: Float = 0f,
    val camYaw: Float = 0f,
    val camPitch: Float = 0f,
    val camRoll: Float = 0f,
    val camFov: Float = 1f,
    val camShake: Float = 0f,
    val camSmooth: Float = 0.5f,
    val camTouch: Boolean = false,
    val camGyro: Boolean = false,
    /** Global scale of every reaction route, 0..2; 0 leaves only the continuous motion system. */
    val reactAmount: Float = 1f,
    /** Which variant of the style's reaction DNA ([BuildStyleReactionsUseCase]) plays. */
    val reactSeed: Int = 0,
    /**
     * The first of Note & key reactions' fields (core/viz Params.hpp, after [reactSeed]). Each is a
     * per-style Customize control, inert while Settings › Behaviour › Note & key reactions is off, and the
     * ParamRandomizer never rolls them. (The note blooms' `bloomLook` and `noteBloom` are gone with the
     * blooms; a preset or link that still carries them loads, and the keys are ignored.)
     *
     * How strongly the key and notes tint the palette, 0..2 (1 = as designed).
     */
    val noteColor: Float = 1f,
    /** Note & key reactions: how strongly pitch height moves the picture, 0..2. */
    val notePitch: Float = 1f,
    /** Note & key reactions: how strongly melodic intervals (steps against leaps) answer, 0..2. */
    val noteIntervals: Float = 1f,
    /** Note & key reactions: how strongly harmonic tension drives the picture, 0..2. */
    val noteTension: Float = 1f,
    /** Note & key reactions: how strongly chord changes answer, 0..2. */
    val noteChords: Float = 1f,
    /** Note & key reactions: how strongly the drum hits answer, 0..2. */
    val noteDrums: Float = 1f,
    /**
     * Liquid mode (Customize › Advanced › Liquid; the switch is [flowEnabled], Liquidity [flowStrength]):
     * what forms at the vanishing point. 0 the live style, 1 a re-roll of it, 2 the next style, 3 ink forms.
     */
    val liquidBorn: Int = 1,
    /** Liquid mode: what ageing liquid does. 0 keeps the palette, 1 fades with depth, 2 bleeds ink. */
    val liquidAge: Int = 0,
    /** Liquid mode: the liquid's surface. 0 ink, 1 wet gloss, 2 glass. */
    val liquidSurface: Int = 0,
    /** Camera travel on the flat styles: how far Fly speed (its sign too) dives the picture in or out. 0 off .. 1. */
    val camFlat: Float = 0f,
    /** Your colours: the first palette's own stops, packed 0xRRGGBB, set from the first; [NO_STOP] unset (core/viz/Colour.hpp). */
    val paletteStop0: Int = NO_STOP,
    val paletteStop1: Int = NO_STOP,
    val paletteStop2: Int = NO_STOP,
    val paletteStop3: Int = NO_STOP,
    val paletteStop4: Int = NO_STOP,
    // Endless zoom's tunnel (core/viz/EndlessZoom.hpp, Customize › Dive); the defaults are the plain tunnel.
    val diveSpacing: Float = 1f,
    val diveTwist: Float = 0f,
    val diveAimX: Float = 0f,
    val diveAimY: Float = 0f,
    val diveShape: Float = 0.5f,
    val diveBlend: Float = 0.75f,
    val diveFog: Float = 0f,
    val diveLens: Float = 0f,
    val diveFollow: Float = 1f,
) {
    companion object {
        const val UNSET_OVERRIDE: Float = -1f

        const val NO_CUSTOM_PALETTE: String = ""

        const val NO_PALETTE_LUT: Int = -1

        const val NO_STOP: Int = -1

        val CYCLIC_PALETTES: List<String> = dev.geode.render.CyclicPalettes.NAMES

        val DEFAULT: SceneParams = SceneParams()

        val NOT_RENDERED: Map<String, String> =
            mapOf(
                "customPaletteId" to "which saved palette slot 1 uses; rendering reads the resolved hues",
                "customPalette2Id" to "which saved palette slot 2 uses; rendering reads the resolved hues",
            )

        /** The built-in palettes' names, in `palette` index order: labels only, from the native params schema. */
        val PALETTES: List<String> get() = ParamSchema.paletteNames

        val PARTICLE_SHAPES: List<String> = listOf("Dot", "Ring", "Star", "Square", "Spark", "Hex", "Bubble")

        val SYMMETRY_FOLDS: List<Int> = listOf(0, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16)

        const val DEFAULT_SYMMETRY_FOLDS: Int = 6

        val FLUID_PATTERNS: List<String> = listOf("Center", "Ring", "Random", "Spectrum")

        val FLUID_PATHS: List<String> = listOf("Orbit", "Lissajous", "Rose", "Bloom", "Drift")

        val CYMATICS_GEOMETRIES: List<String> = listOf("Water dish", "Chladni plate")

        /** camPath values, in order; mirrors CameraRig::pathPoint. */
        val CAMERA_PATHS: List<String> = listOf("Glide", "Spiral", "Orbit", "Hover", "Drive", "Fall", "Rise")
    }

    val usesCustomPalette: Boolean
        get() = paletteBaseOverride >= 0f || paletteRangeOverride >= 0f || paletteStop0 != NO_STOP

    val paletteStops: List<Int>
        get() = listOf(paletteStop0, paletteStop1, paletteStop2, paletteStop3, paletteStop4).takeWhile { it != NO_STOP }

    val usesCustomPalette2: Boolean
        get() = palette2BaseOverride >= 0f || palette2RangeOverride >= 0f

    // The override when it is set, else the built-in palette's (index clamped into the table): native,
    // `geode_params_palette_base`/`_range`, the same rule the renderer draws by.
    val paletteBase: Float
        get() = GeodeNative.paramsPaletteBase(palette, paletteBaseOverride)

    val paletteRange: Float
        get() = GeodeNative.paramsPaletteRange(palette, paletteRangeOverride)

    val palette2Base: Float
        get() = GeodeNative.paramsPaletteBase(palette2, palette2BaseOverride)

    val palette2Range: Float
        get() = GeodeNative.paramsPaletteRange(palette2, palette2RangeOverride)

    fun withoutCustomPalette(second: Boolean = false): SceneParams =
        if (second) {
            copy(
                palette2BaseOverride = UNSET_OVERRIDE,
                palette2RangeOverride = UNSET_OVERRIDE,
                customPalette2Id = NO_CUSTOM_PALETTE,
            )
        } else {
            copy(paletteBaseOverride = UNSET_OVERRIDE, paletteRangeOverride = UNSET_OVERRIDE, customPaletteId = NO_CUSTOM_PALETTE)
                .withPaletteStops(emptyList())
        }

    /** Your colours in the stop fields, the first [colours] in order (at most five), the rest unset. */
    fun withPaletteStops(colours: List<Int>): SceneParams {
        val s = List(5) { colours.getOrElse(it) { NO_STOP } }
        return copy(paletteStop0 = s[0], paletteStop1 = s[1], paletteStop2 = s[2], paletteStop3 = s[3], paletteStop4 = s[4])
    }
}
