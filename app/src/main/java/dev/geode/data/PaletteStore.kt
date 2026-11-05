package dev.geode.data

import android.content.Context
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.ConfigSchema
import dev.geode.render.scene.SceneParams
import org.json.JSONObject
import java.io.File

data class CustomPalette(
    val id: String,
    val name: String,
    val baseHue: Float,
    val hueSpan: Float,
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
        /** The gradient's rows: what [applyGradient] writes into the overrides and a saved palette reads as. */
        private val BASE_HUE = ConfigSchema["palette", "baseHue"]
        private val HUE_SPAN = ConfigSchema["palette", "hueSpan"]

        fun idFor(name: String): String = sanitize(name)

        fun create(
            name: String,
            baseHue: Float,
            hueSpan: Float,
        ): CustomPalette = sanitized(CustomPalette(idFor(name), name, baseHue, hueSpan))

        fun applyGradient(
            p: SceneParams,
            baseHue: Float,
            hueSpan: Float,
            id: String = SceneParams.NO_CUSTOM_PALETTE,
            second: Boolean = false,
        ): SceneParams {
            val base = baseHue.coerceIn(BASE_HUE.range)
            val span = hueSpan.coerceIn(HUE_SPAN.range)
            return if (second) {
                p.copy(palette2BaseOverride = base, palette2RangeOverride = span, customPalette2Id = id)
            } else {
                p.copy(paletteBaseOverride = base, paletteRangeOverride = span, customPaletteId = id)
            }
        }

        fun applyPalette(
            p: SceneParams,
            palette: CustomPalette,
            second: Boolean = false,
        ): SceneParams = applyGradient(p, palette.baseHue, palette.hueSpan, palette.id, second)

        fun clear(
            p: SceneParams,
            second: Boolean = false,
        ): SceneParams = p.withoutCustomPalette(second)

        fun forgetDeleted(
            p: SceneParams,
            deletedId: String,
        ): SceneParams {
            if (deletedId == SceneParams.NO_CUSTOM_PALETTE) return p
            var out = p
            if (out.customPaletteId == deletedId) out = out.copy(customPaletteId = SceneParams.NO_CUSTOM_PALETTE)
            if (out.customPalette2Id == deletedId) out = out.copy(customPalette2Id = SceneParams.NO_CUSTOM_PALETTE)
            return out
        }

        /** The hues the palette maker's preview draws, one per stop along the gradient (core/viz/ConfigMath). */
        fun previewHues(
            baseHue: Float,
            hueSpan: Float,
        ): List<Float> {
            val stops = GeodeNative.configPalettePreviewStops()
            return (0 until stops).map { GeodeNative.configPaletteSampleHue(baseHue, hueSpan, it, stops) }
        }

        fun hueRgb(
            hue: Float,
            saturation: Float = 0.85f,
            value: Float = 1f,
        ): Triple<Float, Float, Float> {
            val h = (((hue % 1f) + 1f) % 1f) * 6f
            val sector = h.toInt() % 6
            val f = h - h.toInt()
            val lo = value * (1f - saturation)
            val fall = value * (1f - saturation * f)
            val rise = value * (1f - saturation * (1f - f))
            return when (sector) {
                0 -> Triple(value, rise, lo)
                1 -> Triple(fall, value, lo)
                2 -> Triple(lo, value, rise)
                3 -> Triple(lo, fall, value)
                4 -> Triple(rise, lo, value)
                else -> Triple(value, lo, fall)
            }
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
                baseHue = c.baseHue.coerceIn(BASE_HUE.range),
                hueSpan = c.hueSpan.coerceIn(HUE_SPAN.range),
            )

        internal fun toJson(c: CustomPalette): String =
            JSONObject()
                .put("id", c.id)
                .put("name", c.name)
                .put("baseHue", c.baseHue.toDouble())
                .put("hueSpan", c.hueSpan.toDouble())
                .toString(2)

        internal fun fromJson(json: String): CustomPalette {
            val o = JSONObject(json)
            val name = o.getString("name")
            return CustomPalette(
                id = o.optString("id").ifBlank { idFor(name) },
                name = name,
                baseHue = o.optDouble("baseHue", BASE_HUE.default.toDouble()).toFloat().coerceIn(BASE_HUE.range),
                hueSpan = o.optDouble("hueSpan", HUE_SPAN.default.toDouble()).toFloat().coerceIn(HUE_SPAN.range),
            )
        }
    }
}
