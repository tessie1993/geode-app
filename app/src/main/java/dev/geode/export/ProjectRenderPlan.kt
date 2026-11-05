package dev.geode.export

import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.EditorProject
import dev.geode.editor.Lane
import dev.geode.editor.LaneKind
import dev.geode.editor.MIN_CLIP_DURATION_MS

/**
 * What a project render draws: one visual over one song (issue #232, narrowed when Studio dropped
 * video editing, #227 and #390). Every render is that: the one-tap Render builds it, and a stopped
 * take is saved as it (`StudioProject.fromTake`: the take as one visual clip over the song on track
 * time). The visualizer exporter (`OffscreenSceneRenderer`, through [VideoExporter]) draws it straight
 * to its destination, hearing the song under it, so an exported visual moves exactly as it did live.
 *
 * A project saved before multi-clip editing went (footage, captions, several lanes or clips, from
 * `EditorProjectJson`) renders the same way, as its first visual clip over the song under it; the
 * rest of it is not drawn. Nothing in the app renders such a project today (a take's render is its
 * own fresh project), so this keeps the old document readable without keeping a second render path.
 */
object ProjectRenderPlan {
    /**
     * One stretch of a scene clip over one stretch of song: programme time [programmeStartMs] for
     * [durationMs], hearing [songUri] from [songStartMs]. [range] is what the visualizer exporter is
     * asked for: null when the stretch is the whole song from its start (the exporter then reads the
     * song's own length, which a project may not know).
     */
    data class SceneSegment(
        val clip: Clip,
        val scene: ClipContent.Scene,
        val programmeStartMs: Long,
        val durationMs: Long,
        val songUri: String,
        val songStartMs: Long,
        val range: ExportRange?,
    )

    /**
     * The segment [project] renders: the first enabled scene clip on the first shown visual lane,
     * over the first song on the first unmuted audio lane that it overlaps. Null when there is no
     * visual, no song, or less than a clip's minimum length of the two together. Pure: no file, no GL.
     */
    fun plan(project: EditorProject): SceneSegment? {
        val lanes = project.timeline.lanes
        val visual = lanes.firstOrNull { it.kind == LaneKind.Visual && !it.muted && it.firstScene() != null } ?: return null
        val clip = visual.firstScene() ?: return null
        val scene = clip.content as ClipContent.Scene
        val songLane = lanes.firstOrNull { it.kind == LaneKind.Audio && !it.muted && it.clips.any(Clip::enabled) } ?: return null
        val song =
            songLane.clips
                .filter { it.enabled && it.content is ClipContent.Audio && it.overlaps(clip) }
                .minByOrNull(Clip::startMs) ?: return null
        val start = maxOf(clip.startMs, song.startMs)
        val end = minOf(clip.endMs, song.endMs)
        val duration = end - start
        if (duration < MIN_CLIP_DURATION_MS) return null
        val songStart = (song.sourceInMs + (start - song.startMs)).coerceAtLeast(0L)
        val wholeSong =
            songStart == 0L &&
                start == song.startMs &&
                end == song.endMs &&
                song.hasBoundedSource &&
                song.durationMs >= song.sourceDurationMs
        return SceneSegment(
            clip = clip,
            scene = scene,
            programmeStartMs = start,
            durationMs = duration,
            songUri = (song.content as ClipContent.Audio).uri,
            songStartMs = songStart,
            range = if (wholeSong) null else ExportRange(songStart, duration),
        )
    }

    private fun Lane.firstScene(): Clip? = clips.filter { it.enabled && it.content is ClipContent.Scene }.minByOrNull(Clip::startMs)
}
