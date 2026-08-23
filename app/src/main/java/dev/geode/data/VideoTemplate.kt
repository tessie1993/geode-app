// Geode video templates — the portable "one file in, one finished video out" unit.
//
// A template bundles four things that together remove every remaining decision:
//   look    — the scene and its parameters (the same shape a Preset stores)
//   layout  — framing, artwork treatment, progress readout, colours
//   text    — placeholder patterns ("{artist} — {title}") anchored on the frame
//   export  — quality, frame rate, audio handling, which slice of the track
//
// Applying it to one track yields a TemplateJob; applying it to a list yields one
// job per track. A TemplateJob is complete: an exporter can run it without asking
// the user anything.
//
// Two conventions here exist purely so a template saved today still loads
// completely in any future build. TemplateFormat.kt documents the whole scheme;
// the short version:
//
//   * Tolerant<T> wraps every closed-set value. A token this build does not know
//     survives as Tolerant.Foreign and is written back verbatim, while callers read
//     a documented default through orElse(). That keeps every `when` over an enum
//     exhaustive without ever losing a newer build's choice.
//   * ForeignFields carries the object keys this build did not recognise. Every
//     section owns one, so a newer build's settings survive a load/save round trip
//     through an older build untouched.
//
// The constructor defaults below ARE the documented defaults: a field missing from
// a file takes the value written here, and nothing else.
//
// Nothing in this file touches the filesystem — TemplateFormat.kt serialises and
// TemplateRepository.kt stores, imports and exports.

package dev.geode.data

import dev.geode.export.ExportAspect
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import java.util.UUID

/**
 * A value drawn from a closed set that a *future* build may extend.
 *
 * [Known] is a value this build understands. [Foreign] is a token written by a newer
 * build: it is kept verbatim so saving the template again does not destroy it, and
 * [orElse] supplies the documented fallback wherever the value is actually used.
 */
sealed interface Tolerant<out T : Any> {
    /** The value when this build understands it, null when it is a foreign token. */
    val valueOrNull: T?

    data class Known<T : Any>(
        val value: T,
    ) : Tolerant<T> {
        override val valueOrNull: T get() = value
    }

    data class Foreign(
        val token: String,
    ) : Tolerant<Nothing> {
        override val valueOrNull: Nothing? get() = null
    }
}

fun <T : Any> Tolerant<T>.orElse(fallback: T): T = valueOrNull ?: fallback

/**
 * Stable identity of a template, carried inside the shared file.
 *
 * Re-importing the same file updates the template already in the library instead of
 * piling up copies, which is what makes a template tradeable rather than disposable.
 */
@JvmInline
value class TemplateId private constructor(
    val value: String,
) {
    override fun toString(): String = value

    companion object {
        fun random(): TemplateId = TemplateId(UUID.randomUUID().toString())

        /**
         * Parses an id that came from a file. Ids are opaque — whatever the author's
         * build wrote is kept — but a blank id is not an id, so the caller decides
         * (the repository mints a fresh one).
         */
        fun parse(raw: String?): TemplateId? = raw?.trim()?.takeIf { it.isNotEmpty() }?.let(::TemplateId)

        /** Compile-time ids for the bundled starters, which are never blank. */
        internal fun fixed(raw: String): TemplateId = TemplateId(raw)
    }
}

/**
 * Where a template in the library came from.
 *
 * Deliberately *not* written to the file, and deliberately only two states: a
 * template shared over WhatsApp must not claim to be bundled on the receiver's
 * device, and once a file is in the library there is nothing on disk that could
 * honestly distinguish "I made this" from "someone sent me this".
 */
enum class TemplateOrigin {
    /** Shipped with the app; browsable, adopted by copying. */
    BUNDLED,

    /** In this device's library, however it got there. */
    SAVED,
}

/**
 * The whole tradeable unit. One template, one file.
 *
 * [formatVersion] records the newest format generation that has ever touched this
 * template, so an old build re-saving a newer file does not mislabel it as old.
 * [origin] is local knowledge and is not serialised: a template shared over WhatsApp
 * must not claim to be bundled on the receiver's device.
 */
data class VideoTemplate(
    val id: TemplateId,
    val name: String,
    val look: TemplateLook = TemplateLook(),
    val layout: TemplateLayout = TemplateLayout(),
    val text: TemplateText = TemplateText(),
    val export: TemplateExport = TemplateExport(),
    val author: String = "",
    val notes: String = "",
    val createdAtMs: Long = 0L,
    val origin: TemplateOrigin = TemplateOrigin.SAVED,
    val formatVersion: Int = TemplateFormat.FORMAT_VERSION,
    val foreign: ForeignFields = ForeignFields.NONE,
)

/**
 * A fresh, independent copy — new identity, no inherited authorship.
 *
 * Used when adopting a bundled starter or duplicating a template, so that editing
 * the copy never overwrites the original on the next import.
 */
fun VideoTemplate.asNewCopy(
    newName: String,
    atMs: Long,
): VideoTemplate =
    copy(
        id = TemplateId.random(),
        name = newName,
        author = "",
        createdAtMs = atMs,
        origin = TemplateOrigin.SAVED,
    )

// ---------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------

/**
 * The visual identity: exactly what a [Preset] carries, minus the preset's name.
 *
 * [paramsForeign] is a second carrier because the scene parameters live in their own
 * nested object; unknown *parameters* and unknown *look* keys are separate sets.
 */
data class TemplateLook(
    val sceneId: String = SceneIds.DEFAULT,
    val attack: Float = DEFAULT_ATTACK,
    val decay: Float = DEFAULT_DECAY,
    val params: SceneParams = SceneParams.DEFAULT,
    val customShader: String? = null,
    val milkPreset: String? = null,
    val foreign: ForeignFields = ForeignFields.NONE,
    val paramsForeign: ForeignFields = ForeignFields.NONE,
) {
    /** Bridges into the existing preset machinery so applying a template reuses it. */
    fun toPreset(name: String): Preset =
        Preset(
            name = name,
            sceneId = sceneId,
            attack = attack,
            decay = decay,
            customShader = customShader,
            params = params,
            milkPreset = milkPreset,
        )

    companion object {
        const val DEFAULT_ATTACK: Float = 0.6f
        const val DEFAULT_DECAY: Float = 0.12f

        fun from(preset: Preset): TemplateLook =
            TemplateLook(
                sceneId = preset.sceneId,
                attack = preset.attack,
                decay = preset.decay,
                params = preset.params,
                customShader = preset.customShader,
                milkPreset = preset.milkPreset,
            )
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

/** How the track's artwork is treated behind the visualiser. */
enum class ArtworkStyle {
    NONE,
    BEHIND_BLURRED,
    CARD,
    CIRCLE,
}

/** How playback position is drawn, if at all. */
enum class ProgressStyle {
    NONE,
    BAR,
    RING,
    WAVEFORM,
}

/**
 * Framing and composition. The aspect ratio lives here rather than in [TemplateExport]
 * because framing is a layout decision — keeping one copy makes a template that
 * contradicts itself unrepresentable.
 */
data class TemplateLayout(
    val ratio: Tolerant<ExportRatio> = Tolerant.Known(DEFAULT_RATIO),
    val artwork: Tolerant<ArtworkStyle> = Tolerant.Known(DEFAULT_ARTWORK),
    val artworkScale: Float = DEFAULT_ARTWORK_SCALE,
    val progress: Tolerant<ProgressStyle> = Tolerant.Known(DEFAULT_PROGRESS),
    val safeAreaFraction: Float = DEFAULT_SAFE_AREA,
    val accentArgb: Int = DEFAULT_ACCENT_ARGB,
    val backdropArgb: Int = DEFAULT_BACKDROP_ARGB,
    val foreign: ForeignFields = ForeignFields.NONE,
) {
    fun resolvedRatio(): ExportRatio = ratio.orElse(DEFAULT_RATIO)

    fun resolvedArtwork(): ArtworkStyle = artwork.orElse(DEFAULT_ARTWORK)

    fun resolvedProgress(): ProgressStyle = progress.orElse(DEFAULT_PROGRESS)

    companion object {
        val DEFAULT_RATIO: ExportRatio = ExportRatio.R9_16
        val DEFAULT_ARTWORK: ArtworkStyle = ArtworkStyle.BEHIND_BLURRED
        val DEFAULT_PROGRESS: ProgressStyle = ProgressStyle.BAR

        const val DEFAULT_ARTWORK_SCALE: Float = 0.62f
        const val DEFAULT_SAFE_AREA: Float = 0.06f
        const val DEFAULT_ACCENT_ARGB: Int = -1 // opaque white
        const val DEFAULT_BACKDROP_ARGB: Int = -16777216 // opaque black
    }
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

/** What a slot is *for*. Drives the default pattern and how an editor labels it. */
enum class TextRole {
    TITLE,
    ARTIST,
    ALBUM,
    CAPTION,
    HANDLE,
    CREDIT,
}

/** Nine-point anchor; [TextSlot.offsetX]/[TextSlot.offsetY] nudge from there. */
enum class TextAnchor {
    TOP_LEFT,
    TOP_CENTER,
    TOP_RIGHT,
    MIDDLE_LEFT,
    MIDDLE_CENTER,
    MIDDLE_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    BOTTOM_RIGHT,
}

enum class TextWeight {
    REGULAR,
    MEDIUM,
    BOLD,
}

/**
 * One piece of text on the frame.
 *
 * [pattern] is stored verbatim, tokens and all, so a pattern using a placeholder this
 * build has never heard of still round-trips untouched — it merely renders without
 * that fragment here. Offsets are fractions of the frame, so a slot survives a change
 * of aspect ratio or resolution.
 */
data class TextSlot(
    val role: Tolerant<TextRole> = Tolerant.Known(DEFAULT_ROLE),
    val pattern: String = "",
    val anchor: Tolerant<TextAnchor> = Tolerant.Known(DEFAULT_ANCHOR),
    val offsetX: Float = 0f,
    val offsetY: Float = 0f,
    val sizeSp: Float = DEFAULT_SIZE_SP,
    val colorArgb: Int = DEFAULT_COLOR_ARGB,
    val weight: Tolerant<TextWeight> = Tolerant.Known(DEFAULT_WEIGHT),
    val allCaps: Boolean = false,
    val shadow: Boolean = true,
    val foreign: ForeignFields = ForeignFields.NONE,
) {
    /** The pattern to actually render: a blank pattern falls back to the role's own. */
    fun effectivePattern(): String = pattern.ifBlank { defaultPatternFor(role.orElse(DEFAULT_ROLE)) }

    companion object {
        val DEFAULT_ROLE: TextRole = TextRole.CAPTION
        val DEFAULT_ANCHOR: TextAnchor = TextAnchor.BOTTOM_CENTER
        val DEFAULT_WEIGHT: TextWeight = TextWeight.MEDIUM

        const val DEFAULT_SIZE_SP: Float = 28f
        const val DEFAULT_COLOR_ARGB: Int = -1 // opaque white

        fun defaultPatternFor(role: TextRole): String =
            when (role) {
                TextRole.TITLE -> TemplatePlaceholder.TITLE.token
                TextRole.ARTIST -> TemplatePlaceholder.ARTIST.token
                TextRole.ALBUM -> TemplatePlaceholder.ALBUM.token
                TextRole.CAPTION -> ""
                TextRole.HANDLE -> ""
                TextRole.CREDIT -> ""
            }
    }
}

/** Every piece of text a template puts on the frame, in draw order. */
data class TemplateText(
    val slots: List<TextSlot> = DEFAULT_SLOTS,
    val foreign: ForeignFields = ForeignFields.NONE,
) {
    companion object {
        /** Title over artist, bottom-left inside the safe area — the common case. */
        val DEFAULT_SLOTS: List<TextSlot> =
            listOf(
                TextSlot(
                    role = Tolerant.Known(TextRole.TITLE),
                    anchor = Tolerant.Known(TextAnchor.BOTTOM_LEFT),
                    offsetY = -0.06f,
                    sizeSp = 34f,
                    weight = Tolerant.Known(TextWeight.BOLD),
                ),
                TextSlot(
                    role = Tolerant.Known(TextRole.ARTIST),
                    anchor = Tolerant.Known(TextAnchor.BOTTOM_LEFT),
                    sizeSp = 22f,
                    weight = Tolerant.Known(TextWeight.REGULAR),
                ),
            )
    }
}

/**
 * A token usable inside [TextSlot.pattern] and [TemplateExport.fileNamePattern].
 *
 * Adding a value here is always safe: older builds drop the fragment when rendering
 * but keep the pattern string itself, so the template is never damaged.
 */
enum class TemplatePlaceholder(
    val key: String,
) {
    TITLE("title"),
    ARTIST("artist"),
    ALBUM("album"),
    DURATION("duration"),
    INDEX("index"),
    TOTAL("total"),
    DATE("date"),
    ;

    val token: String get() = "{$key}"

    companion object {
        val PATTERN: Regex = Regex("\\{([A-Za-z0-9_]+)}")

        fun forKey(key: String): TemplatePlaceholder? = TemplatePlaceholder.entries.firstOrNull { it.key.equals(key, ignoreCase = true) }
    }
}

/** Everything a template needs to know about one track to fill its placeholders. */
data class TrackFacts(
    val uri: String,
    val title: String,
    val artist: String = "",
    val album: String = "",
    val durationMs: Long = 0L,
    val index: Int = 1,
    val total: Int = 1,
    val dateLabel: String = "",
)

/** A slot with its placeholders already substituted for one track. */
data class ResolvedText(
    val slot: TextSlot,
    val text: String,
)

/**
 * Substitutes placeholders for this track.
 *
 * A token this build does not know renders as nothing rather than as a literal
 * "{bpm}" on screen, and the surrounding separators are tidied so the result never
 * looks half-drawn. The pattern itself is untouched — only the rendering is lossy,
 * so a newer build fills it in properly.
 */
fun TrackFacts.fill(pattern: String): String {
    val substituted =
        TemplatePlaceholder.PATTERN.replace(pattern) { match ->
            val placeholder = TemplatePlaceholder.forKey(match.groupValues[1])
            if (placeholder == null) "" else textFor(placeholder)
        }
    return tidy(substituted)
}

/**
 * Resolves every slot for one track, dropping the ones with nothing to show.
 *
 * Slots whose role is [Tolerant.Foreign] are skipped: this build has no idea how a
 * future role is meant to look and will not invent a rendering for it. They stay in
 * the template and are written back untouched.
 */
fun TemplateText.resolve(facts: TrackFacts): List<ResolvedText> =
    slots.mapNotNull { slot ->
        when (slot.role) {
            is Tolerant.Foreign -> null
            is Tolerant.Known ->
                facts
                    .fill(slot.effectivePattern())
                    .takeIf { it.isNotEmpty() }
                    ?.let { filled -> ResolvedText(slot, filled) }
        }
    }

private fun TrackFacts.textFor(placeholder: TemplatePlaceholder): String =
    when (placeholder) {
        TemplatePlaceholder.TITLE -> title
        TemplatePlaceholder.ARTIST -> artist
        TemplatePlaceholder.ALBUM -> album
        TemplatePlaceholder.DURATION -> clockLabel(durationMs)
        TemplatePlaceholder.INDEX -> index.toString()
        TemplatePlaceholder.TOTAL -> total.toString()
        TemplatePlaceholder.DATE -> dateLabel
    }

private fun clockLabel(ms: Long): String {
    if (ms <= 0L) return ""
    val seconds = ms / 1000L
    return "%d:%02d".format(seconds / 60L, seconds % 60L)
}

/** Collapses the gaps a dropped placeholder leaves behind, including dangling separators. */
private fun tidy(text: String): String =
    text
        .replace(REPEATED_SPACE, " ")
        .trim()
        .trim { it in EDGE_PUNCTUATION }
        .trim()

private val REPEATED_SPACE = Regex("\\s{2,}")

private const val EDGE_PUNCTUATION = " -–—·|,;:"

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

enum class AudioHandling {
    FULL_MIX,
    MUTED,
}

/** The slice of the track a template renders. */
sealed interface TemplateSegment {
    data object WholeTrack : TemplateSegment

    data class Fixed(
        val startMs: Long,
        val durationMs: Long,
    ) : TemplateSegment

    /** A window around the track's loudest moment — the "hook" clip. */
    data class LoudestWindow(
        val durationMs: Long,
    ) : TemplateSegment

    /**
     * A segment kind introduced after this build. Its fields are carried verbatim so
     * the template survives; rendering falls back to [WholeTrack].
     */
    data class Unknown(
        val kind: String,
        val fields: ForeignFields,
    ) : TemplateSegment
}

/** The concrete slice a [TemplateSegment] resolves to for one track. */
data class TemplateWindow(
    val startMs: Long,
    val durationMs: Long,
)

/**
 * Resolves the segment against a real track.
 *
 * [hookStartMs] is where the caller's analysis found the loudest moment; it is ignored
 * unless the segment asks for it, so callers that have no analysis can pass 0.
 */
fun TemplateSegment.windowFor(
    trackDurationMs: Long,
    hookStartMs: Long,
): TemplateWindow =
    when (this) {
        TemplateSegment.WholeTrack -> TemplateWindow(0L, trackDurationMs.coerceAtLeast(0L))
        is TemplateSegment.Fixed -> clampWindow(startMs, durationMs, trackDurationMs)
        is TemplateSegment.LoudestWindow -> clampWindow(hookStartMs, durationMs, trackDurationMs)
        is TemplateSegment.Unknown -> TemplateWindow(0L, trackDurationMs.coerceAtLeast(0L))
    }

private fun clampWindow(
    startMs: Long,
    durationMs: Long,
    trackDurationMs: Long,
): TemplateWindow {
    val track = trackDurationMs.coerceAtLeast(0L)
    val start = startMs.coerceIn(0L, track)
    val requested = if (durationMs <= 0L) track - start else durationMs
    return TemplateWindow(start, requested.coerceIn(0L, track - start))
}

/**
 * Everything about producing the file, apart from framing (see [TemplateLayout]).
 *
 * [fps] is stored as written; only 30 and 60 are encodable, so [resolvedFps] snaps to
 * the nearer of the two while the authored value round-trips untouched.
 */
data class TemplateExport(
    val quality: Tolerant<ExportQuality> = Tolerant.Known(DEFAULT_QUALITY),
    val fps: Int = DEFAULT_FPS,
    val loopSafe: Boolean = true,
    val audio: Tolerant<AudioHandling> = Tolerant.Known(DEFAULT_AUDIO),
    val segment: TemplateSegment = TemplateSegment.WholeTrack,
    val fileNamePattern: String = DEFAULT_FILE_NAME_PATTERN,
    val foreign: ForeignFields = ForeignFields.NONE,
) {
    fun resolvedQuality(): ExportQuality = quality.orElse(DEFAULT_QUALITY)

    fun resolvedAudio(): AudioHandling = audio.orElse(DEFAULT_AUDIO)

    fun resolvedFps(): Int = if (fps <= FPS_MIDPOINT) 30 else 60

    companion object {
        val DEFAULT_QUALITY: ExportQuality = ExportQuality.FHD1080
        val DEFAULT_AUDIO: AudioHandling = AudioHandling.FULL_MIX

        const val DEFAULT_FPS: Int = 30
        const val DEFAULT_FILE_NAME_PATTERN: String = "{artist} - {title}"

        private const val FPS_MIDPOINT: Int = 45
    }
}

// ---------------------------------------------------------------------------
// Applying a template
// ---------------------------------------------------------------------------

/**
 * One track, one template, every decision already made.
 *
 * The whole [template] rides along rather than a copy of a few fields, so the exporter
 * never has to reach back for something the plan forgot.
 */
data class TemplateJob(
    val template: VideoTemplate,
    val track: TrackFacts,
    val outputName: String,
    val texts: List<ResolvedText>,
    val window: TemplateWindow,
    val width: Int,
    val height: Int,
    val bitRate: Int,
    val fps: Int,
    val muteAudio: Boolean,
)

/** Plans a single render. */
fun VideoTemplate.jobFor(
    track: TrackFacts,
    hookStartMs: Long = 0L,
): TemplateJob {
    val aspect = ExportAspect.of(export.resolvedQuality(), layout.resolvedRatio())
    return TemplateJob(
        template = this,
        track = track,
        outputName = outputNameFor(track),
        texts = text.resolve(track),
        window = export.segment.windowFor(track.durationMs, hookStartMs),
        width = aspect.width,
        height = aspect.height,
        bitRate = aspect.bitRate,
        fps = export.resolvedFps(),
        muteAudio = export.resolvedAudio() == AudioHandling.MUTED,
    )
}

/**
 * Plans a batch: one job per track, in order.
 *
 * `{index}`/`{total}` are filled from the batch position, and output names are made
 * unique so two tracks sharing a title cannot silently overwrite one another.
 */
fun VideoTemplate.jobsFor(
    tracks: List<TrackFacts>,
    hookStartMs: (TrackFacts) -> Long = { 0L },
): List<TemplateJob> {
    val taken = mutableSetOf<String>()
    return tracks.mapIndexed { position, track ->
        val facts = track.copy(index = position + 1, total = tracks.size)
        val job = jobFor(facts, hookStartMs(facts))
        job.copy(outputName = uniqueName(job.outputName, taken))
    }
}

private fun VideoTemplate.outputNameFor(track: TrackFacts): String {
    val filled = track.fill(export.fileNamePattern)
    return sanitizeFileStem(filled.ifBlank { track.title }.ifBlank { name })
}

private fun uniqueName(
    base: String,
    taken: MutableSet<String>,
): String {
    var candidate = base
    var attempt = 2
    while (!taken.add(candidate)) {
        candidate = "$base ($attempt)"
        attempt++
    }
    return candidate
}

/** Keeps spaces (they read fine everywhere) but drops anything a file name cannot hold. */
private fun sanitizeFileStem(raw: String): String =
    raw
        .replace(UNSAFE_FILE_CHARS, "_")
        .trim()
        .take(MAX_FILE_STEM)
        .trim()
        .ifBlank { "Geode video" }

private val UNSAFE_FILE_CHARS = Regex("""[\\/:*?"<>|]""")

private const val MAX_FILE_STEM = 80
