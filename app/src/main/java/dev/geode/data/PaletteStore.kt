package dev.geode.data

import android.content.Context
import dev.geode.render.bridge.PaletteFeed
import dev.geode.render.scene.SceneParams
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * A palette the user made: up to [PaletteStore.MAX_STOPS] colours ([stops], 0xRRGGBB in row order) painted
 * across a style as one smooth gradient (core/viz/Palette). [baseHue] and [hueSpan] are the hue-only
 * summary the families that colour by hue alone (the simulations, MilkDrop) and the second palette slot
 * use; the native side works them out from the stops, the shell only keeps them. A palette saved before
 * the colour wheel has only those two: it loads as [PaletteStore.legacyStops] sampled from them.
 */
data class CustomPalette(
    val id: String,
    val name: String,
    val baseHue: Float,
    val hueSpan: Float,
    val stops: List<Int> = emptyList(),
    val builtIn: Boolean = false,
)

/** Palette sequence (Customize › Look › Palette): saved palette ids in play order, one step every [beats] beats. */
data class PaletteSequence(
    val on: Boolean = false,
    val beats: Int = PaletteStore.DEFAULT_BEATS,
    val ids: List<String> = emptyList(),
)

class PaletteStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "palettes").apply { mkdirs() }

    init {
        migrateLegacyIds()
    }

    private fun paletteFiles(): List<File> =
        dir
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.extension == "json" && !it.name.startsWith("_") }

    private fun migrateLegacyIds() {
        paletteFiles().forEach { f ->
            val p = runCatching { fromJson(f.readText()) }.getOrNull() ?: return@forEach
            val id = idFor(p.name)
            if (f.nameWithoutExtension == id) return@forEach
            val target = File(dir, "$id.json")
            if (target.exists()) return@forEach
            if (AtomicWrite.text(target, toJson(p.copy(id = id)))) f.delete()
        }
    }

    fun list(): List<CustomPalette> =
        paletteFiles()
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

    /** The saved sequence (`_sequence.json` beside the palettes, so a backup of that folder carries it). */
    fun sequence(): PaletteSequence =
        runCatching {
            val o = JSONObject(File(dir, SEQUENCE_FILE).readText())
            val ids = o.optJSONArray("ids") ?: JSONArray()
            PaletteSequence(
                on = o.optBoolean("on", false),
                beats = o.optInt("beats", DEFAULT_BEATS).takeIf { it in BEATS } ?: DEFAULT_BEATS,
                ids = (0 until ids.length()).map { ids.getString(it) }.take(MAX_SEQUENCE),
            )
        }.getOrDefault(PaletteSequence())

    fun saveSequence(sequence: PaletteSequence) {
        val json =
            JSONObject()
                .put("on", sequence.on)
                .put("beats", sequence.beats)
                .put("ids", JSONArray(sequence.ids.take(MAX_SEQUENCE)))
        AtomicWrite.text(File(dir, SEQUENCE_FILE), json.toString(2))
    }

    /**
     * Hands every gradient (the built-in ones, the user's, and [extra], an audition) and the sequence to the
     * engine's feed, which each renderer sends to its native side; the saved ids in presets resolve there.
     */
    fun publish(extra: Map<String, IntArray> = emptyMap()) {
        val all = BuiltInGradients.ALL + list()
        val byId = all.filter { it.stops.isNotEmpty() }.associate { it.id to it.stops.toIntArray() }
        PaletteFeed.setGradients(byId + extra)
        PaletteFeed.setSequence(sequenceWire(sequence()) { byId[it] })
    }

    companion object {
        const val MAX_STOPS: Int = 8
        const val MAX_SEQUENCE: Int = 8
        const val DEFAULT_BEATS: Int = 16
        val BEATS: List<Int> = listOf(4, 8, 16, 32)
        const val PREVIEW_STOPS: Int = 5

        /** The id an audition ("Try it") rides under; it is never saved, so a preset that kept it falls back to its hues. */
        const val TRY_ID: String = "try"
        private const val SEQUENCE_FILE = "_sequence.json"

        /** Publishes what is saved, once, at app start: before any surface builds a renderer. */
        fun publishSaved(context: Context) = PaletteStore(context).publish()

        /** `[beats, paletteCount, (stopCount, rgb x stopCount) x paletteCount]`, `[0, 0]` while the sequence is off or empty. */
        internal fun sequenceWire(
            sequence: PaletteSequence,
            stopsOf: (String) -> IntArray?,
        ): IntArray {
            val palettes = if (sequence.on) sequence.ids.mapNotNull(stopsOf).filter { it.isNotEmpty() } else emptyList()
            if (palettes.isEmpty()) return intArrayOf(0, 0)
            return palettes.fold(intArrayOf(sequence.beats, palettes.size)) { wire, stops -> wire + stops.size + stops }
        }

        fun idFor(name: String): String = sanitize(name)

        fun create(
            name: String,
            stops: List<Int>,
        ): CustomPalette = sanitized(CustomPalette(idFor(name), name, 0f, 1f, stops))

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

        /** A hue-only palette (an old save, or a built-in hue palette) as [PREVIEW_STOPS] colour stops, 0xRRGGBB. */
        fun legacyStops(
            baseHue: Float,
            hueSpan: Float,
        ): List<Int> =
            (0 until PREVIEW_STOPS).map { i ->
                val (r, g, b) = hueRgb(sampleHue(baseHue, hueSpan, i))
                (channel(r) shl 16) or (channel(g) shl 8) or channel(b)
            }

        private fun channel(v: Float): Int = (v.coerceIn(0f, 1f) * 255f + 0.5f).toInt()

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
                stops = c.stops.take(MAX_STOPS).map { it and 0xFFFFFF },
            )

        internal fun toJson(c: CustomPalette): String =
            JSONObject()
                .put("id", c.id)
                .put("name", c.name)
                .put("baseHue", c.baseHue.toDouble())
                .put("hueSpan", c.hueSpan.toDouble())
                .put("stops", JSONArray(c.stops))
                .toString(2)

        /** Reads a palette; one saved before the colour wheel (no `stops`) comes back as [legacyStops] of its hues. */
        internal fun fromJson(json: String): CustomPalette {
            val o = JSONObject(json)
            val name = o.getString("name")
            val baseHue = o.optDouble("baseHue", 0.0).toFloat().coerceIn(0f, 1f)
            val hueSpan = o.optDouble("hueSpan", 1.0).toFloat().coerceIn(0f, 1f)
            val array = o.optJSONArray("stops")
            val stops =
                if (array != null && array.length() > 0) {
                    (0 until array.length()).map { array.optInt(it) and 0xFFFFFF }.take(MAX_STOPS)
                } else {
                    legacyStops(baseHue, hueSpan)
                }
            return CustomPalette(
                id = o.optString("id").ifBlank { idFor(name) },
                name = name,
                baseHue = baseHue,
                hueSpan = hueSpan,
                stops = stops,
            )
        }
    }
}

/** The palettes that ship with the app (read-only; "Edit" makes a copy to change). Data only: the colours are the design. */
object BuiltInGradients {
    private fun builtIn(
        id: String,
        name: String,
        vararg stops: Int,
    ) = CustomPalette("builtin:$id", name, 0f, 1f, stops.toList(), builtIn = true)

    val ALL: List<CustomPalette> =
        listOf(
            builtIn("sunset", "Sunset glow", 0x2B0F54, 0xB5179E, 0xFF5E62, 0xFFC857),
            builtIn("ocean", "Ocean deep", 0x03045E, 0x0077B6, 0x00B4D8, 0x90E0EF),
            builtIn("forest", "Forest floor", 0x0B2B1F, 0x1F6B3A, 0x7DB46C, 0xE9E6A2),
            builtIn("ember", "Ember", 0x1A0000, 0x7A0C0C, 0xFF4500, 0xFFD166),
            builtIn("aurora", "Northern lights", 0x0B132B, 0x1C7C7D, 0x5BE7A9, 0xB388FF),
            builtIn("candy", "Candy pop", 0xFF6FB5, 0xFFD166, 0x6EE7B7, 0x60A5FA),
            builtIn("ice", "Ice", 0x0A1128, 0x3A5BA0, 0xA9C5FF, 0xFFFFFF),
            builtIn("vapor", "Vapor", 0x2D0A4E, 0xFF4FD8, 0x4FF3FF),
            builtIn("toxic", "Toxic", 0x0B1A00, 0x39FF14, 0xF5FF4F),
            builtIn("royal", "Royal gold", 0x1B0C4B, 0x5B21B6, 0xD4A017, 0xFFF3B0),
            builtIn("blossom", "Cherry blossom", 0xFFC2D1, 0xFF8FAB, 0xB5446E, 0x4A1942),
            builtIn("neon", "Neon night", 0x00F5D4, 0x9B5DE5, 0xF15BB5, 0xFEE440),
            builtIn("desert", "Desert dusk", 0x3D1C02, 0xB5651D, 0xE9A66C, 0xF4E3C1),
            builtIn("space", "Deep space", 0x000814, 0x3A0CA3, 0x7209B7, 0xF72585),
        )
}
