package dev.geode.data

import android.content.Context
import dev.geode.render.scene.SceneParams
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * A saved palette: your colours ([stops], packed 0xRRGGBB, at most [MAX_STOPS]), or, for one saved
 * before the colour picker, only a hue gradient ([baseHue], [hueSpan]) with no stops.
 */
data class CustomPalette(
    val id: String,
    val name: String,
    val baseHue: Float,
    val hueSpan: Float,
    val stops: List<Int> = emptyList(),
)

class PaletteStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "palettes").apply { mkdirs() }

    init {
        migrateLegacyIds()
    }

    private fun migrateLegacyIds() {
        dir
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.extension == "json" }
            .forEach { f ->
                val p = runCatching { fromJson(f.readText()) }.getOrNull() ?: return@forEach
                val id = idFor(p.name)
                if (f.nameWithoutExtension == id) return@forEach
                val target = File(dir, "$id.json")
                if (target.exists()) return@forEach
                if (AtomicWrite.text(target, toJson(p.copy(id = id)))) f.delete()
            }
    }

    fun list(): List<CustomPalette> =
        dir
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.extension == "json" }
            .mapNotNull { runCatching { fromJson(it.readText()) }.getOrNull() }
            .sortedBy { it.name.lowercase() }

    fun get(id: String): CustomPalette? = list().firstOrNull { it.id == id }

    fun save(palette: CustomPalette): CustomPalette {
        val clean = sanitized(palette)
        AtomicWrite.text(File(dir, clean.id + ".json"), toJson(clean))
        return clean
    }

    fun delete(id: String) {
        File(dir, sanitize(id) + ".json").delete()
    }

    companion object {
        const val MAX_STOPS: Int = 5
        private const val WHITE = 0xFFFFFF

        fun idFor(name: String): String = sanitize(name)

        fun create(
            name: String,
            baseHue: Float,
            hueSpan: Float,
        ): CustomPalette = sanitized(CustomPalette(idFor(name), name, baseHue, hueSpan))

        fun create(
            name: String,
            stops: List<Int>,
        ): CustomPalette = sanitized(CustomPalette(idFor(name), name, 0f, 0f, stops))

        fun applyGradient(
            p: SceneParams,
            baseHue: Float,
            hueSpan: Float,
            id: String = SceneParams.NO_CUSTOM_PALETTE,
        ): SceneParams {
            val base = baseHue.coerceIn(0f, 1f)
            val span = hueSpan.coerceIn(0f, 1f)
            return p.withPaletteStops(emptyList()).copy(paletteBaseOverride = base, paletteRangeOverride = span, customPaletteId = id)
        }

        /** A saved palette as the look's own: its colours, else its gradient. */
        fun applyPalette(
            p: SceneParams,
            palette: CustomPalette,
        ): SceneParams =
            if (palette.stops.isNotEmpty()) {
                applyStops(p, palette.stops, palette.id)
            } else {
                applyGradient(clear(p), palette.baseHue, palette.hueSpan, palette.id)
            }

        /** Your colours on the first slot, the hue overrides off; [id] names the saved palette they came from, if any. */
        fun applyStops(
            p: SceneParams,
            stops: List<Int>,
            id: String = SceneParams.NO_CUSTOM_PALETTE,
        ): SceneParams = clear(p).withPaletteStops(stops.take(MAX_STOPS)).copy(customPaletteId = id)

        fun clear(p: SceneParams): SceneParams = p.withoutCustomPalette()

        fun forgetDeleted(
            p: SceneParams,
            deletedId: String,
        ): SceneParams {
            if (deletedId == SceneParams.NO_CUSTOM_PALETTE) return p
            var out = p
            if (out.customPaletteId == deletedId) out = out.copy(customPaletteId = SceneParams.NO_CUSTOM_PALETTE)
            return out
        }

        internal fun sanitize(name: String): String =
            PresetStore
                .safeFileName(name)
                .trim()
                .ifEmpty { "palette" }

        internal fun sanitized(c: CustomPalette): CustomPalette =
            CustomPalette(
                id = sanitize(c.id.ifBlank { c.name }),
                name = c.name.trim().ifEmpty { "Palette" },
                baseHue = c.baseHue.coerceIn(0f, 1f),
                hueSpan = c.hueSpan.coerceIn(0f, 1f),
                stops = c.stops.filter { it in 0..WHITE }.take(MAX_STOPS),
            )

        internal fun toJson(c: CustomPalette): String =
            JSONObject()
                .put("id", c.id)
                .put("name", c.name)
                .put("baseHue", c.baseHue.toDouble())
                .put("hueSpan", c.hueSpan.toDouble())
                .put("stops", JSONArray(c.stops))
                .toString(2)

        internal fun fromJson(json: String): CustomPalette {
            val o = JSONObject(json)
            val name = o.getString("name")
            return CustomPalette(
                id = o.optString("id").ifBlank { idFor(name) },
                name = name,
                baseHue = o.optDouble("baseHue", 0.0).toFloat().coerceIn(0f, 1f),
                hueSpan = o.optDouble("hueSpan", 1.0).toFloat().coerceIn(0f, 1f),
                stops =
                    o
                        .optJSONArray("stops")
                        ?.let { a ->
                            List(a.length()) { a.optInt(it, -1) }
                        }.orEmpty()
                        .filter { it in 0..WHITE }
                        .take(MAX_STOPS),
            )
        }
    }
}
