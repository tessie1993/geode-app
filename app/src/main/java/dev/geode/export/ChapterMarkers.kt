package dev.geode.export

import android.net.Uri
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

    data class Failed(
        val message: String,
    ) : ChapterWriteResult
}

data class ChapterMarkers(
    val chapters: List<Chapter>,
) {
    val isEmpty: Boolean get() = chapters.isEmpty()

    val durationMs: Long get() = chapters.lastOrNull()?.endMs ?: 0L

    /**
     * True when the marker list satisfies the rules video platforms impose before they will show
     * chapters at all: a first chapter at zero, at least three of them, none shorter than ten
     * seconds. The UI can warn instead of the user finding out after the upload.
     */
    val platformReady: Boolean
        get() =
            chapters.size >= MIN_PLATFORM_CHAPTERS &&
                chapters.first().startMs == 0L &&
                chapters.all { it.durationMs >= MIN_PLATFORM_CHAPTER_MS }

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
            ChapterWriteResult.Failed(e.message ?: "The chapter list could not be written.")
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

        const val MIN_PLATFORM_CHAPTERS: Int = 3
        const val MIN_PLATFORM_CHAPTER_MS: Long = 10_000

        /**
         * Turns the clips of a finished soundtrack into markers.
         *
         * A single clip yields no markers: one track is not a mix, and a lone "0:00" chapter is
         * noise. Blank titles fall back to the clip's position rather than to the file name,
         * which is often a hash.
         */
        fun of(spans: List<MixClipSpan>): ChapterMarkers {
            if (spans.size < 2) return None
            val chapters =
                spans.mapIndexed { index, span ->
                    Chapter(
                        startMs = span.startMs,
                        durationMs = span.durationMs,
                        title = span.clip.title.trim().ifEmpty { "Track ${index + 1}" },
                    )
                }
            return ChapterMarkers(chapters)
        }

        fun sidecarName(
            videoFileName: String,
            format: ChapterFormat,
        ): String = "${videoFileName.substringBeforeLast('.', videoFileName)}.${format.extension}"

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
    }
}
