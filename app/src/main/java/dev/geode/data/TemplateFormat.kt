// The Geode template file format: one portable JSON document per template.
//
// THE GUARANTEE (product spec section 4 / C7)
//   A template saved today must load COMPLETELY in any future build, and a template
//   saved by a future build must survive a load/save round trip through this build
//   without losing a single setting. Nothing is ever silently dropped.
//
// SHAPE
//   {
//     "format": "geode.template",
//     "formatVersion": 1,
//     "id": "...", "name": "...", "author": "...", "notes": "...", "createdAtMs": 0,
//     "look":   { "sceneId": "...", "attack": 0.6, "decay": 0.12, "params": { ... } },
//     "layout": { "ratio": "R9_16", "artwork": "CARD", "accent": "#FFFFFFFF", ... },
//     "text":   { "slots": [ { "role": "TITLE", "pattern": "{title}", ... } ] },
//     "export": { "quality": "FHD1080", "fps": 30, "segment": { "kind": "WHOLE_TRACK" } }
//   }
//
//   Plain UTF-8 JSON, a few kilobytes, no binary blobs and no sidecar files: it
//   survives being attached to a WhatsApp message, pasted into a note, or committed
//   to a repo. The same bytes also travel as a geode://template/<gzip+base64url>
//   link (see toLink), which reuses PresetLink's wire format so the bounded-inflate
//   guard against hostile payloads lives in exactly one place.
//
// HOW VERSION TOLERANCE WORKS
//
//   1. Unknown KEYS are carried, not dropped.
//      Each section reads the keys it knows into a typed value and then subtracts:
//      the "known" key set is defined as the set of keys THIS BUILD'S WRITER EMITS
//      for the value it just parsed, and every other key in the source object is
//      kept in that section's ForeignFields. Writing emits the known keys and then
//      merges the carried ones back in.
//      Deriving "known" from the writer instead of from a hand-kept list of key
//      names is the point: adding a field to the writer automatically removes it
//      from the carried set, so the two halves can never drift apart.
//
//   2. Unknown ENUM TOKENS are carried, not clamped.
//      Closed-set values are Tolerant<T>. A token this build does not recognise
//      stays as Tolerant.Foreign(token) and is written back verbatim, while call
//      sites read a documented default through orElse(). An old build therefore
//      renders a newer template with a fallback and still hands the file on intact.
//
//   3. Unknown SEALED VARIANTS are carried, not collapsed.
//      TemplateSegment.Unknown keeps a future segment kind's name and all of its
//      fields and re-emits them unchanged.
//
//   4. MISSING fields take documented defaults.
//      The defaults are the constructor defaults in VideoTemplate.kt: one place,
//      readable, and the only thing a missing key is ever allowed to mean.
//
//   5. formatVersion never goes backwards.
//      Writing stamps max(FORMAT_VERSION, the version the template was read with),
//      because a file this build re-saved still contains the newer build's carried
//      fields. Re-labelling it "1" would be a lie about what is inside.
//
//   6. Readers NEVER refuse a file on version grounds.
//      There is no minimum-reader gate, by design. A higher formatVersion means
//      "expect carried fields", never "give up".
//
// RULES FOR FUTURE VERSIONS (these are what keep the guarantee true)
//
//   * Only ever ADD keys. Never reuse a key name for a different meaning and never
//     change a key's JSON type. The carry mechanism preserves unknown keys; it
//     cannot rescue a key whose meaning changed underneath it. New meaning, new key.
//   * Never delete a key from the writer alone. Drop it from the reader AND the
//     writer together and old files carry it verbatim forever, which is what we want.
//   * Add enum values freely: older builds carry the token.
//   * Add TemplateSegment variants freely: older builds carry them as Unknown.
//   * Bump FORMAT_VERSION and add a line to the history below. It is documentation
//     for humans, not a gate for machines.
//
// VERSION HISTORY
//   1 - initial format: look, layout, text slots, export settings.

package dev.geode.data

import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import dev.geode.ui.PresetLink
import org.json.JSONArray
import org.json.JSONObject

/**
 * Outcome of reading a template file, link or pasted blob.
 *
 * A failure is a value, never an exception: callers show the reason and leave the
 * original bytes alone rather than deleting or rewriting something they could not
 * understand.
 */
sealed interface TemplateParse {
    data class Parsed(
        val template: VideoTemplate,
    ) : TemplateParse

    /** Valid JSON, but not a Geode template. */
    data class NotATemplate(
        val why: String,
    ) : TemplateParse

    /** Not readable as JSON at all. */
    data class Malformed(
        val why: String,
    ) : TemplateParse
}

/**
 * The keys one section of a template file contained that this build did not
 * recognise, kept verbatim so that saving the template again cannot lose them.
 *
 * Opaque on purpose: the payload is JSON text, which is immutable and needs no
 * defensive copying, and callers get no way to reach in and reinterpret settings
 * whose meaning belongs to a different version of the app.
 */
class ForeignFields private constructor(
    internal val json: String,
) {
    val isEmpty: Boolean get() = json == EMPTY_OBJECT

    /** The carried key names, for diagnostics such as "3 newer settings kept as-is". */
    fun keys(): List<String> =
        runCatching {
            JSONObject(json)
                .keys()
                .asSequence()
                .toList()
        }.getOrDefault(emptyList())

    override fun equals(other: Any?): Boolean = other is ForeignFields && other.json == json

    override fun hashCode(): Int = json.hashCode()

    override fun toString(): String = json

    companion object {
        private const val EMPTY_OBJECT: String = "{}"

        val NONE: ForeignFields = ForeignFields(EMPTY_OBJECT)

        internal fun of(fields: JSONObject): ForeignFields = if (fields.length() == 0) NONE else ForeignFields(fields.toString())

        internal fun objectOf(fields: ForeignFields): JSONObject = runCatching { JSONObject(fields.json) }.getOrDefault(JSONObject())
    }
}

/**
 * Serialises and parses [VideoTemplate]. Pure text in, pure text out: storage lives
 * in TemplateRepository.kt so the format can be tested and reasoned about on its own.
 */
object TemplateFormat {
    const val FORMAT: String = "geode.template"

    /** See the version history in the file header before changing this. */
    const val FORMAT_VERSION: Int = 1

    /**
     * Double extension on purpose: `.json` is what messengers and file pickers know
     * how to attach and preview, while `.geode` in front keeps templates
     * recognisable in a crowded Downloads folder.
     */
    const val FILE_SUFFIX: String = ".geode.json"

    const val MIME_TYPE: String = "application/json"

    /** A template is kilobytes; anything past this is not one, so readers stop early. */
    const val MAX_FILE_BYTES: Int = 4 * 1024 * 1024

    const val LINK_HOST: String = "template"

    fun encode(template: VideoTemplate): String = rootOf(template).toString(JSON_INDENT)

    @Suppress("ReturnCount")
    fun decode(text: String): TemplateParse {
        val body = bodyOf(text) ?: return TemplateParse.Malformed("the template was empty")
        val root = runCatching { JSONObject(body) }.getOrNull() ?: return TemplateParse.Malformed("this is not JSON")
        if (!root.optString(KEY_FORMAT).equals(FORMAT, ignoreCase = true)) {
            return TemplateParse.NotATemplate("no '$KEY_FORMAT': '$FORMAT' marker")
        }
        return TemplateParse.Parsed(readTemplate(root))
    }

    fun isTemplateText(text: String): Boolean = decode(text) is TemplateParse.Parsed

    /**
     * A whole template as one pasteable link, or null when it is too long to survive
     * a chat client's line handling. The file is always the reliable path; the link
     * is the convenient one.
     */
    fun toLink(template: VideoTemplate): String? =
        PresetLink
            .encode(encode(template))
            .replaceFirst(PRESET_PREFIX, LINK_PREFIX)
            .takeIf { it.length <= PresetLink.MAX_LINK_LENGTH }

    /** Finds a template link inside a longer pasted message. */
    fun linkIn(text: String): String? {
        val start = text.indexOf(LINK_PREFIX, ignoreCase = true)
        if (start < 0) return null
        val end = (start until text.length).firstOrNull { text[it].isWhitespace() } ?: text.length
        return text.substring(start, end)
    }

    /** The name a shared copy of this template should arrive under. */
    fun fileNameFor(template: VideoTemplate): String {
        val stem =
            template.name
                .replace(UNSAFE_NAME_CHARS, "_")
                .trim()
                .take(MAX_NAME_LENGTH)
                .trim()
                .ifBlank { DEFAULT_NAME }
        return stem + FILE_SUFFIX
    }

    // -----------------------------------------------------------------------
    // Carry mechanism
    // -----------------------------------------------------------------------

    /**
     * Everything in [source] that [emitted] did not account for.
     *
     * [emitted] is what this build's writer produces for the value just parsed, so
     * the known-key set is always exactly in step with the writer.
     */
    private fun foreignOf(
        source: JSONObject?,
        emitted: JSONObject,
    ): ForeignFields {
        if (source == null) return ForeignFields.NONE
        val leftovers = JSONObject()
        for (key in source.keys()) {
            if (emitted.has(key)) continue
            runCatching { leftovers.put(key, source.get(key)) }
        }
        return ForeignFields.of(leftovers)
    }

    /**
     * Merges carried keys back in. A key this build now understands wins: once a
     * field has a meaning here, the typed value is the authoritative one.
     */
    private fun JSONObject.mergeForeign(foreign: ForeignFields): JSONObject {
        val extra = ForeignFields.objectOf(foreign)
        for (key in extra.keys()) {
            if (has(key)) continue
            runCatching { put(key, extra.get(key)) }
        }
        return this
    }

    private inline fun <reified E : Enum<E>> readEnum(
        source: JSONObject?,
        key: String,
        fallback: E,
    ): Tolerant<E> {
        val raw = source?.optString(key).orEmpty().trim()
        if (raw.isEmpty()) return Tolerant.Known(fallback)
        val match = enumValues<E>().firstOrNull { it.name.equals(raw, ignoreCase = true) }
        return if (match == null) Tolerant.Foreign(raw) else Tolerant.Known(match)
    }

    private fun tokenOf(tolerant: Tolerant<Enum<*>>): String =
        when (tolerant) {
            is Tolerant.Known -> tolerant.value.name
            is Tolerant.Foreign -> tolerant.token
        }

    // -----------------------------------------------------------------------
    // Root
    // -----------------------------------------------------------------------

    private fun rootOf(template: VideoTemplate): JSONObject =
        JSONObject()
            .put(KEY_FORMAT, FORMAT)
            .put(KEY_FORMAT_VERSION, maxOf(FORMAT_VERSION, template.formatVersion))
            .put(KEY_ID, template.id.value)
            .put(KEY_NAME, template.name)
            .put(KEY_AUTHOR, template.author)
            .put(KEY_NOTES, template.notes)
            .put(KEY_CREATED_AT, template.createdAtMs)
            .put(KEY_LOOK, lookOf(template.look))
            .put(KEY_LAYOUT, layoutOf(template.layout))
            .put(KEY_TEXT, textOf(template.text))
            .put(KEY_EXPORT, exportOf(template.export))
            .mergeForeign(template.foreign)

    private fun readTemplate(root: JSONObject): VideoTemplate {
        val bare =
            VideoTemplate(
                id = TemplateId.parse(root.optString(KEY_ID)) ?: TemplateId.random(),
                name = root.optString(KEY_NAME).trim().ifBlank { DEFAULT_NAME },
                look = readLook(root.optJSONObject(KEY_LOOK)),
                layout = readLayout(root.optJSONObject(KEY_LAYOUT)),
                text = readText(root.optJSONObject(KEY_TEXT)),
                export = readExport(root.optJSONObject(KEY_EXPORT)),
                author = root.optString(KEY_AUTHOR),
                notes = root.optString(KEY_NOTES),
                createdAtMs = root.optLong(KEY_CREATED_AT, 0L),
                formatVersion = root.optInt(KEY_FORMAT_VERSION, FORMAT_VERSION),
            )
        return bare.copy(foreign = foreignOf(root, rootOf(bare)))
    }

    // -----------------------------------------------------------------------
    // Look
    // -----------------------------------------------------------------------

    private fun lookOf(look: TemplateLook): JSONObject =
        JSONObject()
            .put(KEY_SCENE_ID, look.sceneId)
            .put(KEY_ATTACK, look.attack.toDouble())
            .put(KEY_DECAY, look.decay.toDouble())
            .put(KEY_PARAMS, PresetStore.paramsToJson(look.params).mergeForeign(look.paramsForeign))
            .also { out -> look.customShader?.let { out.put(KEY_CUSTOM_SHADER, it) } }
            .also { out -> look.milkPreset?.let { out.put(KEY_MILK_PRESET, it) } }
            .mergeForeign(look.foreign)

    private fun readLook(source: JSONObject?): TemplateLook {
        val paramsSource = source?.optJSONObject(KEY_PARAMS)
        val params =
            paramsSource
                ?.let { runCatching { PresetStore.paramsFromJson(it) }.getOrDefault(SceneParams.DEFAULT) }
                ?: SceneParams.DEFAULT
        val bare =
            TemplateLook(
                sceneId = source?.optString(KEY_SCENE_ID).orEmpty().ifBlank { SceneIds.DEFAULT },
                attack = optFloat(source, KEY_ATTACK, TemplateLook.DEFAULT_ATTACK),
                decay = optFloat(source, KEY_DECAY, TemplateLook.DEFAULT_DECAY),
                params = params,
                customShader = optStringOrNull(source, KEY_CUSTOM_SHADER),
                milkPreset = optStringOrNull(source, KEY_MILK_PRESET),
                paramsForeign = foreignOf(paramsSource, PresetStore.paramsToJson(params)),
            )
        return bare.copy(foreign = foreignOf(source, lookOf(bare)))
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------

    private fun layoutOf(layout: TemplateLayout): JSONObject =
        JSONObject()
            .put(KEY_RATIO, tokenOf(layout.ratio))
            .put(KEY_ARTWORK, tokenOf(layout.artwork))
            .put(KEY_ARTWORK_SCALE, layout.artworkScale.toDouble())
            .put(KEY_PROGRESS, tokenOf(layout.progress))
            .put(KEY_SAFE_AREA, layout.safeAreaFraction.toDouble())
            .put(KEY_ACCENT, argbHex(layout.accentArgb))
            .put(KEY_BACKDROP, argbHex(layout.backdropArgb))
            .mergeForeign(layout.foreign)

    private fun readLayout(source: JSONObject?): TemplateLayout {
        val bare =
            TemplateLayout(
                ratio = readEnum(source, KEY_RATIO, TemplateLayout.DEFAULT_RATIO),
                artwork = readEnum(source, KEY_ARTWORK, TemplateLayout.DEFAULT_ARTWORK),
                artworkScale = optFloat(source, KEY_ARTWORK_SCALE, TemplateLayout.DEFAULT_ARTWORK_SCALE),
                progress = readEnum(source, KEY_PROGRESS, TemplateLayout.DEFAULT_PROGRESS),
                safeAreaFraction = optFloat(source, KEY_SAFE_AREA, TemplateLayout.DEFAULT_SAFE_AREA),
                accentArgb = readArgb(source, KEY_ACCENT, TemplateLayout.DEFAULT_ACCENT_ARGB),
                backdropArgb = readArgb(source, KEY_BACKDROP, TemplateLayout.DEFAULT_BACKDROP_ARGB),
            )
        return bare.copy(foreign = foreignOf(source, layoutOf(bare)))
    }

    // -----------------------------------------------------------------------
    // Text
    // -----------------------------------------------------------------------

    private fun textOf(text: TemplateText): JSONObject {
        val slots = JSONArray()
        for (slot in text.slots) slots.put(slotOf(slot))
        return JSONObject().put(KEY_SLOTS, slots).mergeForeign(text.foreign)
    }

    private fun readText(source: JSONObject?): TemplateText {
        val array = source?.optJSONArray(KEY_SLOTS)
        // An absent array means "never configured" and takes the documented default; a
        // present but empty one is a real choice to put no text on the frame at all.
        val slots =
            if (array == null) {
                TemplateText.DEFAULT_SLOTS
            } else {
                (0 until array.length()).mapNotNull { index -> array.optJSONObject(index)?.let(::readSlot) }
            }
        val bare = TemplateText(slots = slots)
        return bare.copy(foreign = foreignOf(source, textOf(bare)))
    }

    private fun slotOf(slot: TextSlot): JSONObject =
        JSONObject()
            .put(KEY_ROLE, tokenOf(slot.role))
            .put(KEY_PATTERN, slot.pattern)
            .put(KEY_ANCHOR, tokenOf(slot.anchor))
            .put(KEY_OFFSET_X, slot.offsetX.toDouble())
            .put(KEY_OFFSET_Y, slot.offsetY.toDouble())
            .put(KEY_SIZE_SP, slot.sizeSp.toDouble())
            .put(KEY_COLOR, argbHex(slot.colorArgb))
            .put(KEY_WEIGHT, tokenOf(slot.weight))
            .put(KEY_ALL_CAPS, slot.allCaps)
            .put(KEY_SHADOW, slot.shadow)
            .mergeForeign(slot.foreign)

    private fun readSlot(source: JSONObject): TextSlot {
        val bare =
            TextSlot(
                role = readEnum(source, KEY_ROLE, TextSlot.DEFAULT_ROLE),
                pattern = source.optString(KEY_PATTERN),
                anchor = readEnum(source, KEY_ANCHOR, TextSlot.DEFAULT_ANCHOR),
                offsetX = optFloat(source, KEY_OFFSET_X, 0f),
                offsetY = optFloat(source, KEY_OFFSET_Y, 0f),
                sizeSp = optFloat(source, KEY_SIZE_SP, TextSlot.DEFAULT_SIZE_SP),
                colorArgb = readArgb(source, KEY_COLOR, TextSlot.DEFAULT_COLOR_ARGB),
                weight = readEnum(source, KEY_WEIGHT, TextSlot.DEFAULT_WEIGHT),
                allCaps = source.optBoolean(KEY_ALL_CAPS, false),
                shadow = source.optBoolean(KEY_SHADOW, true),
            )
        return bare.copy(foreign = foreignOf(source, slotOf(bare)))
    }

    // -----------------------------------------------------------------------
    // Export
    // -----------------------------------------------------------------------

    private fun exportOf(export: TemplateExport): JSONObject =
        JSONObject()
            .put(KEY_QUALITY, tokenOf(export.quality))
            .put(KEY_FPS, export.fps)
            .put(KEY_LOOP_SAFE, export.loopSafe)
            .put(KEY_AUDIO, tokenOf(export.audio))
            .put(KEY_SEGMENT, segmentOf(export.segment))
            .put(KEY_FILE_NAME, export.fileNamePattern)
            .mergeForeign(export.foreign)

    private fun readExport(source: JSONObject?): TemplateExport {
        val bare =
            TemplateExport(
                quality = readEnum(source, KEY_QUALITY, TemplateExport.DEFAULT_QUALITY),
                fps = source?.optInt(KEY_FPS, TemplateExport.DEFAULT_FPS) ?: TemplateExport.DEFAULT_FPS,
                loopSafe = source?.optBoolean(KEY_LOOP_SAFE, true) ?: true,
                audio = readEnum(source, KEY_AUDIO, TemplateExport.DEFAULT_AUDIO),
                segment = readSegment(source?.optJSONObject(KEY_SEGMENT)),
                fileNamePattern =
                    source
                        ?.optString(KEY_FILE_NAME)
                        .orEmpty()
                        .ifBlank { TemplateExport.DEFAULT_FILE_NAME_PATTERN },
            )
        return bare.copy(foreign = foreignOf(source, exportOf(bare)))
    }

    private fun segmentOf(segment: TemplateSegment): JSONObject =
        when (segment) {
            TemplateSegment.WholeTrack -> JSONObject().put(KEY_KIND, SEGMENT_WHOLE)
            is TemplateSegment.Fixed ->
                JSONObject()
                    .put(KEY_KIND, SEGMENT_FIXED)
                    .put(KEY_START_MS, segment.startMs)
                    .put(KEY_DURATION_MS, segment.durationMs)
            is TemplateSegment.LoudestWindow ->
                JSONObject()
                    .put(KEY_KIND, SEGMENT_LOUDEST)
                    .put(KEY_DURATION_MS, segment.durationMs)
            is TemplateSegment.Unknown -> JSONObject().put(KEY_KIND, segment.kind).mergeForeign(segment.fields)
        }

    private fun readSegment(source: JSONObject?): TemplateSegment {
        if (source == null) return TemplateSegment.WholeTrack
        val kind = source.optString(KEY_KIND).trim().ifBlank { SEGMENT_WHOLE }
        return when {
            kind.equals(SEGMENT_WHOLE, ignoreCase = true) -> TemplateSegment.WholeTrack
            kind.equals(SEGMENT_FIXED, ignoreCase = true) ->
                TemplateSegment.Fixed(
                    startMs = source.optLong(KEY_START_MS, 0L),
                    durationMs = source.optLong(KEY_DURATION_MS, 0L),
                )
            kind.equals(SEGMENT_LOUDEST, ignoreCase = true) ->
                TemplateSegment.LoudestWindow(durationMs = source.optLong(KEY_DURATION_MS, DEFAULT_HOOK_MS))
            // A kind from a later version: keep the name and every field it brought.
            else -> TemplateSegment.Unknown(kind, foreignOf(source, JSONObject().put(KEY_KIND, kind)))
        }
    }

    // -----------------------------------------------------------------------
    // Small readers
    // -----------------------------------------------------------------------

    /**
     * The JSON body of whatever was handed over: a pasted link, or a file that may
     * carry a byte order mark or stray leading whitespace from an editor or a cloud
     * drive. Starting at the opening brace deals with all of those at once.
     */
    @Suppress("ReturnCount")
    private fun bodyOf(raw: String): String? {
        val link = linkIn(raw)
        if (link != null) return decodeLink(link)
        val start = raw.indexOf('{')
        if (start < 0) return null
        return raw.substring(start).trimEnd().takeIf { it.isNotEmpty() }
    }

    private fun decodeLink(link: String): String? = PresetLink.decode(link.replaceFirst(LINK_PREFIX, PRESET_PREFIX, ignoreCase = true))

    private fun optFloat(
        source: JSONObject?,
        key: String,
        fallback: Float,
    ): Float = source?.optDouble(key, fallback.toDouble())?.toFloat() ?: fallback

    private fun optStringOrNull(
        source: JSONObject?,
        key: String,
    ): String? =
        source
            ?.takeIf { it.has(key) && !it.isNull(key) }
            ?.optString(key)
            ?.takeIf { it.isNotEmpty() }

    /** Colours travel as #AARRGGBB so a human editing the file by hand can read them. */
    private fun argbHex(argb: Int): String = "#%08X".format(argb)

    private fun readArgb(
        source: JSONObject?,
        key: String,
        fallback: Int,
    ): Int {
        val raw =
            source
                ?.optString(key)
                .orEmpty()
                .trim()
                .removePrefix("#")
        return raw.toLongOrNull(HEX_RADIX)?.toInt() ?: fallback
    }

    // -----------------------------------------------------------------------
    // Keys. Never rename one; see the rules in the file header.
    // -----------------------------------------------------------------------

    private const val KEY_FORMAT = "format"
    private const val KEY_FORMAT_VERSION = "formatVersion"
    private const val KEY_ID = "id"
    private const val KEY_NAME = "name"
    private const val KEY_AUTHOR = "author"
    private const val KEY_NOTES = "notes"
    private const val KEY_CREATED_AT = "createdAtMs"
    private const val KEY_LOOK = "look"
    private const val KEY_LAYOUT = "layout"
    private const val KEY_TEXT = "text"
    private const val KEY_EXPORT = "export"

    private const val KEY_SCENE_ID = "sceneId"
    private const val KEY_ATTACK = "attack"
    private const val KEY_DECAY = "decay"
    private const val KEY_PARAMS = "params"
    private const val KEY_CUSTOM_SHADER = "customShader"
    private const val KEY_MILK_PRESET = "milkPreset"

    private const val KEY_RATIO = "ratio"
    private const val KEY_ARTWORK = "artwork"
    private const val KEY_ARTWORK_SCALE = "artworkScale"
    private const val KEY_PROGRESS = "progress"
    private const val KEY_SAFE_AREA = "safeArea"
    private const val KEY_ACCENT = "accent"
    private const val KEY_BACKDROP = "backdrop"

    private const val KEY_SLOTS = "slots"
    private const val KEY_ROLE = "role"
    private const val KEY_PATTERN = "pattern"
    private const val KEY_ANCHOR = "anchor"
    private const val KEY_OFFSET_X = "offsetX"
    private const val KEY_OFFSET_Y = "offsetY"
    private const val KEY_SIZE_SP = "sizeSp"
    private const val KEY_COLOR = "color"
    private const val KEY_WEIGHT = "weight"
    private const val KEY_ALL_CAPS = "allCaps"
    private const val KEY_SHADOW = "shadow"

    private const val KEY_QUALITY = "quality"
    private const val KEY_FPS = "fps"
    private const val KEY_LOOP_SAFE = "loopSafe"
    private const val KEY_AUDIO = "audio"
    private const val KEY_SEGMENT = "segment"
    private const val KEY_FILE_NAME = "fileNamePattern"
    private const val KEY_KIND = "kind"
    private const val KEY_START_MS = "startMs"
    private const val KEY_DURATION_MS = "durationMs"

    private const val SEGMENT_WHOLE = "WHOLE_TRACK"
    private const val SEGMENT_FIXED = "FIXED"
    private const val SEGMENT_LOUDEST = "LOUDEST_WINDOW"

    private const val DEFAULT_NAME = "Untitled template"
    private const val DEFAULT_HOOK_MS = 30_000L
    private const val HEX_RADIX = 16
    private const val JSON_INDENT = 2
    private const val MAX_NAME_LENGTH = 60

    private val UNSAFE_NAME_CHARS = Regex("""[\\/:*?"<>|]""")

    private val LINK_PREFIX: String = "${PresetLink.SCHEME}://$LINK_HOST/"

    private val PRESET_PREFIX: String = "${PresetLink.SCHEME}://${PresetLink.HOST}/"
}
