package dev.geode.editor

data class SubtitleCue(
    val startMs: Long,
    val endMs: Long,
    val text: String,
) {
    fun spans(atMs: Long): Boolean = atMs >= startMs && atMs < endMs
}

/** A project's text lanes as timed cues, for the render's caption overlay. */
object Subtitles {
    fun cuesFrom(lanes: List<Lane>): List<SubtitleCue> =
        lanes
            .filter { it.kind == LaneKind.Text && !it.muted }
            .flatMap { lane -> lane.clips.filter(Clip::enabled) }
            .mapNotNull { clip -> (clip.content as? ClipContent.Text)?.let { SubtitleCue(clip.startMs, clip.endMs, it.text) } }
            .sortedBy { it.startMs }
}
