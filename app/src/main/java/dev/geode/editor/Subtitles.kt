package dev.geode.editor

import dev.geode.ui.LyricLine

data class SubtitleCue(
    val startMs: Long,
    val endMs: Long,
    val text: String,
) {
    fun spans(atMs: Long): Boolean = atMs >= startMs && atMs < endMs
}

/** SubRip in and out, and the two conversions the editor needs: lyric lines to cues, cues to and from a text lane. */
object Subtitles {
    const val MAX_LYRIC_CUE_MS: Long = 8_000L
    private const val MIN_CUE_MS: Long = MIN_CLIP_DURATION_MS

    fun parseSrt(text: String): List<SubtitleCue> =
        text
            .replace("\r\n", "\n")
            .split(Regex("\n\\s*\n"))
            .mapNotNull(::cueFromBlock)
            .sortedBy { it.startMs }

    private fun cueFromBlock(block: String): SubtitleCue? {
        val lines = block.lines().map(String::trim).filter(String::isNotEmpty)
        val timingIndex = lines.indexOfFirst { TIMING.containsMatchIn(it) }
        val match = lines.getOrNull(timingIndex)?.let { TIMING.find(it) } ?: return null
        val start = stampMs(match.groupValues, 1)
        val end = stampMs(match.groupValues, 5)
        val body = lines.drop(timingIndex + 1).joinToString("\n")
        return if (end > start && body.isNotBlank()) SubtitleCue(start, end, body) else null
    }

    fun toSrt(cues: List<SubtitleCue>): String =
        cues
            .sortedBy { it.startMs }
            .mapIndexed { index, cue -> "${index + 1}\n${stamp(cue.startMs)} --> ${stamp(cue.endMs)}\n${cue.text}\n" }
            .joinToString("\n")

    /** Each synced line shows until the next one starts, at most [MAX_LYRIC_CUE_MS]; the last runs to [endMs]. */
    fun fromLyrics(
        lines: List<LyricLine>,
        endMs: Long,
    ): List<SubtitleCue> {
        val timed = lines.filter { it.timeMs >= 0 && it.text.isNotBlank() }.sortedBy { it.timeMs }
        return timed.mapIndexedNotNull { index, line ->
            val next = timed.getOrNull(index + 1)?.timeMs ?: endMs
            val end = minOf(next, line.timeMs + MAX_LYRIC_CUE_MS)
            if (end - line.timeMs < MIN_CUE_MS) null else SubtitleCue(line.timeMs, end, line.text)
        }
    }

    fun cuesFrom(lanes: List<Lane>): List<SubtitleCue> =
        lanes
            .filter { it.kind == LaneKind.Text && !it.muted }
            .flatMap { lane -> lane.clips.filter(Clip::enabled) }
            .mapNotNull { clip -> (clip.content as? ClipContent.Text)?.let { SubtitleCue(clip.startMs, clip.endMs, it.text) } }
            .sortedBy { it.startMs }

    fun clipsFrom(
        cues: List<SubtitleCue>,
        idFor: () -> ClipId,
    ): List<Clip> =
        cues
            .filter { it.endMs - it.startMs >= MIN_CUE_MS }
            .map { Clip(idFor(), ClipContent.Text(it.text), startMs = it.startMs, durationMs = it.endMs - it.startMs) }

    private fun stampMs(
        groups: List<String>,
        first: Int,
    ): Long {
        val h = groups[first].toLong()
        val m = groups[first + 1].toLong()
        val s = groups[first + 2].toLong()
        val ms = groups[first + 3].padEnd(3, '0').take(3).toLong()
        return ((h * 60 + m) * 60 + s) * 1000 + ms
    }

    private fun stamp(ms: Long): String {
        val h = ms / 3_600_000
        val m = ms / 60_000 % 60
        val s = ms / 1000 % 60
        return "%02d:%02d:%02d,%03d".format(h, m, s, ms % 1000)
    }

    private val TIMING = Regex("""(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3})\s*-->\s*(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3})""")
}
