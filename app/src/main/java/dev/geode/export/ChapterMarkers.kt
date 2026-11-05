package dev.geode.export

import android.net.Uri
import dev.geode.editor.Marker
import dev.geode.editor.MarkerSet
import dev.geode.engine.bridge.GeodeNative
import java.io.IOException
import java.io.OutputStream
import java.util.Locale

/**
 * One audio clip the user dropped into a long-form soundtrack, in the order they dropped it.
 *
 * A mix is whatever the user assembled; nothing here is detected from the audio. Chapter
 * boundaries come from these drops and from where each clip actually landed once muxed, so a
 * chapter always points at the start of a track the user chose, never at a silence or a section
 * change some analyser guessed at.
 */
data class MixClip(
    val uri: Uri,
    val title: String,
)

/** Where a [clip] ended up in the finished soundtrack, measured after it was written. */
data class MixClipSpan(
    val clip: MixClip,
    val startMs: Long,
    val durationMs: Long,
) {
    init {
        require(startMs >= 0) { "startMs must not be negative, was $startMs" }
        require(durationMs >= 0) { "durationMs must not be negative, was $durationMs" }
    }

    val endMs: Long get() = startMs + durationMs
}

data class Chapter(
    val startMs: Long,
    val durationMs: Long,
    val title: String,
) {
    val endMs: Long get() = startMs + durationMs
}

/**
 * Sidecar formats for chapter markers.
 *
 * MP4 chapter tracks are not writable through [android.media.MediaMuxer] — it exposes video,
 * audio and metadata tracks, but no QuickTime `chap` reference or Nero chapter atom — so the
 * markers ship beside the video instead of inside it. [DESCRIPTION] is what a listener pastes
 * into a video description; [WEB_VTT] and [FFMETADATA] are what tools read back.
 */
enum class ChapterFormat(
    val extension: String,
    val mimeType: String,
) {
    DESCRIPTION("txt", "text/plain"),
    WEB_VTT("vtt", "text/vtt"),
    FFMETADATA("ffmeta", "text/plain"),
}

sealed interface ChapterWriteResult {
    data object Written : ChapterWriteResult

    /** Nothing to write: a soundtrack with one clip has no boundaries to mark. */
    data object Skipped : ChapterWriteResult

    /** [message] is the stream's own reason, or null when it gave none. */
    data class Failed(
        val message: String?,
    ) : ChapterWriteResult {
        fun reason(fallback: String): String = message ?: fallback
    }
}

data class ChapterMarkers(
    val chapters: List<Chapter>,
) {
    val isEmpty: Boolean get() = chapters.isEmpty()

    val durationMs: Long get() = chapters.lastOrNull()?.endMs ?: 0L

    fun at(positionMs: Long): Chapter? = chapters.lastOrNull { it.startMs <= positionMs }

    fun text(format: ChapterFormat): String =
        when (format) {
            ChapterFormat.DESCRIPTION -> descriptionText()
            ChapterFormat.WEB_VTT -> webVttText()
            ChapterFormat.FFMETADATA -> ffMetadataText()
        }

    fun writeTo(
        out: OutputStream,
        format: ChapterFormat,
    ): ChapterWriteResult {
        if (isEmpty) return ChapterWriteResult.Skipped
        return try {
            out.write(text(format).toByteArray(Charsets.UTF_8))
            out.flush()
            ChapterWriteResult.Written
        } catch (e: IOException) {
            ChapterWriteResult.Failed(e.message)
        }
    }

    private fun descriptionText(): String =
        chapters.joinToString(separator = "\n", postfix = "\n") { "${timestamp(it.startMs)} ${it.title}" }

    private fun webVttText(): String =
        buildString {
            append("WEBVTT\n\n")
            chapters.forEachIndexed { index, chapter ->
                append(index + 1).append('\n')
                append(vttTimestamp(chapter.startMs)).append(" --> ").append(vttTimestamp(chapter.endMs)).append('\n')
                append(chapter.title).append("\n\n")
            }
        }

    private fun ffMetadataText(): String =
        buildString {
            append(";FFMETADATA1\n")
            chapters.forEach { chapter ->
                append("\n[CHAPTER]\nTIMEBASE=1/1000\n")
                append("START=").append(chapter.startMs).append('\n')
                append("END=").append(chapter.endMs).append('\n')
                append("title=").append(chapter.title.replace("\n", " ")).append('\n')
            }
        }

    companion object {
        val None: ChapterMarkers = ChapterMarkers(emptyList())

        /**
         * Turns the clips of a finished soundtrack into markers.
         *
         * A single clip yields no markers: one track is not a mix, and a lone "0:00" chapter is
         * noise. Blank titles fall back to the clip's position rather than to the file name,
         * which is often a hash. The spans are native (`core/output/Chapters`, tested by
         * `core/tests/output_test.cpp`); only the titles are picked here.
         */
        fun of(spans: List<MixClipSpan>): ChapterMarkers {
            if (spans.size < 2) return None
            val triples =
                GeodeNative.chaptersFromClips(
                    LongArray(spans.size) { spans[it].startMs },
                    LongArray(spans.size) { spans[it].durationMs },
                )
            return fromTriples(triples) { source ->
                spans[source]
                    .clip.title
                    .trim()
                    .ifEmpty { "Track ${source + 1}" }
            }
        }

        /**
         * Turns the editor's marker lane into chapters: each marker opens a chapter that runs
         * until the next marker, or until [totalDurationMs] for the last one. When the first marker
         * is after 0:00 a lead-in chapter named [leadInTitle] covers the opening, because players and
         * video sites ignore a chapter list that does not start at 0:00. The spans are native
         * (`core/output/Chapters`); the titles are [markerTitle].
         */
        fun of(
            markers: MarkerSet,
            totalDurationMs: Long,
            leadInTitle: String,
            untitled: (Int) -> String,
        ): ChapterMarkers {
            val ordered = markers.markers
            if (ordered.size < 2) return None
            val triples = GeodeNative.chaptersFromMarkers(LongArray(ordered.size) { ordered[it].atMs }, totalDurationMs)
            return fromTriples(triples) { source -> markerTitle(ordered, source, leadInTitle, untitled) }
        }

        /**
         * The title of the chapter that opens at marker [source] (0-based; [LEAD_IN] for the lead-in):
         * the marker's name, or [untitled] of its 1-based position when the name is blank, never an
         * empty title.
         */
        internal fun markerTitle(
            ordered: List<Marker>,
            source: Int,
            leadInTitle: String,
            untitled: (Int) -> String,
        ): String =
            if (source == LEAD_IN) {
                leadInTitle
            } else {
                ordered[source].name.trim().ifEmpty { untitled(source + 1) }
            }

        /** Native chapter spans, (start ms, duration ms, source) triples, with a title per source. */
        private fun fromTriples(
            triples: LongArray,
            title: (Int) -> String,
        ): ChapterMarkers =
            ChapterMarkers(
                List(triples.size / 3) { i ->
                    Chapter(startMs = triples[i * 3], durationMs = triples[i * 3 + 1], title = title(triples[i * 3 + 2].toInt()))
                },
            )

        /** `m:ss` under an hour, `h:mm:ss` over — the form description fields parse. */
        fun timestamp(ms: Long): String {
            val totalSeconds = (ms / 1000).coerceAtLeast(0)
            val hours = totalSeconds / 3600
            val minutes = (totalSeconds % 3600) / 60
            val seconds = totalSeconds % 60
            return if (hours > 0) {
                String.format(Locale.ROOT, "%d:%02d:%02d", hours, minutes, seconds)
            } else {
                String.format(Locale.ROOT, "%d:%02d", minutes, seconds)
            }
        }

        fun vttTimestamp(ms: Long): String {
            val clamped = ms.coerceAtLeast(0)
            val totalSeconds = clamped / 1000
            return String.format(
                Locale.ROOT,
                "%02d:%02d:%02d.%03d",
                totalSeconds / 3600,
                (totalSeconds % 3600) / 60,
                totalSeconds % 60,
                clamped % 1000,
            )
        }

        /** The source native gives the lead-in chapter it adds in front of a marker list. */
        internal const val LEAD_IN = -1
    }
}
