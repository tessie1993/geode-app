package dev.geode.export

import android.content.Context
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
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.editor.AnimatableParams
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipTransition
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeSheet
import dev.geode.editor.Lane
import dev.geode.editor.LaneKind
import dev.geode.editor.ParamId
import dev.geode.editor.SubtitleCue
import dev.geode.editor.Subtitles
import dev.geode.render.TransitionCatalog

/**
 * The editor project as one Media3 [Composition]: every media lane end to end (gaps closed) and
 * layered over the first, each clip's grade, transition and speed ramp on its own item, and every
 * unmuted audio lane mixed underneath. The persisted export defaults ([ExportDefaults]) drive the
 * output ratio and frame rate, and a loop-safe export trims the tail of the primary lane to a
 * whole frame so a looped playback doesn't hitch on a partial one.
 */
@UnstableApi
object ProjectComposition {
    sealed interface Outcome {
        data class Ready(
            val composition: Composition,
            val durationMs: Long,
        ) : Outcome

        data object NoVideo : Outcome
    }

    /**
     * What [buildWithReport] had to leave out of the composition: clip kinds a media lane cannot
     * carry (a project can end up with, for example, an [ClipContent.Overlay] clip sitting on a
     * Media lane after a lane-kind edit went astray) are skipped rather than thrown for, and their
     * kinds are collected here instead.
     */
    data class Report(
        val skippedClipKinds: Set<String> = emptySet(),
    ) {
        val hasSkips: Boolean get() = skippedClipKinds.isNotEmpty()

        /** A user-facing summary of [skippedClipKinds], or null when nothing was skipped. */
        fun message(context: Context): String? =
            if (skippedClipKinds.isEmpty()) {
                null
            } else {
                context.getString(R.string.editor_export_skipped_kinds, skippedClipKinds.sorted().joinToString(", "))
            }
    }

    /** [buildWithReport] without the report, for callers that only need the [Outcome]. */
    fun build(
        context: Context,
        project: EditorProject,
    ): Outcome = buildWithReport(context, project).first

    fun buildWithReport(
        context: Context,
        project: EditorProject,
        defaults: ExportDefaults = ExportPrefsStore(GeodePrefsFiles(context).general).load(),
    ): Pair<Outcome, Report> {
        val mediaLanes = project.timeline.lanes.filter { it.kind == LaneKind.Media && it.clips.any(Clip::enabled) }
        if (mediaLanes.isEmpty()) return Outcome.NoVideo to Report()

        val cues = Subtitles.cuesFrom(project.timeline.lanes)
        val skippedKinds = mutableSetOf<String>()
        val frameUs = 1_000_000L / defaults.fps
        // The lane that will actually seed the composition's reported duration: the first media
        // lane carrying playable (Video/Still) content, not just the first media lane by index,
        // since an earlier lane can hold nothing but content a media lane can't carry (see [Report]).
        val primaryLane = mediaLanes.firstOrNull { lane -> lane.clips.any(::isPlayable) } ?: mediaLanes.first()
        val rawDurationMs = primaryLane.clips.filter(::isPlayable).sumOf(Clip::durationMs)
        // A remainder shorter than one output frame, dropped from the primary lane's last playable
        // clip so a looped render doesn't repeat (or stutter on) a partial final frame.
        val trimTailMs = if (defaults.loopSafe) (rawDurationMs * 1000L % frameUs) / 1000L else 0L

        val videoSequences =
            mediaLanes.mapNotNull { lane ->
                videoSequenceFor(
                    context = context,
                    lane = lane,
                    cues = cues,
                    sheet = project.keyframes,
                    fps = defaults.fps,
                    trimTailMs = if (lane.id == primaryLane.id) trimTailMs else 0L,
                    skippedKinds = skippedKinds,
                )
            }
        if (videoSequences.isEmpty()) return Outcome.NoVideo to Report(skippedKinds)

        val audioSequences =
            project.timeline.lanes
                .filter { it.kind == LaneKind.Audio && !it.muted && it.clips.any(Clip::enabled) }
                .mapNotNull(::audioSequence)

        val durationMs = rawDurationMs - trimTailMs
        val composition =
            Composition
                .Builder(videoSequences + audioSequences)
                .setEffects(Effects(emptyList(), listOf(ratioEffect(defaults))))
                .build()
        return Outcome.Ready(composition, durationMs) to Report(skippedKinds)
    }

    /** The output aspect ratio from the persisted export defaults, cropping to fill rather than letterboxing. */
    private fun ratioEffect(defaults: ExportDefaults): Effect =
        Presentation.createForAspectRatio(
            defaults.ratio.wRatio.toFloat() / defaults.ratio.hRatio,
            Presentation.LAYOUT_SCALE_TO_FIT_WITH_CROP,
        )

    /** Whether a Media lane can actually carry this clip: enabled, and [ClipContent.Video] or [ClipContent.Still]. */
    private fun isPlayable(clip: Clip): Boolean = clip.enabled && (clip.content is ClipContent.Video || clip.content is ClipContent.Still)

    /** One media lane as a video sequence, or null if it has no playable clip once unsupported content is dropped. */
    private fun videoSequenceFor(
        context: Context,
        lane: Lane,
        cues: List<SubtitleCue>,
        sheet: KeyframeSheet,
        fps: Int,
        trimTailMs: Long,
        skippedKinds: MutableSet<String>,
    ): EditedMediaItemSequence? {
        val enabled = lane.clips.filter(Clip::enabled).sortedBy(Clip::startMs)
        enabled.filterNot(::isPlayable).forEach { skippedKinds += requireNotNull(it.content::class.simpleName) }
        // Trimmed after the unsupported clips are dropped, so a loop-safe trim always lands on the
        // clip that actually ends the rendered sequence, not on one that gets skipped anyway.
        val clips = trimLastClip(enabled.filter(::isPlayable), trimTailMs)
        if (clips.isEmpty()) return null
        val stores = clips.map { if (it.transition != null) TransitionFrameStore() else null }
        val video = EditedMediaItemSequence.Builder()
        clips.forEachIndexed { index, clip ->
            val incoming =
                stores[index]?.let { store ->
                    clip.transition?.let { transition -> transitionEffect(context, clip, transition, store) }
                }
            val outgoing = stores.getOrNull(index + 1)?.let(::TransitionCaptureEffect)
            val captions = captionsFor(clip, cues)
            video.addItem(videoItem(context, clip, lane, sheet, fps, listOfNotNull(incoming, outgoing), listOfNotNull(captions)))
        }
        return video.build()
    }

    /** Shortens the last clip in [clips] by [trimTailMs], or leaves the list untouched if there is nothing to trim. */
    private fun trimLastClip(
        clips: List<Clip>,
        trimTailMs: Long,
    ): List<Clip> {
        if (trimTailMs <= 0L) return clips
        val last = clips.lastOrNull() ?: return clips
        if (last.durationMs <= trimTailMs) return clips
        return clips.dropLast(1) + last.copy(durationMs = last.durationMs - trimTailMs)
    }

    /** Text-lane clips over this clip's span, as one overlay in the clip's own time. */
    private fun captionsFor(
        clip: Clip,
        cues: List<SubtitleCue>,
    ): Effect? {
        val local = TimedTextOverlay.forSpan(cues, clip.startMs, clip.endMs)
        return if (local.isEmpty()) null else OverlayEffect(listOf(TimedTextOverlay(local)))
    }

    /** Only ever called for a clip [isPlayable], so every branch a media lane can't carry is unreachable here. */
    private fun videoItem(
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
                error("not media lane content: $content")
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
