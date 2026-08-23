package dev.geode.publish

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import java.util.Locale

/** One entry in the published track list. [startMs] is measured from the top of the video. */
data class PublishTrack(
    val title: String,
    val startMs: Long,
    val artist: String = "",
    val durationMs: Long = 0L,
)

/**
 * Everything a template is allowed to interpolate.
 *
 * Built once at the publish boundary so the expander only ever sees typed values — no map of
 * loose strings, and no way to reference a fact that was never gathered.
 */
data class PublishFacts(
    val title: String = "",
    val artist: String = "",
    val album: String = "",
    val scene: String = "",
    val preset: String = "",
    val date: String = "",
    val durationMs: Long = 0L,
    val tracks: List<PublishTrack> = emptyList(),
)

/**
 * The tokens a description or filename template may contain.
 *
 * The set is closed on purpose: the editor can list every token with its [hint], and adding one
 * without teaching [valueFrom] how to fill it will not compile.
 */
enum class PublishToken(
    val token: String,
    val hint: String,
) {
    TITLE("{title}", "Title of the video, or of the single track"),
    ARTIST("{artist}", "Artist"),
    ALBUM("{album}", "Album"),
    SCENE("{scene}", "Visualiser scene"),
    PRESET("{preset}", "Preset"),
    DATE("{date}", "Date of the export"),
    DURATION("{duration}", "Total running time"),
    TRACKS("{tracks}", "Track list, one line per track with its timestamp"),
    CHAPTERS("{chapters}", "Chapter markers, timestamps first, ready for YouTube"),
    APP("{app}", "Geode"),
    ;

    fun valueFrom(facts: PublishFacts): String =
        when (this) {
            TITLE -> facts.title
            ARTIST -> facts.artist
            ALBUM -> facts.album
            SCENE -> facts.scene
            PRESET -> facts.preset
            DATE -> facts.date
            DURATION -> DescriptionMaker.timestamp(facts.durationMs)
            TRACKS -> DescriptionMaker.trackList(facts.tracks)
            CHAPTERS -> DescriptionMaker.chapterList(facts.tracks)
            APP -> APP_NAME
        }
}

/** A rule YouTube applies before it will show chapters at all. */
enum class ChapterProblem(
    val message: String,
) {
    NO_ZERO_START("The first chapter has to start at 0:00 — YouTube ignores the whole list otherwise."),
    TOO_FEW("YouTube needs at least three chapters before it shows any."),
    TOO_SHORT("Every chapter has to run at least ten seconds, and one of these is shorter."),
    OUT_OF_ORDER("Chapter times have to climb, and one of these goes backwards."),
}

/**
 * Whether the track list will actually register as chapters.
 *
 * The description is still perfectly usable when it will not — the timestamps just stay plain text
 * — so this is advice to show, not an error to block on.
 */
sealed interface ChapterCheck {
    data object Ready : ChapterCheck

    data class NotChapters(
        val problems: List<ChapterProblem>,
    ) : ChapterCheck
}

/** The editable, saved pair of templates. */
data class PublishTemplates(
    val description: String = DEFAULT_DESCRIPTION,
    val fileName: String = DEFAULT_FILE_NAME,
)

/** A finished description, ready to show, copy and save under [fileName]. */
data class PublishDescription(
    val text: String,
    val fileName: String,
    val chapters: ChapterCheck,
    val unknownTokens: List<String>,
)

/**
 * Persists the user's edited templates.
 *
 * Takes the [SharedPreferences] rather than a [Context] so it stays a plain value store, matching
 * the other Geode preference stores.
 */
class PublishTemplateStore(
    private val prefs: SharedPreferences,
) {
    fun load(): PublishTemplates =
        PublishTemplates(
            description = prefs.getString(KEY_DESCRIPTION, null) ?: DEFAULT_DESCRIPTION,
            fileName = prefs.getString(KEY_FILE_NAME, null) ?: DEFAULT_FILE_NAME,
        )

    fun save(templates: PublishTemplates) {
        prefs
            .edit()
            .putString(KEY_DESCRIPTION, templates.description)
            .putString(KEY_FILE_NAME, templates.fileName)
            .apply()
    }

    /** Forgets the edits so [load] returns the shipped defaults again. */
    fun reset() {
        prefs.edit().remove(KEY_DESCRIPTION).remove(KEY_FILE_NAME).apply()
    }

    private companion object {
        const val KEY_DESCRIPTION = "publish_description_template"
        const val KEY_FILE_NAME = "publish_filename_template"
    }
}

/**
 * Builds the description that goes in the upload box: a track list with timestamps, chapter
 * markers, and whatever else the saved template asks for.
 */
object DescriptionMaker {
    fun build(
        facts: PublishFacts,
        templates: PublishTemplates,
        extension: String = DEFAULT_EXTENSION,
    ): PublishDescription =
        PublishDescription(
            text = expand(templates.description, facts).trimEnd(),
            fileName = fileName(templates.fileName, facts, extension),
            chapters = checkChapters(facts),
            unknownTokens = (unknownTokens(templates.description) + unknownTokens(templates.fileName)).distinct(),
        )

    /**
     * Substitutes every known token in [template].
     *
     * Anything that looks like a token but is not one is left exactly as written: silently deleting
     * `{titel}` would hide the typo, whereas seeing it in the preview is how the user finds it.
     */
    fun expand(
        template: String,
        facts: PublishFacts,
    ): String {
        var text = template
        for (token in PublishToken.entries) {
            if (text.contains(token.token)) text = text.replace(token.token, token.valueFrom(facts))
        }
        return text
    }

    /** Every `{…}` in [template] that is not a real token, de-duplicated, in the order written. */
    fun unknownTokens(template: String): List<String> {
        val known = PublishToken.entries.mapTo(mutableSetOf()) { it.token }
        return TOKEN_SHAPE
            .findAll(template)
            .map { it.value }
            .filterNot { it in known }
            .distinct()
            .toList()
    }

    /**
     * `m:ss`, or `h:mm:ss` past the hour — the two forms YouTube parses.
     *
     * Formatted against a fixed locale because these are read back by machine: a locale with
     * non-ASCII digits would produce timestamps YouTube cannot see.
     */
    fun timestamp(ms: Long): String {
        val total = (ms / MILLIS_PER_SECOND).coerceAtLeast(0L)
        val hours = total / SECONDS_PER_HOUR
        val minutes = total % SECONDS_PER_HOUR / SECONDS_PER_MINUTE
        val seconds = total % SECONDS_PER_MINUTE
        return if (hours > 0) {
            String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds)
        } else {
            String.format(Locale.US, "%d:%02d", minutes, seconds)
        }
    }

    /** One line per track, timestamp first so the list doubles as chapter markers. */
    fun trackList(tracks: List<PublishTrack>): String =
        tracks.joinToString("\n") { track ->
            val name = track.title.ifBlank { UNTITLED }
            if (track.artist.isBlank()) {
                "${timestamp(track.startMs)} $name"
            } else {
                "${timestamp(track.startMs)} ${track.artist} — $name"
            }
        }

    /** Chapter markers only: title without the artist, so the chapter chips stay readable. */
    fun chapterList(tracks: List<PublishTrack>): String =
        tracks.joinToString("\n") { track ->
            "${timestamp(track.startMs)} ${track.title.ifBlank { UNTITLED }}"
        }

    /** Checks the track list against YouTube's chapter rules. */
    fun checkChapters(facts: PublishFacts): ChapterCheck {
        val tracks = facts.tracks
        var outOfOrder = false
        var tooShort = false
        var previousStart = -1L
        tracks.forEachIndexed { index, track ->
            if (track.startMs <= previousStart) outOfOrder = true
            previousStart = track.startMs
            val end = tracks.getOrNull(index + 1)?.startMs ?: endOfLastTrack(track, facts)
            if (end > 0L && end - track.startMs < MIN_CHAPTER_MS) tooShort = true
        }
        val problems =
            buildList {
                if (tracks.isEmpty() || tracks.first().startMs != 0L) add(ChapterProblem.NO_ZERO_START)
                if (tracks.size < MIN_CHAPTERS) add(ChapterProblem.TOO_FEW)
                if (tooShort) add(ChapterProblem.TOO_SHORT)
                if (outOfOrder) add(ChapterProblem.OUT_OF_ORDER)
            }
        return if (problems.isEmpty()) ChapterCheck.Ready else ChapterCheck.NotChapters(problems)
    }

    /**
     * Expands the filename template and makes the result safe to write.
     *
     * A template can legitimately expand to a newline-laden track list or a title with a slash in
     * it, so this is the one place that has to assume the worst about its own input.
     */
    fun fileName(
        template: String,
        facts: PublishFacts,
        extension: String = DEFAULT_EXTENSION,
    ): String {
        val stem =
            expand(template, facts)
                .replace(UNSAFE_FOR_FILENAME, " ")
                .replace(RUNS_OF_SPACE, " ")
                .trim()
                .trim('.')
                .take(MAX_STEM_CHARS)
                .trim()
                .ifBlank { APP_NAME }
        val suffix = extension.trimStart('.')
        return if (suffix.isBlank()) stem else "$stem.$suffix"
    }

    /**
     * Puts [text] on the clipboard — the "one tap to copy" the publish sheet is built around.
     *
     * Returns false only when the platform refused; the caller should say so rather than leave a
     * button that silently did nothing.
     */
    fun copyToClipboard(
        context: Context,
        text: String,
        label: String = APP_NAME,
    ): Boolean {
        val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return false
        return runCatching { clipboard.setPrimaryClip(ClipData.newPlainText(label, text)) }.isSuccess
    }

    /**
     * True when the caller still has to confirm the copy itself. From Android 13 the system shows
     * its own clipboard toast, and adding a second one just says it twice.
     */
    val needsCopyConfirmation: Boolean
        get() = Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU

    private fun endOfLastTrack(
        track: PublishTrack,
        facts: PublishFacts,
    ): Long =
        when {
            facts.durationMs > 0L -> facts.durationMs
            track.durationMs > 0L -> track.startMs + track.durationMs
            // Nothing says where the video ends, so the last chapter's length is unknowable and is
            // left unjudged rather than guessed at.
            else -> 0L
        }

    private const val MILLIS_PER_SECOND = 1000L
    private const val SECONDS_PER_MINUTE = 60L
    private const val SECONDS_PER_HOUR = 3600L
    private const val MIN_CHAPTERS = 3
    private const val MIN_CHAPTER_MS = 10_000L
    private const val UNTITLED = "Untitled"
    private const val DEFAULT_EXTENSION = "mp4"

    /** Leaves room for the extension inside the 255-byte limit even when the title is multi-byte. */
    private const val MAX_STEM_CHARS = 100

    private val TOKEN_SHAPE = Regex("\\{[A-Za-z0-9_]+}")
    private val UNSAFE_FOR_FILENAME = Regex("[\\\\/:*?\"<>|\\x00-\\x1F]")
    private val RUNS_OF_SPACE = Regex("\\s+")
}

private const val APP_NAME = "Geode"

private const val DEFAULT_DESCRIPTION =
    "{title} — {artist}\n" +
        "\n" +
        "{chapters}\n" +
        "\n" +
        "Visuals: {scene}, rendered in {app}."

private const val DEFAULT_FILE_NAME = "{artist} - {title}"
