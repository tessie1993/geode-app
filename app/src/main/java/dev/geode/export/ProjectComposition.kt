package dev.geode.export

import android.content.Context
import android.net.Uri
import androidx.annotation.PluralsRes
import androidx.media3.common.Effect
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.OverlayEffect
import androidx.media3.effect.Presentation
import androidx.media3.transformer.Composition
import androidx.media3.transformer.EditedMediaItem
import androidx.media3.transformer.EditedMediaItemSequence
import androidx.media3.transformer.Effects
import dev.geode.R
import dev.geode.data.ExportDefaults
import dev.geode.editor.AnimatableParams
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipId
import dev.geode.editor.ClipTransition
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeSheet
import dev.geode.editor.Lane
import dev.geode.editor.LaneKind
import dev.geode.editor.MIN_CLIP_DURATION_MS
import dev.geode.editor.ParamId
import dev.geode.editor.SubtitleCue
import dev.geode.editor.Subtitles
import dev.geode.render.TransitionCatalog

/**
 * The one render path's two halves (issue #232): [plan] decides how every clip of a project
 * renders, and [compose] turns a plan whose visual clips are already drawn into one Media3
 * [Composition].
 *
 * Every render renders a project. A Scene clip (a look, a take, a beat cut) is drawn by the same
 * offscreen GL renderer the visualizer export uses (`OffscreenSceneRenderer`, through
 * [VideoExporter]), hearing the song under it, so an exported visual moves exactly as it did live.
 * That drawing happens before [compose]: each [SceneSegment] becomes a scratch MP4 and enters the
 * composition as a plain video item, layered with footage like any other.
 *
 * A project that is nothing but one visual over one stretch of song ([RenderPlan.direct], which is
 * what the one-tap Render builds) skips the composition: the visualizer exporter writes it straight
 * to its destination, with its loudness readout, background image and watermark.
 *
 * Nothing is dropped without a word. A clip the path cannot draw yet is a [ClipRender.Skipped] with
 * a [SkipReason], and [RenderPlan.report] is what the user is told.
 *
 * Lanes play end to end (gaps closed) and later lanes layer over the first, each clip's grade,
 * transition and speed ramp on its own item, every unmuted audio lane mixed underneath. The
 * project's export settings ([ExportDefaults]) drive the output ratio and frame rate, and a
 * loop-safe export trims the tail of the primary lane to a whole frame so a looped playback doesn't
 * hitch on a partial one.
 */
@UnstableApi
object ProjectComposition {
    /** Why a clip is not in the render. [label] is the user-facing phrase for the report. */
    enum class SkipReason(
        @PluralsRes val label: Int,
    ) {
        /** Overlay clips draw once overlays are timeline elements (part 2E). */
        OVERLAY_NOT_RENDERED(R.plurals.render_skip_overlay),

        /** A visual hears its song; with no unmuted audio under it there is nothing to react to. */
        NO_SONG(R.plurals.render_skip_no_song),

        /** Part of a visual clip ran past the song under it; the part with a song still renders. */
        PAST_SONG_END(R.plurals.render_skip_past_song),

        /** Content on a lane that cannot carry it (a video on a visual lane, say). */
        WRONG_LANE(R.plurals.render_skip_wrong_lane),
    }

    /** How one enabled clip renders. */
    sealed interface ClipRender {
        val clip: Clip

        /** A video or still, decoded and graded by Transformer. */
        data class Footage(
            override val clip: Clip,
        ) : ClipRender

        /** A scene clip, drawn offscreen over the song in one or more [segments]. */
        data class Offscreen(
            override val clip: Clip,
            val scene: ClipContent.Scene,
            val segments: List<SceneSegment>,
        ) : ClipRender

        /** A text clip, burned into the video under it as a caption. */
        data class Caption(
            override val clip: Clip,
        ) : ClipRender

        /** An audio clip, mixed under the picture. */
        data class Mix(
            override val clip: Clip,
        ) : ClipRender

        data class Skipped(
            override val clip: Clip,
            val reason: SkipReason,
        ) : ClipRender
    }

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

    data class RenderPlan(
        val clips: List<ClipRender>,
        /** Clips partly drawn: the reason the rest of them is missing. */
        val partial: List<Pair<Clip, SkipReason>> = emptyList(),
    ) {
        val segments: List<SceneSegment> get() = clips.filterIsInstance<ClipRender.Offscreen>().flatMap { it.segments }

        /**
         * The single segment a one-visual project is, when the visualizer exporter can write the
         * whole render: no footage, no captions, one scene stretch, and the song under it is all the
         * audio there is.
         */
        val direct: SceneSegment?
            get() {
                if (clips.any { it is ClipRender.Footage || it is ClipRender.Caption }) return null
                val only = segments.singleOrNull() ?: return null
                val mixes = clips.filterIsInstance<ClipRender.Mix>()
                val song = mixes.singleOrNull()?.clip ?: return null
                val sameSpan = song.startMs == only.programmeStartMs && song.durationMs == only.durationMs
                return only.takeIf { sameSpan && (song.content as ClipContent.Audio).uri == it.songUri }
            }

        val report: Report
            get() =
                Report(
                    (clips.filterIsInstance<ClipRender.Skipped>().map { it.reason } + partial.map { it.second })
                        .groupingBy { it }
                        .eachCount(),
                )
    }

    /** What the render had to leave out, by reason and count of clips. */
    data class Report(
        val skipped: Map<SkipReason, Int> = emptyMap(),
    ) {
        /** A user-facing summary, or null when nothing was skipped. */
        fun message(context: Context): String? =
            if (skipped.isEmpty()) {
                null
            } else {
                val parts =
                    skipped.entries
                        .sortedBy { it.key.ordinal }
                        .map { (reason, n) -> context.resources.getQuantityString(reason.label, n, n) }
                context.getString(R.string.editor_export_skipped_kinds, parts.joinToString(", "))
            }
    }

    sealed interface Outcome {
        data class Ready(
            val composition: Composition,
            val durationMs: Long,
        ) : Outcome

        data object NoVideo : Outcome
    }

    /**
     * How every enabled clip of [project] renders. Pure: no file, no GL, so the decision can be read
     * (and shown) before anything is drawn. Hidden (muted) visual and text lanes draw nothing and are
     * not reported; muted audio lanes are silent the same way.
     */
    fun plan(project: EditorProject): RenderPlan {
        val lanes = project.timeline.lanes
        val songLane = lanes.firstOrNull { it.kind == LaneKind.Audio && !it.muted && it.clips.any(Clip::enabled) }
        val partial = mutableListOf<Pair<Clip, SkipReason>>()
        val clips =
            lanes.flatMap { lane ->
                if (lane.muted && lane.kind != LaneKind.Media) return@flatMap emptyList()
                lane.clips
                    .filter(Clip::enabled)
                    .sortedBy(Clip::startMs)
                    .map { clip -> renderFor(clip, lane, songLane, partial) }
            }
        return RenderPlan(clips, partial)
    }

    private fun renderFor(
        clip: Clip,
        lane: Lane,
        songLane: Lane?,
        partial: MutableList<Pair<Clip, SkipReason>>,
    ): ClipRender {
        val content = clip.content
        return when {
            content is ClipContent.Overlay -> ClipRender.Skipped(clip, SkipReason.OVERLAY_NOT_RENDERED)
            content.laneKind != lane.kind -> ClipRender.Skipped(clip, SkipReason.WRONG_LANE)
            content is ClipContent.Video || content is ClipContent.Still -> ClipRender.Footage(clip)
            content is ClipContent.Text -> ClipRender.Caption(clip)
            content is ClipContent.Audio -> ClipRender.Mix(clip)
            content is ClipContent.Scene -> {
                val segments = segmentsFor(clip, content, songLane)
                val covered = segments.sumOf(SceneSegment::durationMs)
                when {
                    segments.isEmpty() -> ClipRender.Skipped(clip, SkipReason.NO_SONG)
                    else -> {
                        if (clip.durationMs - covered >= MIN_CLIP_DURATION_MS) partial += clip to SkipReason.PAST_SONG_END
                        ClipRender.Offscreen(clip, content, segments)
                    }
                }
            }
            else -> ClipRender.Skipped(clip, SkipReason.WRONG_LANE)
        }
    }

    /**
     * The stretches of [clip] that have a song under them on [songLane], the first unmuted audio
     * lane: one per audio clip it overlaps, each hearing that clip's source from the matching point.
     */
    private fun segmentsFor(
        clip: Clip,
        scene: ClipContent.Scene,
        songLane: Lane?,
    ): List<SceneSegment> {
        songLane ?: return emptyList()
        return songLane.clips
            .filter { it.enabled && it.content is ClipContent.Audio && it.overlaps(clip) }
            .sortedBy(Clip::startMs)
            .mapNotNull { song ->
                val start = maxOf(clip.startMs, song.startMs)
                val end = minOf(clip.endMs, song.endMs)
                val duration = end - start
                if (duration < MIN_CLIP_DURATION_MS) return@mapNotNull null
                val songStart = (song.sourceInMs + (start - song.startMs)).coerceAtLeast(0L)
                val wholeSong =
                    songStart == 0L &&
                        start == song.startMs &&
                        end == song.endMs &&
                        song.hasBoundedSource &&
                        song.durationMs >= song.sourceDurationMs
                SceneSegment(
                    clip = clip,
                    scene = scene,
                    programmeStartMs = start,
                    durationMs = duration,
                    songUri = (song.content as ClipContent.Audio).uri,
                    songStartMs = songStart,
                    range = if (wholeSong) null else ExportRange(songStart, duration),
                )
            }
    }

    /**
     * [project] as one Media3 composition, every scene segment of [plan] taken from [drawn] (the
     * scratch file it was rendered to). A segment missing from [drawn] is left out, as is the whole
     * composition when no lane has anything to show ([Outcome.NoVideo]).
     */
    fun compose(
        context: Context,
        project: EditorProject,
        plan: RenderPlan,
        drawn: Map<SceneSegment, Uri>,
        defaults: ExportDefaults,
    ): Outcome {
        val cues = Subtitles.cuesFrom(project.timeline.lanes)
        val renders = plan.clips.associateBy { it.clip.id }
        val pictureLanes =
            project.timeline.lanes
                .filter { it.kind == LaneKind.Media || (it.kind == LaneKind.Visual && !it.muted) }
                .map { lane -> lane to itemsFor(lane, renders, drawn) }
                .filter { (_, items) -> items.isNotEmpty() }
        if (pictureLanes.isEmpty()) return Outcome.NoVideo

        val frameUs = 1_000_000L / defaults.fps
        val primary = pictureLanes.first().second
        val rawDurationMs = primary.sumOf(LaneItem::durationMs)
        // A remainder shorter than one output frame, dropped from the primary lane's last item so a
        // looped render doesn't repeat (or stutter on) a partial final frame. Drawn scenes are whole
        // frames already, so only footage is trimmed.
        val trimTailMs =
            if (defaults.loopSafe && primary.last().footage != null) (rawDurationMs * 1000L % frameUs) / 1000L else 0L

        val videoSequences =
            pictureLanes.mapIndexed { index, (lane, items) ->
                videoSequenceFor(
                    context = context,
                    lane = lane,
                    items = if (index == 0) trimLast(items, trimTailMs) else items,
                    cues = cues,
                    sheet = project.keyframes,
                    fps = defaults.fps,
                )
            }
        val audioSequences =
            project.timeline.lanes
                .filter { it.kind == LaneKind.Audio && !it.muted && it.clips.any(Clip::enabled) }
                .mapNotNull(::audioSequence)

        val composition =
            Composition
                .Builder(videoSequences + audioSequences)
                .setEffects(Effects(emptyList(), listOf(ratioEffect(defaults))))
                .build()
        // The render runs as long as its longest picture lane, which need not be the primary one.
        val longestMs = pictureLanes.drop(1).maxOfOrNull { (_, items) -> items.sumOf(LaneItem::durationMs) } ?: 0L
        return Outcome.Ready(composition, maxOf(rawDurationMs - trimTailMs, longestMs))
    }

    /**
     * One picture on a lane: a footage clip, or one drawn segment of a scene clip. [first] marks the
     * item that carries its clip's incoming transition.
     */
    private data class LaneItem(
        val clip: Clip,
        val startMs: Long,
        val durationMs: Long,
        val footage: Clip? = null,
        val drawn: Uri? = null,
        val first: Boolean = true,
    )

    private fun itemsFor(
        lane: Lane,
        renders: Map<ClipId, ClipRender>,
        drawn: Map<SceneSegment, Uri>,
    ): List<LaneItem> =
        lane.clips.sortedBy(Clip::startMs).flatMap { clip ->
            when (val render = renders[clip.id]) {
                is ClipRender.Footage -> listOf(LaneItem(clip, clip.startMs, clip.durationMs, footage = clip))
                is ClipRender.Offscreen ->
                    render.segments
                        .mapNotNull { segment -> drawn[segment]?.let { segment to it } }
                        .mapIndexed { i, (segment, uri) ->
                            LaneItem(clip, segment.programmeStartMs, segment.durationMs, drawn = uri, first = i == 0)
                        }
                else -> emptyList()
            }
        }

    /** Shortens the last item by [trimTailMs] (footage only; see [compose]). */
    private fun trimLast(
        items: List<LaneItem>,
        trimTailMs: Long,
    ): List<LaneItem> {
        if (trimTailMs <= 0L) return items
        val last = items.last()
        val footage = last.footage ?: return items
        if (last.durationMs <= trimTailMs) return items
        val trimmed = footage.copy(durationMs = footage.durationMs - trimTailMs)
        return items.dropLast(1) + last.copy(durationMs = trimmed.durationMs, footage = trimmed)
    }

    /** The output aspect ratio from the export settings, cropping to fill rather than letterboxing. */
    private fun ratioEffect(defaults: ExportDefaults): Effect =
        Presentation.createForAspectRatio(
            defaults.ratio.wRatio.toFloat() / defaults.ratio.hRatio,
            Presentation.LAYOUT_SCALE_TO_FIT_WITH_CROP,
        )

    private fun videoSequenceFor(
        context: Context,
        lane: Lane,
        items: List<LaneItem>,
        cues: List<SubtitleCue>,
        sheet: KeyframeSheet,
        fps: Int,
    ): EditedMediaItemSequence {
        val stores = items.map { if (it.first && it.clip.transition != null) TransitionFrameStore() else null }
        val video = EditedMediaItemSequence.Builder()
        items.forEachIndexed { index, item ->
            val incoming =
                stores[index]?.let { store ->
                    item.clip.transition?.let { transition -> transitionEffect(context, item.clip, transition, store) }
                }
            val outgoing = stores.getOrNull(index + 1)?.let(::TransitionCaptureEffect)
            val captions = captionsFor(item.startMs, item.startMs + item.durationMs, cues)
            val boundary = listOfNotNull(incoming, outgoing)
            val footage = item.footage
            video.addItem(
                if (footage != null) {
                    footageItem(context, footage, lane, sheet, fps, boundary, listOfNotNull(captions))
                } else {
                    drawnItem(requireNotNull(item.drawn), item.durationMs, fps, boundary + listOfNotNull(captions))
                },
            )
        }
        return video.build()
    }

    /** A scene segment already drawn to [uri]; its song is on the audio lane, so its own copy is dropped. */
    private fun drawnItem(
        uri: Uri,
        durationMs: Long,
        fps: Int,
        effects: List<Effect>,
    ): EditedMediaItem =
        EditedMediaItem
            .Builder(
                MediaItem
                    .Builder()
                    .setUri(uri)
                    .setClippingConfiguration(MediaItem.ClippingConfiguration.Builder().setEndPositionMs(durationMs).build())
                    .build(),
            ).setRemoveAudio(true)
            .setFrameRate(fps)
            .setEffects(Effects(emptyList(), effects))
            .build()

    /** Text-lane clips over a span, as one overlay in the item's own time. */
    private fun captionsFor(
        startMs: Long,
        endMs: Long,
        cues: List<SubtitleCue>,
    ): Effect? {
        val local = TimedTextOverlay.forSpan(cues, startMs, endMs)
        return if (local.isEmpty()) null else OverlayEffect(listOf(TimedTextOverlay(local)))
    }

    /** Only ever called for a [ClipRender.Footage] clip, so every other content kind is unreachable here. */
    private fun footageItem(
        context: Context,
        clip: Clip,
        lane: Lane,
        sheet: KeyframeSheet,
        fps: Int,
        boundary: List<Effect>,
        captions: List<Effect>,
    ): EditedMediaItem =
        when (val content = clip.content) {
            is ClipContent.Video -> videoItem(context, clip, content, lane, sheet, fps, boundary, captions)
            is ClipContent.Still ->
                EditedMediaItem
                    .Builder(MediaItem.fromUri(content.uri))
                    .setDurationUs(clip.durationMs * 1000L)
                    .setFrameRate(fps)
                    .setEffects(Effects(emptyList(), boundary + captions))
                    .build()
            is ClipContent.Scene, is ClipContent.Text, is ClipContent.Overlay, is ClipContent.Audio ->
                error("not footage: $content")
        }

    private fun videoItem(
        context: Context,
        clip: Clip,
        content: ClipContent.Video,
        lane: Lane,
        sheet: KeyframeSheet,
        fps: Int,
        boundary: List<Effect>,
        captions: List<Effect>,
    ): EditedMediaItem {
        val edit = content.edit
        val lut = edit.lutUri?.let { CubeLut.load(context, it) }
        val tracks = sheet.tracksFor(clip.id).filter { it.enabled && !it.isEmpty }
        val ramp = tracks.firstOrNull { it.paramId == SPEED }?.let { SpeedRamp.fromTrack(it, clip) }
        val graded = tracks.any { it.paramId != SPEED }
        val sourceSpanMs = ramp?.let { it.sourceSpanUs / 1000L } ?: (clip.durationMs * edit.speed).toLong()
        val sourceOutMs =
            (clip.sourceInMs + sourceSpanMs).let { if (clip.hasBoundedSource) it.coerceAtMost(clip.sourceDurationMs) else it }
        val item =
            MediaItem
                .Builder()
                .setUri(content.uri)
                .setClippingConfiguration(
                    MediaItem.ClippingConfiguration
                        .Builder()
                        .setStartPositionMs(clip.sourceInMs.coerceAtLeast(0L))
                        .apply { if (sourceOutMs > clip.sourceInMs) setEndPositionMs(sourceOutMs) }
                        .build(),
                ).build()
        val grade = if (graded) keyframedGrade(clip, edit, sheet, lut) else edit.gradeEffects(lut)
        return EditedMediaItem
            .Builder(item)
            .setRemoveAudio(edit.mute || lane.muted)
            .setFrameRate(fps)
            .setEffects(Effects(emptyList(), grade + boundary + listOfNotNull(edit.captionEffect()) + captions))
            .apply { (ramp ?: edit.speedProvider())?.let { setSpeed(it) } }
            .build()
    }

    /**
     * The static chain with brightness/contrast/saturation/hue/rotation swapped for their per-frame
     * forms, evaluated against the clip's own source-in time rather than whatever frame the decoder
     * happens to present first (which, for a clip trimmed into its source, can be a warm-up frame
     * from before the clip's actual start).
     */
    private fun keyframedGrade(
        clip: Clip,
        edit: ClipEdit,
        sheet: KeyframeSheet,
        lut: CubeLut?,
    ): List<Effect> {
        val editAt: (Long) -> ClipEdit = { ms -> AnimatableParams.applyToClip(edit, sheet.valuesAt(clip.startMs + ms, clip.id)) }
        val statics = edit.copy(brightness = 0f, contrast = 0f, saturation = 0f, hueDegrees = 0f, rotationDegrees = 0f).gradeEffects(lut)
        val sourceInUs = clip.sourceInMs.coerceAtLeast(0L) * 1000L
        return listOf(KeyframedGrade(sourceInUs, editAt), KeyframedRotation(sourceInUs) { ms -> editAt(ms).rotationDegrees }) + statics
    }

    private fun transitionEffect(
        context: Context,
        clip: Clip,
        transition: ClipTransition,
        store: TransitionFrameStore,
    ): Effect? {
        val def = TransitionCatalog.definition(context, transition.id) ?: return null
        val durationMs = transition.boundedDurationMs.coerceAtMost(clip.durationMs)
        return GlTransitionEffect(def, durationMs * 1000L, store)
    }

    private fun audioSequence(lane: Lane): EditedMediaItemSequence? {
        val builder = EditedMediaItemSequence.Builder()
        var added = 0
        lane.clips.filter(Clip::enabled).sortedBy(Clip::startMs).forEach { clip ->
            val uri = (clip.content as? ClipContent.Audio)?.uri ?: return@forEach
            val item =
                MediaItem
                    .Builder()
                    .setUri(uri)
                    .setClippingConfiguration(
                        MediaItem.ClippingConfiguration
                            .Builder()
                            .setStartPositionMs(clip.sourceInMs.coerceAtLeast(0L))
                            .setEndPositionMs(clip.sourceInMs + clip.durationMs)
                            .build(),
                    ).build()
            builder.addItem(EditedMediaItem.Builder(item).setRemoveVideo(true).build())
            added++
        }
        return if (added > 0) builder.build() else null
    }

    private val SPEED = ParamId("clip.speed")
}
