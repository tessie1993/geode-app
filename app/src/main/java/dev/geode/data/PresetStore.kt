package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import dev.geode.render.scene.SceneParams
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest

data class Preset(
    val name: String,
    val sceneId: String,
    val attack: Float,
    val decay: Float,
    val customShader: String? = null,
    val params: SceneParams = SceneParams.DEFAULT,
    val milkPreset: String? = null,
)

class PresetStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "presets").apply { mkdirs() }

    private val milkDir = File(context.filesDir, "milk")

    init {
        migrateLegacyFileNames()
    }

    private fun migrateLegacyFileNames() {
        dir
            .walkTopDown()
            .filter { it.isFile && it.extension == "json" }
            .toList()
            .forEach { f ->
                val name = runCatching { fromJson(f.readText()).name }.getOrNull() ?: return@forEach
                val stem = safeFileName(name)
                if (f.nameWithoutExtension == stem) return@forEach
                val target = File(f.parentFile, "$stem.json")
                if (target.exists() || !f.renameTo(target)) return@forEach
                val milk = File(milkDir, f.nameWithoutExtension + ".milk")
                val milkTarget = File(milkDir, "$stem.milk")
                if (milk.isFile && !milkTarget.exists()) milk.renameTo(milkTarget)
            }
    }

    fun folderOf(name: String): String {
        val f = findFile(name) ?: return ""
        return f.parentFile
            ?.relativeTo(dir)
            ?.path
            .orEmpty()
    }

    @WorkerThread
    fun folders(): List<String> =
        dir
            .walkTopDown()
            .filter { it.isDirectory && it != dir }
            .map { it.relativeTo(dir).path }
            .sorted()
            .toList()

    fun addFolder(path: String) {
        File(dir, sanitize(path)).mkdirs()
    }

    fun renameFolder(
        from: String,
        to: String,
    ) {
        val src = File(dir, sanitize(from))
        if (src.isDirectory) src.renameTo(File(dir, sanitize(to)))
    }

    fun removeFolder(name: String): Boolean {
        if (name.isBlank()) return false
        val f = File(dir, sanitize(name))
        val isRoot = runCatching { f.canonicalPath == dir.canonicalPath }.getOrDefault(true)
        if (isRoot || !f.isDirectory) return false
        if (f.walkTopDown().any { it.isFile }) return false
        return f.deleteRecursively()
    }

    @WorkerThread
    fun moveToFolder(
        name: String,
        folder: String,
    ) {
        val f = findFile(name) ?: return
        val destDir = if (folder.isEmpty()) dir else File(dir, sanitize(folder)).apply { mkdirs() }
        f.renameTo(File(destDir, f.name))
    }

    fun fileOf(name: String): File? = findFile(name)

    fun milkFileOf(presetName: String): File = File(milkDir, milkFileName(presetName))

    @WorkerThread
    fun materializeMilk(
        presetName: String,
        source: String?,
    ): String? {
        val file = milkFileOf(presetName)
        if (source != null) {
            val stale = runCatching { !file.isFile || file.readText() != source }.getOrDefault(true)
            if (stale) AtomicWrite.text(file, source)
        }
        return file.takeIf { it.isFile }?.absolutePath
    }

    private fun findFile(name: String): File? =
        dir.walkTopDown().firstOrNull { it.isFile && it.extension == "json" && it.nameWithoutExtension == safeFileName(name) }

    @WorkerThread
    fun list(): List<Preset> =
        dir
            .walkTopDown()
            .filter { it.isFile && it.extension == "json" }
            .mapNotNull { f ->
                runCatching { fromJson(f.readText()) }
                    .onFailure { dev.geode.RingLog.note("PresetStore", "unreadable preset skipped: ${f.name}", it) }
                    .getOrNull()
            }
            .sortedBy { it.name }
            .toList()

    @WorkerThread
    fun save(
        preset: Preset,
        folder: String = "",
    ) {
        val destDir = if (folder.isEmpty()) dir else File(dir, sanitize(folder)).apply { mkdirs() }
        val dest = File(destDir, safeFileName(preset.name) + ".json")
        val previous = findFile(preset.name)?.takeIf { it != dest }
        if (AtomicWrite.text(dest, toJson(preset))) previous?.delete()
    }

    @WorkerThread
    fun delete(name: String) {
        findFile(name)?.delete()
    }

    companion object {
        internal fun toJson(p: Preset): String =
            JSONObject()
                .put("name", p.name)
                .put("sceneId", p.sceneId)
                .put("attack", p.attack.toDouble())
                .put("decay", p.decay.toDouble())
                .put("speed", p.params.speed.toDouble())
                .put("zoom", p.params.zoom.toDouble())
                .put("rotation", p.params.rotation.toDouble())
                .put("endlessZoom", p.params.endlessZoom)
                .put("endlessZoomSpeed", p.params.endlessZoomSpeed.toDouble())
                .put("audioDrive", p.params.audioDrive.toDouble())
                .put("beatResponse", p.params.beatResponse.toDouble())
                .put("turbulence", p.params.turbulence.toDouble())
                .put("density", p.params.density.toDouble())
                .put("marchDetail", p.params.marchDetail.toDouble())
                .put("trails", p.params.trails)
                .put("trailLength", p.params.trailLength.toDouble())
                .put("trailZoom", p.params.trailZoom.toDouble())
                .put("trailWarp", p.params.trailWarp.toDouble())
                .put("mirror", p.params.mirror)
                .put("palette", p.params.palette)
                .put("colorShift", p.params.colorShift.toDouble())
                .put("hueRange", p.params.hueRange.toDouble())
                .put("saturation", p.params.saturation.toDouble())
                .put("brightness", p.params.brightness.toDouble())
                .put("colorCycle", p.params.colorCycle)
                .put("cycleSpeed", p.params.cycleSpeed.toDouble())
                .put("invert", p.params.invert)
                .put("contrast", p.params.contrast.toDouble())
                .put("gamma", p.params.gamma.toDouble())
                .put("intensity", p.params.intensity.toDouble())
                .put("sway", p.params.sway.toDouble())
                .put("pulse", p.params.pulse.toDouble())
                .put("warp", p.params.warp.toDouble())
                .put("ripple", p.params.ripple.toDouble())
                .put("symmetry", p.params.symmetry)
                .put("kaleidoscope", p.params.kaleidoscope)
                .put("morph", p.params.morph.toDouble())
                .put("pixelate", p.params.pixelate.toDouble())
                .put("posterize", p.params.posterize.toDouble())
                .put("particleShape", p.params.particleShape)
                .put("particleSize", p.params.particleSize.toDouble())
                .put("palette2", p.params.palette2)
                .put("paletteMix", p.params.paletteMix.toDouble())
                .put("paletteBaseOverride", p.params.paletteBaseOverride.toDouble())
                .put("paletteRangeOverride", p.params.paletteRangeOverride.toDouble())
                .put("palette2BaseOverride", p.params.palette2BaseOverride.toDouble())
                .put("palette2RangeOverride", p.params.palette2RangeOverride.toDouble())
                .put("customPaletteId", p.params.customPaletteId)
                .put("customPalette2Id", p.params.customPalette2Id)
                .put("milkdropPaletteTint", p.params.milkdropPaletteTint.toDouble())
                .put("milkdropBlendPresets", p.params.milkdropBlendPresets)
                .put("duotone", p.params.duotone)
                .put("bloom", p.params.bloom.toDouble())
                .put("driftX", p.params.driftX.toDouble())
                .put("driftY", p.params.driftY.toDouble())
                .put("shake", p.params.shake.toDouble())
                .put("tile", p.params.tile.toDouble())
                .put("twist", p.params.twist.toDouble())
                .put("temperature", p.params.temperature.toDouble())
                .put("bassGain", p.params.bassGain.toDouble())
                .put("midGain", p.params.midGain.toDouble())
                .put("trebGain", p.params.trebGain.toDouble())
                .put("flash", p.params.flash.toDouble())
                .put("chromaAb", p.params.chromaAb.toDouble())
                .put("vignette", p.params.vignette.toDouble())
                .put("scanlines", p.params.scanlines.toDouble())
                .put("grain", p.params.grain.toDouble())
                .put("glitch", p.params.glitch.toDouble())
                .put("fisheye", p.params.fisheye.toDouble())
                .put("strobe", p.params.strobe.toDouble())
                .put("paramFadeSec", p.params.paramFadeSec.toDouble())
                .put("solarize", p.params.solarize)
                .put("fluidQuality", p.params.fluidQuality)
                .put("fluidAutoQuality", p.params.fluidAutoQuality)
                .put("fluidIterations", p.params.fluidIterations)
                .put("fluidPressure", p.params.fluidPressure.toDouble())
                .put("fluidCurl", p.params.fluidCurl.toDouble())
                .put("fluidVelocityDissipation", p.params.fluidVelocityDissipation.toDouble())
                .put("fluidDensityDissipation", p.params.fluidDensityDissipation.toDouble())
                .put("fluidChromaticAging", p.params.fluidChromaticAging.toDouble())
                .put("fluidSplatRadius", p.params.fluidSplatRadius.toDouble())
                .put("fluidSplatForce", p.params.fluidSplatForce.toDouble())
                .put("fluidBeatPattern", p.params.fluidBeatPattern)
                .put("fluidBeatSplats", p.params.fluidBeatSplats)
                .put("fluidStirrers", p.params.fluidStirrers)
                .put("fluidStirrerSpeed", p.params.fluidStirrerSpeed.toDouble())
                .put("fluidBassPump", p.params.fluidBassPump)
                .put("fluidPaletteCycleSpeed", p.params.fluidPaletteCycleSpeed.toDouble())
                .put("fluidParticlesEnabled", p.params.fluidParticlesEnabled)
                .put("fluidParticleDrag", p.params.fluidParticleDrag.toDouble())
                .put("fluidParticleBrightness", p.params.fluidParticleBrightness.toDouble())
                .put("fluidDyeEnabled", p.params.fluidDyeEnabled)
                .put("fluidShading", p.params.fluidShading)
                .put("fluidBloom", p.params.fluidBloom)
                .put("fluidBloomIntensity", p.params.fluidBloomIntensity.toDouble())
                .put("fluidBloomThreshold", p.params.fluidBloomThreshold.toDouble())
                .put("fluidSunrays", p.params.fluidSunrays)
                .put("fluidSunraysWeight", p.params.fluidSunraysWeight.toDouble())
                .put("fluidCurlAudio", p.params.fluidCurlAudio.toDouble())
                .put("fluidBloomAudio", p.params.fluidBloomAudio.toDouble())
                .put("fluidFadeAudio", p.params.fluidFadeAudio.toDouble())
                .put("fluidRadiusPulse", p.params.fluidRadiusPulse.toDouble())
                .put("fluidSparkle", p.params.fluidSparkle)
                .put("fluidSpawnPath", p.params.fluidSpawnPath)
                .put("fluidSpawnPoints", p.params.fluidSpawnPoints)
                .put("fluidSpawnProgress", p.params.fluidSpawnProgress.toDouble())
                .put("fluidCatchPoints", p.params.fluidCatchPoints)
                .put("fluidCatchPull", p.params.fluidCatchPull.toDouble())
                .put("fluidCatchRadius", p.params.fluidCatchRadius.toDouble())
                .put("fluidParticleLife", p.params.fluidParticleLife.toDouble())
                .put("flowEnabled", p.params.flowEnabled)
                .put("flowStrength", p.params.flowStrength.toDouble())
                .put("flowForce", p.params.flowForce.toDouble())
                .put("flowCurl", p.params.flowCurl.toDouble())
                .put("waterWaveSpeed", p.params.waterWaveSpeed.toDouble())
                .put("waterDamping", p.params.waterDamping.toDouble())
                .put("waterRippleStrength", p.params.waterRippleStrength.toDouble())
                .put("waterDepth", p.params.waterDepth.toDouble())
                .put("waterSpecular", p.params.waterSpecular.toDouble())
                .put("waterFlow", p.params.waterFlow.toDouble())
                .put("waterLiquid", p.params.waterLiquid.toDouble())
                .put("waterLiquidFlow", p.params.waterLiquidFlow.toDouble())
                .put("waterLiquidFade", p.params.waterLiquidFade.toDouble())
                .put("paletteLut", p.params.paletteLut)
                .put("beamXy", p.params.beamXy)
                .put("beamWidth", p.params.beamWidth.toDouble())
                .put("beamIntensity", p.params.beamIntensity.toDouble())
                .put("beamTail", p.params.beamTail.toDouble())
                .put("cymaticsGeometry", p.params.cymaticsGeometry)
                .put("cymaticsFundamental", p.params.cymaticsFundamental.toDouble())
                .put("cymaticsModes", p.params.cymaticsModes)
                .put("cymaticsRing", p.params.cymaticsRing.toDouble())
                .put("cymaticsFocus", p.params.cymaticsFocus.toDouble())
                .put("cymaticsScale", p.params.cymaticsScale.toDouble())
                .put("cymaticsFill", p.params.cymaticsFill.toDouble())
                .put("cymaticsLine", p.params.cymaticsLine.toDouble())
                .put("cymaticsGlow", p.params.cymaticsGlow.toDouble())
                .put("cymaticsIridescence", p.params.cymaticsIridescence.toDouble())
                .put("cymaticsCaustic", p.params.cymaticsCaustic.toDouble())
                .put("cymaticsFlow", p.params.cymaticsFlow.toDouble())
                .put("cymaticsSwirl", p.params.cymaticsSwirl.toDouble())
                .put("rippleOverlayEnabled", p.params.rippleOverlayEnabled)
                .put("rippleOverlayStrength", p.params.rippleOverlayStrength.toDouble())
                .put("rippleOverlaySpecular", p.params.rippleOverlaySpecular.toDouble())
                .apply { if (p.customShader != null) put("customShader", p.customShader) }
                .apply { if (p.milkPreset != null) put("milkPreset", p.milkPreset) }
                .toString(2)

        internal val ENVELOPE_KEYS = setOf("name", "sceneId", "attack", "decay", "customShader", "milkPreset")

        internal fun milkFileName(presetName: String): String = safeFileName(presetName.removeSuffix(".milk")) + ".milk"

        private val UNSAFE_CHARS = Regex("[^A-Za-z0-9-_ ]")

        private fun sanitize(name: String): String = name.replace(UNSAFE_CHARS, "_")

        internal fun safeFileName(name: String): String {
            val stem = name.replace(UNSAFE_CHARS, "_")
            if (stem == name) return name
            val hash =
                MessageDigest
                    .getInstance("SHA-256")
                    .digest(name.toByteArray(Charsets.UTF_8))
                    .take(4)
                    .joinToString("") { b -> "%02x".format(b.toInt() and 0xff) }
            return "$stem-$hash"
        }

        internal fun paramsToJson(params: SceneParams): JSONObject =
            JSONObject(toJson(Preset("", "", 0f, 0f, null, params)))
                .also { o -> ENVELOPE_KEYS.forEach(o::remove) }

        internal fun paramsFromJson(o: JSONObject): SceneParams {
            val full = JSONObject(o.toString())
            full.put("name", "")
            full.put("sceneId", "")
            full.put("attack", 0.0)
            full.put("decay", 0.0)
            return fromJson(full.toString()).params
        }

        internal fun fromJson(json: String): Preset {
            val o = JSONObject(json)
            return Preset(
                name = o.getString("name"),
                sceneId = o.getString("sceneId"),
                attack = o.getDouble("attack").toFloat(),
                decay = o.getDouble("decay").toFloat(),
                customShader = if (o.has("customShader")) o.getString("customShader") else null,
                milkPreset = if (o.has("milkPreset")) o.getString("milkPreset") else null,
                params =
                    SceneParams(
                        speed = o.optDouble("speed", 1.0).toFloat(),
                        zoom = o.optDouble("zoom", 1.0).toFloat(),
                        rotation = o.optDouble("rotation", 0.0).toFloat(),
                        endlessZoom = o.optBoolean("endlessZoom", false),
                        endlessZoomSpeed = o.optDouble("endlessZoomSpeed", 0.3).toFloat(),
                        sway = o.optDouble("sway", 0.0).toFloat(),
                        pulse = o.optDouble("pulse", 0.0).toFloat(),
                        driftX = o.optDouble("driftX", 0.0).toFloat(),
                        driftY = o.optDouble("driftY", 0.0).toFloat(),
                        shake = o.optDouble("shake", 0.0).toFloat(),
                        audioDrive = o.optDouble("audioDrive", 1.0).toFloat(),
                        beatResponse = o.optDouble("beatResponse", 1.0).toFloat(),
                        turbulence = o.optDouble("turbulence", 0.0).toFloat(),
                        density = o.optDouble("density", 1.0).toFloat(),
                        marchDetail = o.optDouble("marchDetail", 1.0).toFloat(),
                        trails = o.optBoolean("trails", false),
                        trailLength = o.optDouble("trailLength", 0.5).toFloat(),
                        trailZoom = o.optDouble("trailZoom", 0.0).toFloat(),
                        trailWarp = o.optDouble("trailWarp", 0.0).toFloat(),
                        mirror = o.optBoolean("mirror", false),
                        warp = o.optDouble("warp", 0.0).toFloat(),
                        ripple = o.optDouble("ripple", 0.0).toFloat(),
                        symmetry = o.optInt("symmetry", 0),
                        kaleidoscope = o.optBoolean("kaleidoscope", false),
                        morph = o.optDouble("morph", 0.0).toFloat(),
                        pixelate = o.optDouble("pixelate", 0.0).toFloat(),
                        posterize = o.optDouble("posterize", 0.0).toFloat(),
                        particleShape = o.optInt("particleShape", 0),
                        particleSize = o.optDouble("particleSize", 1.0).toFloat(),
                        tile = o.optDouble("tile", 1.0).toFloat(),
                        twist = o.optDouble("twist", 0.0).toFloat(),
                        palette = o.optInt("palette", 0),
                        palette2 = o.optInt("palette2", 1),
                        paletteMix = o.optDouble("paletteMix", 0.0).toFloat(),
                        paletteBaseOverride =
                            o.optDouble("paletteBaseOverride", SceneParams.UNSET_OVERRIDE.toDouble()).toFloat(),
                        paletteRangeOverride =
                            o.optDouble("paletteRangeOverride", SceneParams.UNSET_OVERRIDE.toDouble()).toFloat(),
                        palette2BaseOverride =
                            o.optDouble("palette2BaseOverride", SceneParams.UNSET_OVERRIDE.toDouble()).toFloat(),
                        palette2RangeOverride =
                            o.optDouble("palette2RangeOverride", SceneParams.UNSET_OVERRIDE.toDouble()).toFloat(),
                        customPaletteId = o.optString("customPaletteId", SceneParams.NO_CUSTOM_PALETTE),
                        customPalette2Id = o.optString("customPalette2Id", SceneParams.NO_CUSTOM_PALETTE),
                        milkdropPaletteTint = o.optDouble("milkdropPaletteTint", 0.0).toFloat(),
                        milkdropBlendPresets = o.optBoolean("milkdropBlendPresets", false),
                        colorShift = o.optDouble("colorShift", 0.0).toFloat(),
                        hueRange = o.optDouble("hueRange", 1.0).toFloat(),
                        saturation = o.optDouble("saturation", 1.0).toFloat(),
                        brightness = o.optDouble("brightness", 1.0).toFloat(),
                        contrast = o.optDouble("contrast", 1.0).toFloat(),
                        gamma = o.optDouble("gamma", 1.0).toFloat(),
                        colorCycle = o.optBoolean("colorCycle", false),
                        cycleSpeed = o.optDouble("cycleSpeed", 0.1).toFloat(),
                        invert = o.optBoolean("invert", false),
                        intensity = o.optDouble("intensity", 1.0).toFloat(),
                        duotone = o.optBoolean("duotone", false),
                        bloom = o.optDouble("bloom", 0.0).toFloat(),
                        temperature = o.optDouble("temperature", 0.0).toFloat(),
                        solarize = o.optBoolean("solarize", false),
                        bassGain = o.optDouble("bassGain", 1.0).toFloat(),
                        midGain = o.optDouble("midGain", 1.0).toFloat(),
                        trebGain = o.optDouble("trebGain", 1.0).toFloat(),
                        flash = o.optDouble("flash", 0.0).toFloat(),
                        chromaAb = o.optDouble("chromaAb", 0.0).toFloat(),
                        vignette = o.optDouble("vignette", 0.0).toFloat(),
                        scanlines = o.optDouble("scanlines", 0.0).toFloat(),
                        grain = o.optDouble("grain", 0.0).toFloat(),
                        glitch = o.optDouble("glitch", 0.0).toFloat(),
                        fisheye = o.optDouble("fisheye", 0.0).toFloat(),
                        strobe = o.optDouble("strobe", 0.0).toFloat(),
                        paramFadeSec = o.optDouble("paramFadeSec", 0.0).toFloat(),
                        fluidQuality = o.optInt("fluidQuality", 2),
                        fluidAutoQuality = o.optBoolean("fluidAutoQuality", true),
                        fluidIterations = o.optInt("fluidIterations", 20),
                        fluidPressure = o.optDouble("fluidPressure", 0.8).toFloat(),
                        fluidCurl = o.optDouble("fluidCurl", 30.0).toFloat(),
                        fluidVelocityDissipation = o.optDouble("fluidVelocityDissipation", 0.2).toFloat(),
                        fluidDensityDissipation = o.optDouble("fluidDensityDissipation", 1.0).toFloat(),
                        fluidChromaticAging = o.optDouble("fluidChromaticAging", 0.3).toFloat(),
                        fluidSplatRadius = o.optDouble("fluidSplatRadius", 0.12).toFloat(),
                        fluidSplatForce = o.optDouble("fluidSplatForce", 1.0).toFloat(),
                        fluidBeatPattern = o.optInt("fluidBeatPattern", 1),
                        fluidBeatSplats = o.optInt("fluidBeatSplats", 3),
                        fluidStirrers = o.optInt("fluidStirrers", 2),
                        fluidStirrerSpeed = o.optDouble("fluidStirrerSpeed", 1.0).toFloat(),
                        fluidBassPump = o.optBoolean("fluidBassPump", false),
                        fluidPaletteCycleSpeed = o.optDouble("fluidPaletteCycleSpeed", 0.5).toFloat(),
                        fluidParticlesEnabled = o.optBoolean("fluidParticlesEnabled", true),
                        fluidParticleDrag = o.optDouble("fluidParticleDrag", 0.5).toFloat(),
                        fluidParticleBrightness = o.optDouble("fluidParticleBrightness", 1.0).toFloat(),
                        fluidDyeEnabled = o.optBoolean("fluidDyeEnabled", true),
                        fluidShading = o.optBoolean("fluidShading", true),
                        fluidBloom = o.optBoolean("fluidBloom", true),
                        fluidBloomIntensity = o.optDouble("fluidBloomIntensity", 0.8).toFloat(),
                        fluidBloomThreshold = o.optDouble("fluidBloomThreshold", 0.6).toFloat(),
                        fluidSunrays = o.optBoolean("fluidSunrays", true),
                        fluidSunraysWeight = o.optDouble("fluidSunraysWeight", 1.0).toFloat(),
                        fluidCurlAudio = o.optDouble("fluidCurlAudio", 0.5).toFloat(),
                        fluidBloomAudio = o.optDouble("fluidBloomAudio", 0.5).toFloat(),
                        fluidFadeAudio = o.optDouble("fluidFadeAudio", 0.6).toFloat(),
                        fluidRadiusPulse = o.optDouble("fluidRadiusPulse", 0.4).toFloat(),
                        fluidSparkle = o.optBoolean("fluidSparkle", true),
                        fluidSpawnPath = o.optInt("fluidSpawnPath", 1),
                        fluidSpawnPoints = o.optInt("fluidSpawnPoints", 3),
                        fluidSpawnProgress = o.optDouble("fluidSpawnProgress", 1.0).toFloat(),
                        fluidCatchPoints = o.optInt("fluidCatchPoints", 0),
                        fluidCatchPull = o.optDouble("fluidCatchPull", 1.0).toFloat(),
                        fluidCatchRadius = o.optDouble("fluidCatchRadius", 0.12).toFloat(),
                        fluidParticleLife = o.optDouble("fluidParticleLife", 12.0).toFloat(),
                        flowEnabled = o.optBoolean("flowEnabled", false),
                        flowStrength = o.optDouble("flowStrength", 0.35).toFloat(),
                        flowForce = o.optDouble("flowForce", 1.0).toFloat(),
                        flowCurl = o.optDouble("flowCurl", 25.0).toFloat(),
                        waterWaveSpeed = o.optDouble("waterWaveSpeed", 1.0).toFloat(),
                        waterDamping = o.optDouble("waterDamping", 0.985).toFloat(),
                        waterRippleStrength = o.optDouble("waterRippleStrength", 1.0).toFloat(),
                        waterDepth = o.optDouble("waterDepth", 0.6).toFloat(),
                        waterSpecular = o.optDouble("waterSpecular", 0.7).toFloat(),
                        waterFlow = o.optDouble("waterFlow", 0.3).toFloat(),
                        waterLiquid = o.optDouble("waterLiquid", 0.85).toFloat(),
                        waterLiquidFlow = o.optDouble("waterLiquidFlow", 1.4).toFloat(),
                        waterLiquidFade = o.optDouble("waterLiquidFade", 0.35).toFloat(),
                        paletteLut = o.optInt("paletteLut", SceneParams.NO_PALETTE_LUT),
                        beamXy = o.optBoolean("beamXy", false),
                        beamWidth = o.optDouble("beamWidth", 1.0).toFloat(),
                        beamIntensity = o.optDouble("beamIntensity", 1.0).toFloat(),
                        beamTail = o.optDouble("beamTail", 0.35).toFloat(),
                        cymaticsGeometry = o.optInt("cymaticsGeometry", 0),
                        cymaticsFundamental = o.optDouble("cymaticsFundamental", 110.0).toFloat(),
                        cymaticsModes = o.optInt("cymaticsModes", 5),
                        cymaticsRing = o.optDouble("cymaticsRing", 0.35).toFloat(),
                        cymaticsFocus = o.optDouble("cymaticsFocus", 0.7).toFloat(),
                        cymaticsScale = o.optDouble("cymaticsScale", 3.2).toFloat(),
                        cymaticsFill = o.optDouble("cymaticsFill", 0.45).toFloat(),
                        cymaticsLine = o.optDouble("cymaticsLine", 1.0).toFloat(),
                        cymaticsGlow = o.optDouble("cymaticsGlow", 1.0).toFloat(),
                        cymaticsIridescence = o.optDouble("cymaticsIridescence", 0.5).toFloat(),
                        cymaticsCaustic = o.optDouble("cymaticsCaustic", 0.8).toFloat(),
                        cymaticsFlow = o.optDouble("cymaticsFlow", 0.35).toFloat(),
                        cymaticsSwirl = o.optDouble("cymaticsSwirl", 0.05).toFloat(),
                        rippleOverlayEnabled = o.optBoolean("rippleOverlayEnabled", false),
                        rippleOverlayStrength = o.optDouble("rippleOverlayStrength", 0.4).toFloat(),
                        rippleOverlaySpecular = o.optDouble("rippleOverlaySpecular", 0.3).toFloat(),
                    ),
            )
        }
    }
}
