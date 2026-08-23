package dev.geode.render.scene

import kotlin.random.Random

object ParamRandomizer {
    private const val REROLLS = 8

    val NEVER_ROLLED: Map<String, String> =
        mapOf(
            "paletteBaseOverride" to "a user-made palette's own hue",
            "paletteRangeOverride" to "a user-made palette's own hue span",
            "palette2BaseOverride" to "a user-made palette's own hue (slot 2)",
            "palette2RangeOverride" to "a user-made palette's own hue span (slot 2)",
            "customPaletteId" to "which saved palette slot 1 uses",
            "customPalette2Id" to "which saved palette slot 2 uses",
            "paramFadeSec" to "an automation preference, not a look",
            "fluidQuality" to "a performance setting, not a look",
            "fluidAutoQuality" to "a performance setting, not a look",
            "fluidSpawnProgress" to "how much the song itself drives the look",
            "fluidParticlesEnabled" to "the fluid particle layer's master switch",
            "fluidDyeEnabled" to "the fluid ink layer's master switch",
            "flowEnabled" to "the FlowField's master switch",
            "rippleOverlayEnabled" to "the water-ripple overlay's master switch",
        )

    /**
     * Rolls a new look.
     *
     * [sceneId] scopes the roll to what the style on screen can actually show: rolling
     * `Morph` on a fluid style or `Fluid curl` on a shader burns the reroll on a parameter the
     * user cannot see and cannot feel, which is how "randomize" ends up appearing to do nothing.
     * Pass null to roll the whole surface (that is what builds [KEYS_BY_TAB]).
     */
    fun randomize(
        current: SceneParams,
        locked: Set<String>,
        rng: Random = Random.Default,
        tab: CustomizeTab? = null,
        sceneId: String? = null,
    ): SceneParams {
        repeat(REROLLS) {
            val rolled = roll(current, locked, rng, tab, sceneId, null)
            if (rolled != current) return rolled
        }
        return current
    }

    val KEYS_BY_TAB: Map<CustomizeTab, List<String>> =
        mutableListOf<Pair<CustomizeTab, String>>()
            .also { roll(SceneParams.DEFAULT, emptySet(), Random(0), null, null, it) }
            .groupBy({ it.first }, { it.second })

    val KEYS: List<String> = KEYS_BY_TAB.values.flatten()

    val LOCKABLE_LABELS: Set<String> = KEYS.toSet()

    fun keysFor(tab: CustomizeTab): List<String> = KEYS_BY_TAB[tab].orEmpty()

    /**
     * Puts one tab's parameters back to their defaults and leaves every other tab alone.
     *
     * Which fields a tab owns is worked out from the rolls themselves rather than from a second
     * hand-written table, because a second table drifts: somebody adds a control, wires its roll,
     * and "Reset this tab" quietly stops covering it. A roll is run twice over two parameter sets
     * that differ in EVERY field, with the same seed both times. A field the roll writes comes out
     * identical in both runs (the roll decided it); a field it leaves alone still differs (it came
     * from the input). That is an exact answer, not a sampled one.
     */
    fun resetTab(
        current: SceneParams,
        tab: CustomizeTab,
    ): SceneParams {
        val owned = FIELDS_BY_TAB[tab].orEmpty()
        if (owned.isEmpty()) return current
        val out = current.copy()
        for (field in PARAM_FIELDS) {
            if (field.name in owned) field.set(out, field.get(SceneParams.DEFAULT))
        }
        return out
    }

    private val PARAM_FIELDS: List<java.lang.reflect.Field> =
        SceneParams::class.java.declaredFields
            .filterNot { java.lang.reflect.Modifier.isStatic(it.modifiers) }
            .onEach { it.isAccessible = true }

    /** Seeds unioned so a roll gated behind a `chance()` still reveals what it writes. */
    private val PROBE_SEEDS = listOf(1, 7, 13, 29, 61, 127, 257, 521)

    private val FIELDS_BY_TAB: Map<CustomizeTab, Set<String>> by lazy {
        CustomizeTab.entries.associateWith { tab ->
            buildSet {
                for (seed in PROBE_SEEDS) {
                    val one = roll(probe(1), emptySet(), Random(seed), tab, null, null)
                    val two = roll(probe(2), emptySet(), Random(seed), tab, null, null)
                    for (field in PARAM_FIELDS) {
                        if (field.get(one) == field.get(two)) add(field.name)
                    }
                }
            }
        }
    }

    /** A parameter set whose every field differs from the other probe's. */
    private fun probe(variant: Int): SceneParams {
        val out = SceneParams.DEFAULT.copy()
        PARAM_FIELDS.forEachIndexed { index, field ->
            when (field.type) {
                Float::class.javaPrimitiveType -> field.setFloat(out, 0.017f * index + 0.31f * variant)
                Int::class.javaPrimitiveType -> field.setInt(out, index + 3 * variant)
                Boolean::class.javaPrimitiveType -> field.setBoolean(out, variant == 1)
                else -> field.set(out, "probe$variant-$index")
            }
        }
        return out
    }

    private fun roll(
        current: SceneParams,
        locked: Set<String>,
        rng: Random,
        tab: CustomizeTab?,
        sceneId: String?,
        keySink: MutableList<Pair<CustomizeTab, String>>?,
    ): SceneParams {
        fun f(
            lo: Float,
            hi: Float,
        ) = lo + rng.nextFloat() * (hi - lo)

        fun n(
            lo: Int,
            hi: Int,
        ) = lo + rng.nextInt(hi - lo + 1)

        fun chance(p: Float) = rng.nextFloat() < p

        fun sometimes(
            p: Float,
            lo: Float,
            hi: Float,
        ) = if (chance(p)) f(lo, hi) else 0f

        val folds = SceneParams.SYMMETRY_FOLDS.filter { fold -> fold >= 2 }
        var s = current

        var owner = CustomizeTab.MOTION

        fun section(of: CustomizeTab) {
            owner = of
        }

        fun r(
            key: String,
            block: (SceneParams) -> SceneParams,
        ) {
            keySink?.add(owner to key)
            if (tab != null && tab != owner) return
            if (sceneId != null && !ParamScope.of(key).appliesTo(sceneId)) return
            if (key !in locked) s = block(s)
        }

        section(CustomizeTab.MOTION)
        r(ParamKeys.SPEED) { it.copy(speed = f(0.2f, 2.5f)) }
        r(ParamKeys.ZOOM) { it.copy(zoom = f(0.6f, 2f)) }
        r(ParamKeys.ROTATION) { it.copy(rotation = f(-1.5f, 1.5f)) }
        r(ParamKeys.SWAY) { it.copy(sway = sometimes(0.5f, 0.1f, 0.8f)) }
        r(ParamKeys.DRIFT_X) { it.copy(driftX = sometimes(0.3f, -0.5f, 0.5f)) }
        r(ParamKeys.DRIFT_Y) { it.copy(driftY = sometimes(0.3f, -0.5f, 0.5f)) }
        r(ParamKeys.BEAT_PULSE) { it.copy(pulse = sometimes(0.6f, 0.15f, 0.9f)) }
        r(ParamKeys.BEAT_SHAKE) { it.copy(shake = sometimes(0.3f, 0.1f, 0.6f)) }
        r(ParamKeys.ENDLESS_ZOOM) { it.copy(endlessZoom = chance(0.2f)) }
        r(ParamKeys.DIVE_SPEED) { it.copy(endlessZoomSpeed = f(0.1f, 0.8f)) }
        r(ParamKeys.TURBULENCE) { it.copy(turbulence = sometimes(0.5f, 0.1f, 1f)) }

        section(CustomizeTab.SHAPE)
        r(ParamKeys.XY_PLOT) { it.copy(beamXy = chance(0.35f)) }
        r(ParamKeys.BEAM_WIDTH) { it.copy(beamWidth = f(0.4f, 2.5f)) }
        r(ParamKeys.BEAM_BRIGHTNESS) { it.copy(beamIntensity = f(0.5f, 2f)) }
        r(ParamKeys.BEAM_TAIL) { it.copy(beamTail = f(0.05f, 0.8f)) }
        r(ParamKeys.DOMAIN_WARP) { it.copy(warp = sometimes(0.5f, 0.1f, 0.8f)) }
        r(ParamKeys.RIPPLE) { it.copy(ripple = sometimes(0.4f, 0.1f, 0.8f)) }
        r(ParamKeys.MORPH) { it.copy(morph = sometimes(0.5f, 0.1f, 0.8f)) }
        r(ParamKeys.TWIST) { it.copy(twist = sometimes(0.4f, -0.8f, 0.8f)) }
        r(ParamKeys.KALEIDOSCOPE) {
            val on = chance(0.3f)
            it.copy(kaleidoscope = on, symmetry = if (on) folds.random(rng) else it.symmetry)
        }
        r(ParamKeys.TILE) { it.copy(tile = if (chance(0.25f)) f(2f, 4f) else 1f) }
        r(ParamKeys.PIXELATE) { it.copy(pixelate = sometimes(0.15f, 0.2f, 0.6f)) }
        r(ParamKeys.MIRROR) { it.copy(mirror = chance(0.15f)) }
        r(ParamKeys.PARTICLE_SHAPE) { it.copy(particleShape = rng.nextInt(SceneParams.PARTICLE_SHAPES.size)) }
        r(ParamKeys.PARTICLE_SIZE) { it.copy(particleSize = f(0.5f, 1.8f)) }
        r(ParamKeys.DENSITY) { it.copy(density = f(0.4f, 1f)) }

        section(CustomizeTab.REACTIVITY)
        r(ParamKeys.AUDIO_DRIVE) { it.copy(audioDrive = f(0.6f, 1.8f)) }
        r(ParamKeys.BEAT_RESPONSE) { it.copy(beatResponse = f(0.3f, 2f)) }
        r(ParamKeys.BEAT_FLASH) { it.copy(flash = sometimes(0.5f, 0.1f, 0.6f)) }
        r(ParamKeys.BASS_GAIN) { it.copy(bassGain = f(0.8f, 1.4f)) }
        r(ParamKeys.MID_GAIN) { it.copy(midGain = f(0.8f, 1.4f)) }
        r(ParamKeys.TREBLE_GAIN) { it.copy(trebGain = f(0.8f, 1.4f)) }

        section(CustomizeTab.SCENE)
        r(ParamKeys.BLEND_PRESET_CHANGES) { it.copy(milkdropBlendPresets = chance(0.35f)) }

        section(CustomizeTab.COLOR)
        r(ParamKeys.PALETTE) {
            it.copy(palette = rng.nextInt(SceneParams.PALETTES.size)).withoutCustomPalette()
        }
        r(ParamKeys.COLOUR_MAP) {
            it.copy(
                paletteLut =
                    if (chance(0.25f)) rng.nextInt(SceneParams.CYCLIC_PALETTES.size) else SceneParams.NO_PALETTE_LUT,
            )
        }
        r(ParamKeys.PALETTE_2) {
            it.copy(palette2 = rng.nextInt(SceneParams.PALETTES.size)).withoutCustomPalette(second = true)
        }
        r(ParamKeys.PALETTE_BLEND) { it.copy(paletteMix = sometimes(0.5f, 0.2f, 0.8f)) }
        r(ParamKeys.MILKDROP_PALETTE_TINT) { it.copy(milkdropPaletteTint = sometimes(0.4f, 0.2f, 0.9f)) }
        r(ParamKeys.HUE_SHIFT) { it.copy(colorShift = f(0f, 1f)) }
        r(ParamKeys.HUE_RANGE) { it.copy(hueRange = f(0.5f, 1.5f)) }
        r(ParamKeys.COLOR_CYCLE) { it.copy(colorCycle = chance(0.3f)) }
        r(ParamKeys.CYCLE_SPEED) { it.copy(cycleSpeed = f(0.02f, 0.3f)) }
        r(ParamKeys.SATURATION) { it.copy(saturation = f(0.4f, 1.4f)) }
        r(ParamKeys.BRIGHTNESS) { it.copy(brightness = f(0.7f, 1.3f)) }
        r(ParamKeys.CONTRAST) { it.copy(contrast = f(0.8f, 1.4f)) }
        r(ParamKeys.GAMMA) { it.copy(gamma = f(0.8f, 1.3f)) }
        r(ParamKeys.INTENSITY) { it.copy(intensity = f(0.7f, 1.4f)) }
        r(ParamKeys.TEMPERATURE) { it.copy(temperature = sometimes(0.4f, -0.6f, 0.6f)) }
        r(ParamKeys.BLOOM) { it.copy(bloom = sometimes(0.5f, 0.1f, 0.7f)) }
        r(ParamKeys.POSTERIZE) { it.copy(posterize = sometimes(0.15f, 0.2f, 0.6f)) }
        r(ParamKeys.DUOTONE) { it.copy(duotone = chance(0.1f)) }
        r(ParamKeys.SOLARIZE) { it.copy(solarize = chance(0.05f)) }
        r(ParamKeys.INVERT) { it.copy(invert = chance(0.03f)) }

        section(CustomizeTab.FX)
        r(ParamKeys.TRAILS) { it.copy(trails = chance(0.4f)) }
        r(ParamKeys.TRAIL_LENGTH) { it.copy(trailLength = f(0.3f, 0.9f)) }
        r(ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT) { it.copy(trailZoom = sometimes(0.3f, -0.3f, 0.3f)) }
        r(ParamKeys.TRAIL_WARP_LIQUID_ECHO) { it.copy(trailWarp = sometimes(0.3f, 0.1f, 0.6f)) }
        r(ParamKeys.CHROMATIC_ABERRATION) { it.copy(chromaAb = sometimes(0.4f, 0.1f, 0.5f)) }
        r(ParamKeys.VIGNETTE) { it.copy(vignette = sometimes(0.5f, 0.1f, 0.6f)) }
        r(ParamKeys.SCANLINES) { it.copy(scanlines = sometimes(0.25f, 0.15f, 0.5f)) }
        r(ParamKeys.FILM_GRAIN) { it.copy(grain = sometimes(0.3f, 0.1f, 0.4f)) }
        r(ParamKeys.GLITCH) { it.copy(glitch = sometimes(0.2f, 0.1f, 0.4f)) }
        r(ParamKeys.FISHEYE) { it.copy(fisheye = sometimes(0.25f, -0.5f, 0.5f)) }
        r(ParamKeys.STROBE) { it.copy(strobe = sometimes(0.08f, 0.15f, 0.4f)) }

        section(CustomizeTab.FLUID)
        r(ParamKeys.SOLVER_ITERATIONS) { it.copy(fluidIterations = n(12, 28)) }
        r(ParamKeys.PRESSURE) { it.copy(fluidPressure = f(0.5f, 0.95f)) }

        r(ParamKeys.FLUID_CURL) { it.copy(fluidCurl = f(5f, 45f)) }
        r(ParamKeys.MOTION_FADE) { it.copy(fluidVelocityDissipation = f(0.02f, 0.6f)) }
        r(ParamKeys.FLUID_FADE) { it.copy(fluidDensityDissipation = f(0.2f, 2.2f)) }
        r(ParamKeys.CHROMATIC_AGING) { it.copy(fluidChromaticAging = f(0f, 0.8f)) }

        r(ParamKeys.BEAT_PATTERN) { it.copy(fluidBeatPattern = rng.nextInt(SceneParams.FLUID_PATTERNS.size)) }
        r(ParamKeys.BEAT_SPLATS) { it.copy(fluidBeatSplats = n(1, 6)) }
        r(ParamKeys.STIRRERS) { it.copy(fluidStirrers = n(0, 3)) }
        r(ParamKeys.STIRRER_SPEED) { it.copy(fluidStirrerSpeed = f(0.3f, 1.6f)) }
        r(ParamKeys.FLUID_SPLAT_RADIUS) { it.copy(fluidSplatRadius = f(0.05f, 0.25f)) }
        r(ParamKeys.FLUID_SPLAT_FORCE) { it.copy(fluidSplatForce = f(0.5f, 2f)) }
        r(ParamKeys.BASS_PUMP) { it.copy(fluidBassPump = chance(0.25f)) }
        r(ParamKeys.TREBLE_SPARKLE) { it.copy(fluidSparkle = chance(0.7f)) }
        r(ParamKeys.PALETTE_CYCLE) { it.copy(fluidPaletteCycleSpeed = f(0f, 1.2f)) }

        r(ParamKeys.PATH) { it.copy(fluidSpawnPath = rng.nextInt(SceneParams.FLUID_PATHS.size)) }
        r(ParamKeys.SPAWN_POINTS) { it.copy(fluidSpawnPoints = n(2, 5)) }
        r(ParamKeys.CATCH_POINTS) { it.copy(fluidCatchPoints = n(0, 3)) }
        r(ParamKeys.CATCH_PULL) { it.copy(fluidCatchPull = f(0.4f, 1.8f)) }
        r(ParamKeys.CATCH_RADIUS) { it.copy(fluidCatchRadius = f(0.06f, 0.2f)) }
        r(ParamKeys.PARTICLE_LIFE_S) { it.copy(fluidParticleLife = f(3f, 12f)) }

        r(ParamKeys.PARTICLE_DRAG) { it.copy(fluidParticleDrag = f(0.15f, 0.9f)) }
        r(ParamKeys.PARTICLE_BRIGHTNESS) { it.copy(fluidParticleBrightness = f(0.6f, 1.6f)) }
        r(ParamKeys.SHADING_EMBOSSED_INK) { it.copy(fluidShading = chance(0.7f)) }
        r(ParamKeys.GLOW_FLUID) { it.copy(fluidBloom = chance(0.8f)) }
        r(ParamKeys.FLUID_GLOW) { it.copy(fluidBloomIntensity = f(0.4f, 1.4f)) }
        r(ParamKeys.GLOW_THRESHOLD) { it.copy(fluidBloomThreshold = f(0.4f, 0.8f)) }
        r(ParamKeys.SUNRAYS) { it.copy(fluidSunrays = chance(0.7f)) }
        r(ParamKeys.SUNRAYS_WEIGHT) { it.copy(fluidSunraysWeight = f(0.4f, 1f)) }

        r(ParamKeys.CURL_FROM_MIDS) { it.copy(fluidCurlAudio = f(0.1f, 0.9f)) }
        r(ParamKeys.GLOW_FROM_LOUDNESS) { it.copy(fluidBloomAudio = f(0.1f, 0.9f)) }
        r(ParamKeys.FADE_WHEN_QUIET) { it.copy(fluidFadeAudio = f(0.2f, 0.9f)) }
        r(ParamKeys.RADIUS_ON_BEAT) { it.copy(fluidRadiusPulse = f(0f, 0.8f)) }

        r(ParamKeys.FLOW_STRENGTH) { it.copy(flowStrength = f(0.1f, 0.7f)) }
        r(ParamKeys.FLOW_FORCE) { it.copy(flowForce = f(0.4f, 2f)) }
        r(ParamKeys.FLOW_CURL) { it.copy(flowCurl = f(5f, 40f)) }

        r(ParamKeys.WAVE_SPEED) { it.copy(waterWaveSpeed = f(0.5f, 1.6f)) }
        r(ParamKeys.DAMPING) { it.copy(waterDamping = f(0.96f, 0.995f)) }
        r(ParamKeys.RIPPLE_STRENGTH) { it.copy(waterRippleStrength = f(0.5f, 1.6f)) }
        r(ParamKeys.DEPTH) { it.copy(waterDepth = f(0.3f, 0.9f)) }
        r(ParamKeys.SPECULAR) { it.copy(waterSpecular = f(0.3f, 0.9f)) }
        r(ParamKeys.FLOW_DRIFT) { it.copy(waterFlow = f(0.05f, 0.6f)) }
        r(ParamKeys.LIQUID) { it.copy(waterLiquid = f(0.4f, 1f)) }
        r(ParamKeys.LIQUID_FLOW) { it.copy(waterLiquidFlow = f(0.6f, 2.6f)) }
        r(ParamKeys.LIQUID_FADE) { it.copy(waterLiquidFade = f(0.1f, 1.2f)) }
        r(ParamKeys.RIPPLE_OVERLAY_STRENGTH) { it.copy(rippleOverlayStrength = f(0.15f, 0.7f)) }
        r(ParamKeys.RIPPLE_GLINT) { it.copy(rippleOverlaySpecular = f(0.1f, 0.6f)) }

        section(CustomizeTab.CYMATICS)
        r(ParamKeys.GEOMETRY) { it.copy(cymaticsGeometry = rng.nextInt(SceneParams.CYMATICS_GEOMETRIES.size)) }
        r(ParamKeys.FUNDAMENTAL_HZ) { it.copy(cymaticsFundamental = f(55f, 260f)) }
        r(ParamKeys.STANDING_WAVES) { it.copy(cymaticsModes = n(2, CymaticsMath.MAX_RENDERED_MODES)) }
        r(ParamKeys.TONAL_FOCUS) { it.copy(cymaticsFocus = f(0.35f, 1f)) }
        r(ParamKeys.PLATE_RING) { it.copy(cymaticsRing = f(0.15f, 0.8f)) }
        r(ParamKeys.FIELD_SCALE) { it.copy(cymaticsScale = f(1.6f, 5.5f)) }
        r(ParamKeys.WAVE_FLOW) { it.copy(cymaticsFlow = f(0.05f, 0.8f)) }
        r(ParamKeys.FIELD_SWIRL) { it.copy(cymaticsSwirl = f(-0.3f, 0.3f)) }
        r(ParamKeys.FILL) { it.copy(cymaticsFill = f(0.05f, 1f)) }
        r(ParamKeys.NODAL_LINES) { it.copy(cymaticsLine = f(0.4f, 1.7f)) }
        r(ParamKeys.NODAL_GLOW) { it.copy(cymaticsGlow = f(0.3f, 1.7f)) }
        r(ParamKeys.IRIDESCENCE) { it.copy(cymaticsIridescence = f(0f, 1f)) }
        r(ParamKeys.CAUSTIC_SHEEN) { it.copy(cymaticsCaustic = f(0.2f, 1.3f)) }

        return s
    }
}
