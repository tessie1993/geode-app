package dev.geode.data

import android.content.Context
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
        const val PREVIEW_STOPS: Int = 9

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
            val base = baseHue.coerceIn(0f, 1f)
            val span = hueSpan.coerceIn(0f, 1f)
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

        fun sampleHue(
            baseHue: Float,
            hueSpan: Float,
            index: Int,
            stops: Int = PREVIEW_STOPS,
        ): Float {
            val t = if (stops <= 1) 0f else index.toFloat() / (stops - 1).toFloat()
            val h = baseHue + hueSpan * t
            return ((h % 1f) + 1f) % 1f
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
                baseHue = c.baseHue.coerceIn(0f, 1f),
                hueSpan = c.hueSpan.coerceIn(0f, 1f),
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
                baseHue = o.optDouble("baseHue", 0.0).toFloat().coerceIn(0f, 1f),
                hueSpan = o.optDouble("hueSpan", 1.0).toFloat().coerceIn(0f, 1f),
            )
        }
    }
}
