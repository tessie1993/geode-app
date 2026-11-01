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
            }.sortedBy { it.name }
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
                .put("formDrive", p.params.formDrive.toDouble())
                .put("motionAmount", p.params.motionAmount.toDouble())
                .put("motionBreath", p.params.motionBreath.toDouble())
                .put("motionOrbit", p.params.motionOrbit.toDouble())
                .put("motionDrift", p.params.motionDrift.toDouble())
                .put("motionHue", p.params.motionHue.toDouble())
                .put("camFly", p.params.camFly.toDouble())
                .put("camOrbit", p.params.camOrbit.toDouble())
                .put("camPush", p.params.camPush.toDouble())
                .put("camYaw", p.params.camYaw.toDouble())
                .put("camPitch", p.params.camPitch.toDouble())
                .put("camRoll", p.params.camRoll.toDouble())
                .put("camFov", p.params.camFov.toDouble())
                .put("camShake", p.params.camShake.toDouble())
                .put("camSmooth", p.params.camSmooth.toDouble())
                .put("reactAmount", p.params.reactAmount.toDouble())
                .put("camPath", p.params.camPath)
                .put("reactSeed", p.params.reactSeed)
                .put("camTouch", p.params.camTouch)
                .put("camGyro", p.params.camGyro)
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

        /**
         * The envelope ranges the Customize sliders offer (attack 5 ms to 1 s, decay 10 ms to 1.5 s).
         * Every preset the app saves sits inside them; a shared link can say anything.
         */
        val ATTACK_RANGE: ClosedFloatingPointRange<Float> = 0.005f..1f
        val DECAY_RANGE: ClosedFloatingPointRange<Float> = 0.01f..1.5f

        /**
         * [raw] as a Float, or [fallback] when it is not a finite one.
         *
         * Presets and templates arrive from `geode://` links anything can send, and their numbers
         * are read straight into the renderer and back out through `JSONObject.put`. A NaN or an
         * infinity (`NaN` and `Infinity` read as numbers, and `1e39` is a finite Double that
         * overflows to infinity as a Float) would poison the frame through every `clamp`, and
         * `put` throws on one, which crashed the link's confirm dialog and the save after import.
         * Checking after the narrowing catches all three; the field's default stands in, as if
         * the key were absent.
         */
        internal fun finiteOr(
            raw: Double,
            fallback: Float,
        ): Float {
            val narrowed = raw.toFloat()
            return if (narrowed.isFinite()) narrowed else fallback
        }

        /** [finiteOr], clamped into [range]; a non-finite value falls back to the range's low end. */
        internal fun finiteIn(
            raw: Double,
            range: ClosedFloatingPointRange<Float>,
        ): Float = finiteOr(raw, range.start).coerceIn(range)

        private fun JSONObject.optFinite(
            key: String,
            fallback: Double,
        ): Float = finiteOr(optDouble(key, fallback), fallback.toFloat())

        internal fun fromJson(json: String): Preset {
            val o = JSONObject(json)
            return Preset(
                name = o.getString("name"),
                sceneId = o.getString("sceneId"),
                attack = finiteIn(o.getDouble("attack"), ATTACK_RANGE),
                decay = finiteIn(o.getDouble("decay"), DECAY_RANGE),
                customShader = if (o.has("customShader")) o.getString("customShader") else null,
                milkPreset = if (o.has("milkPreset")) o.getString("milkPreset") else null,
                params =
                    SceneParams(
                        speed = o.optFinite("speed", 1.0),
                        zoom = o.optFinite("zoom", 1.0),
                        rotation = o.optFinite("rotation", 0.0),
                        endlessZoom = o.optBoolean("endlessZoom", false),
                        endlessZoomSpeed = o.optFinite("endlessZoomSpeed", 0.3),
                        sway = o.optFinite("sway", 0.0),
                        pulse = o.optFinite("pulse", 0.0),
                        driftX = o.optFinite("driftX", 0.0),
                        driftY = o.optFinite("driftY", 0.0),
                        shake = o.optFinite("shake", 0.0),
                        audioDrive = o.optFinite("audioDrive", 1.0),
                        beatResponse = o.optFinite("beatResponse", 1.0),
                        turbulence = o.optFinite("turbulence", 0.0),
                        density = o.optFinite("density", 1.0),
                        marchDetail = o.optFinite("marchDetail", 1.0),
                        trails = o.optBoolean("trails", false),
                        trailLength = o.optFinite("trailLength", 0.5),
                        trailZoom = o.optFinite("trailZoom", 0.0),
                        trailWarp = o.optFinite("trailWarp", 0.0),
                        mirror = o.optBoolean("mirror", false),
                        warp = o.optFinite("warp", 0.0),
                        ripple = o.optFinite("ripple", 0.0),
                        // Fallback must match SceneParams' own default, not an arbitrary literal,
                        // so an old file missing this key round-trips to the same rendered scene.
                        symmetry = o.optInt("symmetry", SceneParams.DEFAULT.symmetry),
                        kaleidoscope = o.optBoolean("kaleidoscope", false),
                        morph = o.optFinite("morph", 0.0),
                        pixelate = o.optFinite("pixelate", 0.0),
                        posterize = o.optFinite("posterize", 0.0),
                        particleShape = o.optInt("particleShape", 0),
                        particleSize = o.optFinite("particleSize", 1.0),
                        tile = o.optFinite("tile", 1.0),
                        twist = o.optFinite("twist", 0.0),
                        palette = o.optInt("palette", 0),
                        palette2 = o.optInt("palette2", 1),
                        paletteMix = o.optFinite("paletteMix", 0.0),
                        paletteBaseOverride =
                            o.optFinite("paletteBaseOverride", SceneParams.UNSET_OVERRIDE.toDouble()),
                        paletteRangeOverride =
                            o.optFinite("paletteRangeOverride", SceneParams.UNSET_OVERRIDE.toDouble()),
                        palette2BaseOverride =
                            o.optFinite("palette2BaseOverride", SceneParams.UNSET_OVERRIDE.toDouble()),
                        palette2RangeOverride =
                            o.optFinite("palette2RangeOverride", SceneParams.UNSET_OVERRIDE.toDouble()),
                        customPaletteId = o.optString("customPaletteId", SceneParams.NO_CUSTOM_PALETTE),
                        customPalette2Id = o.optString("customPalette2Id", SceneParams.NO_CUSTOM_PALETTE),
                        milkdropPaletteTint = o.optFinite("milkdropPaletteTint", 0.0),
                        milkdropBlendPresets = o.optBoolean("milkdropBlendPresets", false),
                        colorShift = o.optFinite("colorShift", 0.0),
                        hueRange = o.optFinite("hueRange", 1.0),
                        saturation = o.optFinite("saturation", 1.0),
                        brightness = o.optFinite("brightness", 1.0),
                        contrast = o.optFinite("contrast", 1.0),
                        gamma = o.optFinite("gamma", 1.0),
                        colorCycle = o.optBoolean("colorCycle", false),
                        cycleSpeed = o.optFinite("cycleSpeed", 0.1),
                        invert = o.optBoolean("invert", false),
                        intensity = o.optFinite("intensity", 1.0),
                        duotone = o.optBoolean("duotone", false),
                        bloom = o.optFinite("bloom", 0.0),
                        temperature = o.optFinite("temperature", 0.0),
                        solarize = o.optBoolean("solarize", false),
                        bassGain = o.optFinite("bassGain", 1.0),
                        midGain = o.optFinite("midGain", 1.0),
                        trebGain = o.optFinite("trebGain", 1.0),
                        flash = o.optFinite("flash", 0.0),
                        chromaAb = o.optFinite("chromaAb", 0.0),
                        vignette = o.optFinite("vignette", 0.0),
                        scanlines = o.optFinite("scanlines", 0.0),
                        grain = o.optFinite("grain", 0.0),
                        glitch = o.optFinite("glitch", 0.0),
                        fisheye = o.optFinite("fisheye", 0.0),
                        strobe = o.optFinite("strobe", 0.0),
                        paramFadeSec = o.optFinite("paramFadeSec", 0.0),
                        fluidQuality = o.optInt("fluidQuality", 2),
                        fluidAutoQuality = o.optBoolean("fluidAutoQuality", true),
                        fluidIterations = o.optInt("fluidIterations", 20),
                        fluidPressure = o.optFinite("fluidPressure", 0.8),
                        fluidCurl = o.optFinite("fluidCurl", 30.0),
                        fluidVelocityDissipation = o.optFinite("fluidVelocityDissipation", 0.2),
                        fluidDensityDissipation = o.optFinite("fluidDensityDissipation", 1.0),
                        fluidChromaticAging = o.optFinite("fluidChromaticAging", 0.3),
                        fluidSplatRadius = o.optFinite("fluidSplatRadius", 0.12),
                        fluidSplatForce = o.optFinite("fluidSplatForce", 1.0),
                        fluidBeatPattern = o.optInt("fluidBeatPattern", 1),
                        fluidBeatSplats = o.optInt("fluidBeatSplats", 3),
                        fluidStirrers = o.optInt("fluidStirrers", 2),
                        fluidStirrerSpeed = o.optFinite("fluidStirrerSpeed", 1.0),
                        fluidBassPump = o.optBoolean("fluidBassPump", false),
                        fluidPaletteCycleSpeed = o.optFinite("fluidPaletteCycleSpeed", 0.5),
                        fluidParticlesEnabled = o.optBoolean("fluidParticlesEnabled", true),
                        fluidParticleDrag = o.optFinite("fluidParticleDrag", 0.5),
                        fluidParticleBrightness = o.optFinite("fluidParticleBrightness", 1.0),
                        fluidDyeEnabled = o.optBoolean("fluidDyeEnabled", true),
                        fluidShading = o.optBoolean("fluidShading", true),
                        fluidBloom = o.optBoolean("fluidBloom", true),
                        fluidBloomIntensity = o.optFinite("fluidBloomIntensity", 0.8),
                        fluidBloomThreshold = o.optFinite("fluidBloomThreshold", 0.6),
                        fluidSunrays = o.optBoolean("fluidSunrays", true),
                        fluidSunraysWeight = o.optFinite("fluidSunraysWeight", 1.0),
                        fluidCurlAudio = o.optFinite("fluidCurlAudio", 0.5),
                        fluidBloomAudio = o.optFinite("fluidBloomAudio", 0.5),
                        fluidFadeAudio = o.optFinite("fluidFadeAudio", 0.6),
                        fluidRadiusPulse = o.optFinite("fluidRadiusPulse", 0.4),
                        fluidSparkle = o.optBoolean("fluidSparkle", true),
                        fluidSpawnPath = o.optInt("fluidSpawnPath", 1),
                        fluidSpawnPoints = o.optInt("fluidSpawnPoints", 3),
                        fluidSpawnProgress = o.optFinite("fluidSpawnProgress", 1.0),
                        // Fallback must match SceneParams' own default (see symmetry above).
                        fluidCatchPoints = o.optInt("fluidCatchPoints", SceneParams.DEFAULT.fluidCatchPoints),
                        fluidCatchPull = o.optFinite("fluidCatchPull", 1.0),
                        fluidCatchRadius = o.optFinite("fluidCatchRadius", 0.12),
                        // Fallback must match SceneParams' own default (see symmetry above).
                        fluidParticleLife =
                            o.optFinite("fluidParticleLife", SceneParams.DEFAULT.fluidParticleLife.toDouble()),
                        flowEnabled = o.optBoolean("flowEnabled", false),
                        flowStrength = o.optFinite("flowStrength", 0.35),
                        flowForce = o.optFinite("flowForce", 1.0),
                        flowCurl = o.optFinite("flowCurl", 25.0),
                        waterWaveSpeed = o.optFinite("waterWaveSpeed", 1.0),
                        waterDamping = o.optFinite("waterDamping", 0.985),
                        waterRippleStrength = o.optFinite("waterRippleStrength", 1.0),
                        waterDepth = o.optFinite("waterDepth", 0.6),
                        waterSpecular = o.optFinite("waterSpecular", 0.7),
                        waterFlow = o.optFinite("waterFlow", 0.3),
                        waterLiquid = o.optFinite("waterLiquid", 0.85),
                        waterLiquidFlow = o.optFinite("waterLiquidFlow", 1.4),
                        waterLiquidFade = o.optFinite("waterLiquidFade", 0.35),
                        paletteLut = o.optInt("paletteLut", SceneParams.NO_PALETTE_LUT),
                        cymaticsGeometry = o.optInt("cymaticsGeometry", 0),
                        cymaticsFundamental = o.optFinite("cymaticsFundamental", 110.0),
                        cymaticsModes = o.optInt("cymaticsModes", 5),
                        cymaticsRing = o.optFinite("cymaticsRing", 0.35),
                        cymaticsFocus = o.optFinite("cymaticsFocus", 0.7),
                        cymaticsScale = o.optFinite("cymaticsScale", 3.2),
                        cymaticsFill = o.optFinite("cymaticsFill", 0.45),
                        cymaticsLine = o.optFinite("cymaticsLine", 1.0),
                        cymaticsGlow = o.optFinite("cymaticsGlow", 1.0),
                        cymaticsIridescence = o.optFinite("cymaticsIridescence", 0.5),
                        cymaticsCaustic = o.optFinite("cymaticsCaustic", 0.8),
                        cymaticsFlow = o.optFinite("cymaticsFlow", 0.35),
                        cymaticsSwirl = o.optFinite("cymaticsSwirl", 0.05),
                        rippleOverlayEnabled = o.optBoolean("rippleOverlayEnabled", false),
                        rippleOverlayStrength = o.optFinite("rippleOverlayStrength", 0.4),
                        rippleOverlaySpecular = o.optFinite("rippleOverlaySpecular", 0.3),
                        formDrive = o.optFinite("formDrive", 0.7),
                        motionAmount = o.optFinite("motionAmount", 0.7),
                        motionBreath = o.optFinite("motionBreath", 0.5),
                        motionOrbit = o.optFinite("motionOrbit", 0.5),
                        motionDrift = o.optFinite("motionDrift", 0.5),
                        motionHue = o.optFinite("motionHue", 0.4),
                        camFly = o.optFinite("camFly", SceneParams.DEFAULT.camFly.toDouble()),
                        camOrbit = o.optFinite("camOrbit", SceneParams.DEFAULT.camOrbit.toDouble()),
                        camPush = o.optFinite("camPush", SceneParams.DEFAULT.camPush.toDouble()),
                        camYaw = o.optFinite("camYaw", SceneParams.DEFAULT.camYaw.toDouble()),
                        camPitch = o.optFinite("camPitch", SceneParams.DEFAULT.camPitch.toDouble()),
                        camRoll = o.optFinite("camRoll", SceneParams.DEFAULT.camRoll.toDouble()),
                        camFov = o.optFinite("camFov", SceneParams.DEFAULT.camFov.toDouble()),
                        camShake = o.optFinite("camShake", SceneParams.DEFAULT.camShake.toDouble()),
                        camSmooth = o.optFinite("camSmooth", SceneParams.DEFAULT.camSmooth.toDouble()),
                        reactAmount = o.optFinite("reactAmount", SceneParams.DEFAULT.reactAmount.toDouble()),
                        camPath = o.optInt("camPath", SceneParams.DEFAULT.camPath),
                        reactSeed = o.optInt("reactSeed", SceneParams.DEFAULT.reactSeed),
                        camTouch = o.optBoolean("camTouch", SceneParams.DEFAULT.camTouch),
                        camGyro = o.optBoolean("camGyro", SceneParams.DEFAULT.camGyro),
                    ),
            )
        }
    }
}
