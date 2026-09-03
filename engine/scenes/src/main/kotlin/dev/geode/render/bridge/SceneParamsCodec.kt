package dev.geode.render.bridge

import dev.geode.render.scene.SceneParams

/** Packs [SceneParams] in `geode_viz_param_name` order; [verify] proves that order against the library once. */
object SceneParamsCodec {
    const val FIELDS = 134

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
            "glitch",
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
        out[63] = p.glitch
        out[64] = p.fisheye
        out[65] = p.strobe
        out[66] = p.paramFadeSec
        out[67] = p.fluidQuality.toFloat()
        out[68] = flag(p.fluidAutoQuality)
        out[69] = p.fluidIterations.toFloat()
        out[70] = p.fluidPressure
        out[71] = p.fluidCurl
        out[72] = p.fluidVelocityDissipation
        out[73] = p.fluidDensityDissipation
        out[74] = p.fluidChromaticAging
        out[75] = p.fluidSplatRadius
        out[76] = p.fluidSplatForce
        out[77] = p.fluidBeatPattern.toFloat()
        out[78] = p.fluidBeatSplats.toFloat()
        out[79] = p.fluidStirrers.toFloat()
        out[80] = p.fluidStirrerSpeed
        out[81] = flag(p.fluidBassPump)
        out[82] = p.fluidPaletteCycleSpeed
        out[83] = flag(p.fluidSparkle)
        out[84] = p.fluidSpawnPath.toFloat()
        out[85] = p.fluidSpawnPoints.toFloat()
        out[86] = p.fluidSpawnProgress
        out[87] = p.fluidCatchPoints.toFloat()
        out[88] = p.fluidCatchPull
        out[89] = p.fluidCatchRadius
        out[90] = flag(p.fluidParticlesEnabled)
        out[91] = p.fluidParticleLife
        out[92] = p.fluidParticleDrag
        out[93] = p.fluidParticleBrightness
        out[94] = flag(p.fluidDyeEnabled)
        out[95] = flag(p.fluidShading)
        out[96] = flag(p.fluidBloom)
        out[97] = p.fluidBloomIntensity
        out[98] = p.fluidBloomThreshold
        out[99] = flag(p.fluidSunrays)
        out[100] = p.fluidSunraysWeight
        out[101] = p.fluidCurlAudio
        out[102] = p.fluidBloomAudio
        out[103] = p.fluidFadeAudio
        out[104] = p.fluidRadiusPulse
        out[105] = flag(p.flowEnabled)
        out[106] = p.flowStrength
        out[107] = p.flowForce
        out[108] = p.flowCurl
        out[109] = p.waterWaveSpeed
        out[110] = p.waterDamping
        out[111] = p.waterRippleStrength
        out[112] = p.waterDepth
        out[113] = p.waterSpecular
        out[114] = p.waterFlow
        out[115] = p.waterLiquid
        out[116] = p.waterLiquidFlow
        out[117] = p.waterLiquidFade
        out[118] = p.cymaticsGeometry.toFloat()
        out[119] = p.cymaticsFundamental
        out[120] = p.cymaticsModes.toFloat()
        out[121] = p.cymaticsRing
        out[122] = p.cymaticsFocus
        out[123] = p.cymaticsScale
        out[124] = p.cymaticsFill
        out[125] = p.cymaticsLine
        out[126] = p.cymaticsGlow
        out[127] = p.cymaticsIridescence
        out[128] = p.cymaticsCaustic
        out[129] = p.cymaticsFlow
        out[130] = p.cymaticsSwirl
        out[131] = flag(p.rippleOverlayEnabled)
        out[132] = p.rippleOverlayStrength
        out[133] = p.rippleOverlaySpecular
    }

    private fun flag(on: Boolean): Float = if (on) 1f else 0f
}
