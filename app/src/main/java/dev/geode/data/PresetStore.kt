package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.ParamSpec
import dev.geode.render.scene.SceneParams
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import kotlin.math.roundToInt

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

    private var migrated = false

    /**
     * Renames files saved under older name rules, once, before the first read or write of the
     * folder. It walks the tree and parses every preset, so it runs on whichever thread first uses
     * the store (the session's first use is its IO listing at start) and not in the constructor,
     * which the session runs on the main thread.
     */
    @Synchronized
    private fun ensureMigrated() {
        if (migrated) return
        migrated = true
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
        ensureMigrated()
        val f = findFile(name) ?: return ""
        return f.parentFile
            ?.relativeTo(dir)
            ?.path
            .orEmpty()
    }

    @WorkerThread
    fun folders(): List<String> {
        ensureMigrated()
        return dir
            .walkTopDown()
            .filter { it.isDirectory && it != dir }
            .map { it.relativeTo(dir).path }
            .sorted()
            .toList()
    }

    fun addFolder(path: String) {
        ensureMigrated()
        File(dir, sanitize(path)).mkdirs()
    }

    fun renameFolder(
        from: String,
        to: String,
    ) {
        ensureMigrated()
        val src = File(dir, sanitize(from))
        if (src.isDirectory) src.renameTo(File(dir, sanitize(to)))
    }

    @WorkerThread
    fun moveToFolder(
        name: String,
        folder: String,
    ) {
        ensureMigrated()
        val f = findFile(name) ?: return
        val destDir = if (folder.isEmpty()) dir else File(dir, sanitize(folder)).apply { mkdirs() }
        f.renameTo(File(destDir, f.name))
    }

    fun fileOf(name: String): File? {
        ensureMigrated()
        return findFile(name)
    }

    fun milkFileOf(presetName: String): File = File(milkDir, milkFileName(presetName))

    @WorkerThread
    fun materializeMilk(
        presetName: String,
        source: String?,
    ): String? {
        ensureMigrated()
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
    fun list(): List<Preset> {
        ensureMigrated()
        return dir
            .walkTopDown()
            .filter { it.isFile && it.extension == "json" }
            .mapNotNull { f ->
                runCatching { fromJson(f.readText()) }
                    .onFailure { dev.geode.RingLog.note("PresetStore", "unreadable preset skipped: ${f.name}", it) }
                    .getOrNull()
            }.sortedBy { it.name }
            .toList()
    }

    @WorkerThread
    fun save(
        preset: Preset,
        folder: String = "",
    ) {
        ensureMigrated()
        val destDir = if (folder.isEmpty()) dir else File(dir, sanitize(folder)).apply { mkdirs() }
        val dest = File(destDir, safeFileName(preset.name) + ".json")
        val previous = findFile(preset.name)?.takeIf { it != dest }
        if (AtomicWrite.text(dest, toJson(preset))) previous?.delete()
    }

    @WorkerThread
    fun delete(name: String) {
        ensureMigrated()
        findFile(name)?.delete()
    }

    companion object {
        internal fun toJson(p: Preset): String =
            JSONObject()
                .put("name", p.name)
                .put("sceneId", p.sceneId)
                .put("attack", p.attack.toDouble())
                .put("decay", p.decay.toDouble())
                .also { putParams(it, p.params) }
                .apply { if (p.customShader != null) put("customShader", p.customShader) }
                .apply { if (p.milkPreset != null) put("milkPreset", p.milkPreset) }
                .toString(2)

        internal val ENVELOPE_KEYS = setOf("name", "sceneId", "attack", "decay", "customShader", "milkPreset")

        internal fun milkFileName(presetName: String): String = safeFileName(presetName.removeSuffix(".milk")) + ".milk"

        private val UNSAFE_CHARS = Regex("[^A-Za-z0-9-_ ]")

        private fun sanitize(name: String): String = name.replace(UNSAFE_CHARS, "_")

        internal fun safeFileName(name: String): String {
            val stem = name.replace(UNSAFE_CHARS, "_")
            // A blank name is not a file name: File(dir, "") is dir itself, so a project whose id is
            // empty (a restored backup, an older app) resolved to the projects root, and deleting it
            // removed every project and every take.
            if (stem == name && name.isNotBlank()) return name
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

        internal fun paramsFromJson(o: JSONObject): SceneParams = readParams(o)

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

        /**
         * The two Kotlin-only fields ([SceneParams.NOT_RENDERED]): the saved palette slots' ids. They
         * are not schema rows, so they travel by hand, under their own names, as strings.
         */
        private const val KEY_CUSTOM_PALETTE_ID = "customPaletteId"
        private const val KEY_CUSTOM_PALETTE2_ID = "customPalette2Id"

        /**
         * Writes [params] into [o] by the native params schema ([ParamSchema], core/viz/ParamTable.inc):
         * every row under its name, a flag as a JSON boolean, a picker as an int and every float as a
         * number, from the wire frame [SceneParamsCodec.pack] gives. The names are the ones the format
         * always used (the table's names are the preset keys), so a file written now holds the same
         * keys and values as one written before the schema.
         */
        private fun putParams(
            o: JSONObject,
            params: SceneParams,
        ) {
            val frame = SceneParamsCodec.pack(params)
            for (spec in ParamSchema.params) {
                if (spec.retired) continue
                val v = frame[spec.slot]
                when (spec.kind) {
                    ParamSpec.Kind.BOOL -> o.put(spec.name, v > 0.5f)
                    ParamSpec.Kind.INT -> o.put(spec.name, v.roundToInt())
                    ParamSpec.Kind.FLOAT, ParamSpec.Kind.CIRCULAR -> o.put(spec.name, v.toDouble())
                }
            }
            o.put(KEY_CUSTOM_PALETTE_ID, params.customPaletteId)
            o.put(KEY_CUSTOM_PALETTE2_ID, params.customPalette2Id)
        }

        /**
         * Reads scene params from [o] by the schema: each row under its name, its default when the key
         * is missing or not a finite number ([finiteOr]), then held to the row's range (the widest any
         * control uses), so a shared link cannot hand the renderer a value no control can make. The ids
         * are copied as written: an id whose overrides are unset is kept, as it always was, rather than
         * dropped by [SceneParamsCodec.unpack].
         */
        private fun readParams(o: JSONObject): SceneParams {
            val frame = FloatArray(SceneParamsCodec.FIELDS)
            for (spec in ParamSchema.params) {
                val def = spec.default
                val v =
                    when {
                        spec.retired -> def
                        spec.kind == ParamSpec.Kind.BOOL -> if (o.optBoolean(spec.name, def > 0.5f)) 1f else 0f
                        spec.kind == ParamSpec.Kind.INT -> o.optInt(spec.name, def.roundToInt()).toFloat()
                        else -> finiteOr(o.optDouble(spec.name, def.toDouble()), def)
                    }
                // A circular value wraps natively (1.5 is 0.5); clamping it would turn the hue.
                frame[spec.slot] = if (spec.kind == ParamSpec.Kind.CIRCULAR) v else v.coerceIn(spec.range)
            }
            val read = SceneParamsCodec.unpack(frame, SceneParams.DEFAULT)
            return read.copy(
                customPaletteId = o.optString(KEY_CUSTOM_PALETTE_ID, SceneParams.NO_CUSTOM_PALETTE),
                customPalette2Id = o.optString(KEY_CUSTOM_PALETTE2_ID, SceneParams.NO_CUSTOM_PALETTE),
            )
        }

        internal fun fromJson(json: String): Preset {
            val o = JSONObject(json)
            return Preset(
                name = o.getString("name"),
                sceneId = Catalogue.resolve(o.getString("sceneId")),
                attack = finiteIn(o.getDouble("attack"), ATTACK_RANGE),
                decay = finiteIn(o.getDouble("decay"), DECAY_RANGE),
                customShader = if (o.has("customShader")) o.getString("customShader") else null,
                milkPreset = if (o.has("milkPreset")) o.getString("milkPreset") else null,
                params = readParams(o),
            )
        }
    }
}
