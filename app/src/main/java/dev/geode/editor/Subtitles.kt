package dev.geode.editor

data class SubtitleCue(
    val startMs: Long,
    val endMs: Long,
    val text: String,
)

/**
 * SubRip in and out, and cues to and from a text lane. Which cue a rendered frame shows is native
 * (`core/signal/timeline/Cues`, through `export/TimedTextOverlay`).
 */
object Subtitles {
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
