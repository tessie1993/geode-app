package dev.geode.export

import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.EditorProject
import dev.geode.editor.LaneKind
import dev.geode.editor.MIN_CLIP_DURATION_MS
import dev.geode.engine.bridge.GeodeNative

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
     * The segment [project] renders: the first enabled scene clip on the first shown visual lane, over the
     * first song on the first unmuted audio lane that it overlaps. Null when there is no visual, no song, or
     * less than a clip's minimum length of the two together. Native picks and times it (`core/output/RenderPlan`).
     */
    fun plan(project: EditorProject): SceneSegment? {
        val visuals = project.clipsOn(LaneKind.Visual)
        val songs = project.clipsOn(LaneKind.Audio)
        val out = LongArray(GeodeNative.RENDER_PLAN_LONGS)
        if (!GeodeNative.renderPlan(visuals.spans(), visuals.lanes(), songs.spans(), songs.lanes(), MIN_CLIP_DURATION_MS, out)) return null
        val clip = visuals[out[0].toInt()].third
        return SceneSegment(
            clip = clip,
            scene = clip.content as ClipContent.Scene,
            programmeStartMs = out[2],
            durationMs = out[3],
            songUri = (songs[out[1].toInt()].third.content as ClipContent.Audio).uri,
            songStartMs = out[4],
            range = if (out[5] != 0L) null else ExportRange(out[4], out[3]),
        )
    }

    /** Each clip on [kind] lanes as (its lane's index in the timeline, since lane order is "first"; PLAN_* flags; it). */
    private fun EditorProject.clipsOn(kind: LaneKind): List<Triple<Int, Int, Clip>> =
        timeline.lanes.withIndex().filter { it.value.kind == kind }.flatMap { (index, lane) ->
            lane.clips.map {
                val wanted = if (kind == LaneKind.Visual) it.content is ClipContent.Scene else it.content is ClipContent.Audio
                val flags = it.enabled.bit(GeodeNative.PLAN_ENABLED) or wanted.bit(GeodeNative.PLAN_WANTED)
                Triple(index, flags or (!lane.muted).bit(GeodeNative.PLAN_LANE_LIVE), it)
            }
        }

    private fun Boolean.bit(flag: Int): Int = if (this) flag else 0

    private fun List<Triple<Int, Int, Clip>>.spans(): LongArray =
        flatMap { it.third.run { listOf(startMs, endMs, sourceInMs, sourceDurationMs) } }.toLongArray()

    private fun List<Triple<Int, Int, Clip>>.lanes(): IntArray = flatMap { listOf(it.first, it.second) }.toIntArray()
}
