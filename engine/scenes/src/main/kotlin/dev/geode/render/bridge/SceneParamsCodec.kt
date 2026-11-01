package dev.geode.render.bridge

import dev.geode.render.scene.SceneParams

/** Packs [SceneParams] in `geode_viz_param_name` order; [verify] proves that order against the library once. */
object SceneParamsCodec {
    const val FIELDS = 153

    val NAMES: List<String> =
        listOf(
            "speed",
            "zoom",
            "rotation",
            "endlessZoom",
            "endlessZoomSpeed",
            "sway",
            "pulse",
            "driftX",
            "driftY",
            "shake",
            "audioDrive",
            "beatResponse",
            "turbulence",
            "density",
            "marchDetail",
            "trails",
            "trailLength",
            "trailZoom",
            "trailWarp",
            "mirror",
            "warp",
            "ripple",
            "symmetry",
            "kaleidoscope",
            "morph",
            "pixelate",
            "posterize",
            "particleShape",
            "particleSize",
            "tile",
            "twist",
            "palette",
            "palette2",
            "paletteMix",
            "paletteBaseOverride",
            "paletteRangeOverride",
            "palette2BaseOverride",
            "palette2RangeOverride",
            "paletteLut",
            "milkdropPaletteTint",
            "milkdropBlendPresets",
            "colorShift",
            "hueRange",
            "saturation",
            "brightness",
            "contrast",
            "gamma",
            "colorCycle",
            "cycleSpeed",
            "invert",
            "intensity",
            "duotone",
            "bloom",
            "temperature",
            "solarize",
            "bassGain",
            "midGain",
            "trebGain",
            "flash",
            "chromaAb",
            "vignette",
            "scanlines",
            "grain",
            "fisheye",
            "strobe",
            "paramFadeSec",
            "fluidQuality",
            "fluidAutoQuality",
            "fluidIterations",
            "fluidPressure",
            "fluidCurl",
            "fluidVelocityDissipation",
            "fluidDensityDissipation",
            "fluidChromaticAging",
            "fluidSplatRadius",
            "fluidSplatForce",
            "fluidBeatPattern",
            "fluidBeatSplats",
            "fluidStirrers",
            "fluidStirrerSpeed",
            "fluidBassPump",
            "fluidPaletteCycleSpeed",
            "fluidSparkle",
            "fluidSpawnPath",
            "fluidSpawnPoints",
            "fluidSpawnProgress",
            "fluidCatchPoints",
            "fluidCatchPull",
            "fluidCatchRadius",
            "fluidParticlesEnabled",
            "fluidParticleLife",
            "fluidParticleDrag",
            "fluidParticleBrightness",
            "fluidDyeEnabled",
            "fluidShading",
            "fluidBloom",
            "fluidBloomIntensity",
            "fluidBloomThreshold",
            "fluidSunrays",
            "fluidSunraysWeight",
            "fluidCurlAudio",
            "fluidBloomAudio",
            "fluidFadeAudio",
            "fluidRadiusPulse",
            "flowEnabled",
            "flowStrength",
            "flowForce",
            "flowCurl",
            "waterWaveSpeed",
            "waterDamping",
            "waterRippleStrength",
            "waterDepth",
            "waterSpecular",
            "waterFlow",
            "waterLiquid",
            "waterLiquidFlow",
            "waterLiquidFade",
            "cymaticsGeometry",
            "cymaticsFundamental",
            "cymaticsModes",
            "cymaticsRing",
            "cymaticsFocus",
            "cymaticsScale",
            "cymaticsFill",
            "cymaticsLine",
            "cymaticsGlow",
            "cymaticsIridescence",
            "cymaticsCaustic",
            "cymaticsFlow",
            "cymaticsSwirl",
            "rippleOverlayEnabled",
            "rippleOverlayStrength",
            "rippleOverlaySpecular",
            "formDrive",
            "motionAmount",
            "motionBreath",
            "motionOrbit",
            "motionDrift",
            "motionHue",
            "camFly",
            "camPath",
            "camOrbit",
            "camPush",
            "camYaw",
            "camPitch",
            "camRoll",
            "camFov",
            "camShake",
            "camSmooth",
            "camTouch",
            "camGyro",
            "reactAmount",
            "reactSeed",
        )

    fun verify(nativeNames: String) {
        val native = nativeNames.split('\n')
        check(native == NAMES) { "native scene parameter order differs from SceneParamsCodec: $native" }
    }

    fun pack(
        p: SceneParams,
        out: FloatArray,
    ) {
        check(out.size >= FIELDS) { "scene parameter frame needs $FIELDS floats, got ${out.size}" }
        out[0] = p.speed
        out[1] = p.zoom
        out[2] = p.rotation
        out[3] = flag(p.endlessZoom)
        out[4] = p.endlessZoomSpeed
        out[5] = p.sway
        out[6] = p.pulse
        out[7] = p.driftX
        out[8] = p.driftY
        out[9] = p.shake
        out[10] = p.audioDrive
        out[11] = p.beatResponse
        out[12] = p.turbulence
        out[13] = p.density
        out[14] = p.marchDetail
        out[15] = flag(p.trails)
        out[16] = p.trailLength
        out[17] = p.trailZoom
        out[18] = p.trailWarp
        out[19] = flag(p.mirror)
        out[20] = p.warp
        out[21] = p.ripple
        out[22] = p.symmetry.toFloat()
        out[23] = flag(p.kaleidoscope)
        out[24] = p.morph
        out[25] = p.pixelate
        out[26] = p.posterize
        out[27] = p.particleShape.toFloat()
        out[28] = p.particleSize
        out[29] = p.tile
        out[30] = p.twist
        out[31] = p.palette.toFloat()
        out[32] = p.palette2.toFloat()
        out[33] = p.paletteMix
        out[34] = p.paletteBaseOverride
        out[35] = p.paletteRangeOverride
        out[36] = p.palette2BaseOverride
        out[37] = p.palette2RangeOverride
        out[38] = p.paletteLut.toFloat()
        out[39] = p.milkdropPaletteTint
        out[40] = flag(p.milkdropBlendPresets)
        out[41] = p.colorShift
        out[42] = p.hueRange
        out[43] = p.saturation
        out[44] = p.brightness
        out[45] = p.contrast
        out[46] = p.gamma
        out[47] = flag(p.colorCycle)
        out[48] = p.cycleSpeed
        out[49] = flag(p.invert)
        out[50] = p.intensity
        out[51] = flag(p.duotone)
        out[52] = p.bloom
        out[53] = p.temperature
        out[54] = flag(p.solarize)
        out[55] = p.bassGain
        out[56] = p.midGain
        out[57] = p.trebGain
        out[58] = p.flash
        out[59] = p.chromaAb
        out[60] = p.vignette
        out[61] = p.scanlines
        out[62] = p.grain
        out[63] = p.fisheye
        out[64] = p.strobe
        out[65] = p.paramFadeSec
        out[66] = p.fluidQuality.toFloat()
        out[67] = flag(p.fluidAutoQuality)
        out[68] = p.fluidIterations.toFloat()
        out[69] = p.fluidPressure
        out[70] = p.fluidCurl
        out[71] = p.fluidVelocityDissipation
        out[72] = p.fluidDensityDissipation
        out[73] = p.fluidChromaticAging
        out[74] = p.fluidSplatRadius
        out[75] = p.fluidSplatForce
        out[76] = p.fluidBeatPattern.toFloat()
        out[77] = p.fluidBeatSplats.toFloat()
        out[78] = p.fluidStirrers.toFloat()
        out[79] = p.fluidStirrerSpeed
        out[80] = flag(p.fluidBassPump)
        out[81] = p.fluidPaletteCycleSpeed
        out[82] = flag(p.fluidSparkle)
        out[83] = p.fluidSpawnPath.toFloat()
        out[84] = p.fluidSpawnPoints.toFloat()
        out[85] = p.fluidSpawnProgress
        out[86] = p.fluidCatchPoints.toFloat()
        out[87] = p.fluidCatchPull
        out[88] = p.fluidCatchRadius
        out[89] = flag(p.fluidParticlesEnabled)
        out[90] = p.fluidParticleLife
        out[91] = p.fluidParticleDrag
        out[92] = p.fluidParticleBrightness
        out[93] = flag(p.fluidDyeEnabled)
        out[94] = flag(p.fluidShading)
        out[95] = flag(p.fluidBloom)
        out[96] = p.fluidBloomIntensity
        out[97] = p.fluidBloomThreshold
        out[98] = flag(p.fluidSunrays)
        out[99] = p.fluidSunraysWeight
        out[100] = p.fluidCurlAudio
        out[101] = p.fluidBloomAudio
        out[102] = p.fluidFadeAudio
        out[103] = p.fluidRadiusPulse
        out[104] = flag(p.flowEnabled)
        out[105] = p.flowStrength
        out[106] = p.flowForce
        out[107] = p.flowCurl
        out[108] = p.waterWaveSpeed
        out[109] = p.waterDamping
        out[110] = p.waterRippleStrength
        out[111] = p.waterDepth
        out[112] = p.waterSpecular
        out[113] = p.waterFlow
        out[114] = p.waterLiquid
        out[115] = p.waterLiquidFlow
        out[116] = p.waterLiquidFade
        out[117] = p.cymaticsGeometry.toFloat()
        out[118] = p.cymaticsFundamental
        out[119] = p.cymaticsModes.toFloat()
        out[120] = p.cymaticsRing
        out[121] = p.cymaticsFocus
        out[122] = p.cymaticsScale
        out[123] = p.cymaticsFill
        out[124] = p.cymaticsLine
        out[125] = p.cymaticsGlow
        out[126] = p.cymaticsIridescence
        out[127] = p.cymaticsCaustic
        out[128] = p.cymaticsFlow
        out[129] = p.cymaticsSwirl
        out[130] = flag(p.rippleOverlayEnabled)
        out[131] = p.rippleOverlayStrength
        out[132] = p.rippleOverlaySpecular
        out[133] = p.formDrive
        out[134] = p.motionAmount
        out[135] = p.motionBreath
        out[136] = p.motionOrbit
        out[137] = p.motionDrift
        out[138] = p.motionHue
        out[139] = p.camFly
        out[140] = p.camPath.toFloat()
        out[141] = p.camOrbit
        out[142] = p.camPush
        out[143] = p.camYaw
        out[144] = p.camPitch
        out[145] = p.camRoll
        out[146] = p.camFov
        out[147] = p.camShake
        out[148] = p.camSmooth
        out[149] = flag(p.camTouch)
        out[150] = flag(p.camGyro)
        out[151] = p.reactAmount
        out[152] = p.reactSeed.toFloat()
    }

    private fun flag(on: Boolean): Float = if (on) 1f else 0f

    /** [p] as a fresh frame. */
    fun pack(p: SceneParams): FloatArray = FloatArray(FIELDS).also { pack(p, it) }

    /**
     * The inverse of [pack]: a frame back into parameters, ints rounded and flags over 0.5. The two
     * Kotlin-only fields (the saved palette slots' ids, [SceneParams.NOT_RENDERED]) come from [base];
     * an id is dropped when the frame cleared the hue overrides it stands for, since a saved palette
     * IS its overrides (see [SceneParams.withoutCustomPalette]).
     */
    fun unpack(
        values: FloatArray,
        base: SceneParams,
    ): SceneParams {
        check(values.size >= FIELDS) { "scene parameter frame needs $FIELDS floats, got ${values.size}" }
        val out = base.copy()
        NAMES.forEachIndexed { index, name ->
            val field = FIELD_BY_NAME.getValue(name)
            val v = values[index]
            when (field.type) {
                Float::class.javaPrimitiveType -> field.setFloat(out, v)
                Int::class.javaPrimitiveType -> field.setInt(out, Math.round(v))
                Boolean::class.javaPrimitiveType -> field.setBoolean(out, v > 0.5f)
                else -> error("$name is not a wire type")
            }
        }
        var result = out
        if (!result.usesCustomPalette) result = result.copy(customPaletteId = SceneParams.NO_CUSTOM_PALETTE)
        if (!result.usesCustomPalette2) result = result.copy(customPalette2Id = SceneParams.NO_CUSTOM_PALETTE)
        return result
    }

    private val FIELD_BY_NAME: Map<String, java.lang.reflect.Field> =
        SceneParams::class.java.declaredFields
            .filter { it.name in NAMES }
            .onEach { it.isAccessible = true }
            .associateBy { it.name }
}
