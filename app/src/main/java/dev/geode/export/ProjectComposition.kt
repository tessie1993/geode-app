package dev.geode.export

import android.content.Context
import androidx.media3.common.Effect
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.OverlayEffect
import androidx.media3.transformer.Composition
import androidx.media3.transformer.EditedMediaItem
import androidx.media3.transformer.EditedMediaItemSequence
import androidx.media3.transformer.Effects
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
 * The editor project as one Media3 [Composition]: the first media lane end to end (gaps closed),
 * each clip's grade, transition and speed ramp on its own item, and the first unmuted audio lane
 * as a second sequence mixed underneath.
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

    fun build(
        context: Context,
        project: EditorProject,
    ): Outcome {
        val videoLane = project.timeline.lanes.firstOrNull { it.kind == LaneKind.Media && it.clips.any(Clip::enabled) } ?: return Outcome.NoVideo
        val clips = videoLane.clips.filter(Clip::enabled).sortedBy(Clip::startMs)
        val stores = clips.map { if (it.transition != null) TransitionFrameStore() else null }
        val cues = Subtitles.cuesFrom(project.timeline.lanes)
        val video = EditedMediaItemSequence.Builder()
        clips.forEachIndexed { index, clip ->
            val incoming = stores[index]?.let { store -> clip.transition?.let { transition -> transitionEffect(context, clip, transition, store) } }
            val outgoing = stores.getOrNull(index + 1)?.let(::TransitionCaptureEffect)
            val captions = captionsFor(clip, cues)
            video.addItem(videoItem(context, clip, videoLane, project.keyframes, listOfNotNull(incoming, outgoing), listOfNotNull(captions)))
        }
        val sequences = listOfNotNull(video.build(), audioSequence(project))
        return Outcome.Ready(Composition.Builder(sequences).build(), clips.sumOf(Clip::durationMs))
    }

    /** Text-lane clips over this clip's span, as one overlay in the clip's own time. */
    private fun captionsFor(
        clip: Clip,
        cues: List<SubtitleCue>,
    ): Effect? {
        val local = TimedTextOverlay.forSpan(cues, clip.startMs, clip.endMs)
        return if (local.isEmpty()) null else OverlayEffect(listOf(TimedTextOverlay(local)))
    }

    private fun videoItem(
        context: Context,
        clip: Clip,
        lane: Lane,
        sheet: KeyframeSheet,
        boundary: List<Effect>,
        captions: List<Effect>,
    ): EditedMediaItem =
        when (val content = clip.content) {
            is ClipContent.Video -> videoItem(context, clip, content, lane, sheet, boundary, captions)
            is ClipContent.Still ->
                EditedMediaItem
                    .Builder(MediaItem.fromUri(content.uri))
                    .setDurationUs(clip.durationMs * 1000L)
                    .setFrameRate(STILL_FPS)
                    .setEffects(Effects(emptyList(), boundary + captions))
                    .build()
            is ClipContent.Scene, is ClipContent.Text, is ClipContent.Overlay, is ClipContent.Audio ->
                throw IllegalArgumentException("not media lane content: $content")
        }

    private fun videoItem(
        context: Context,
        clip: Clip,
        content: ClipContent.Video,
        lane: Lane,
        sheet: KeyframeSheet,
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
            .setEffects(Effects(emptyList(), grade + boundary + listOfNotNull(edit.captionEffect()) + captions))
            .apply { (ramp ?: edit.speedProvider())?.let { setSpeed(it) } }
            .build()
    }

    /** The static chain with brightness/contrast/saturation/hue/rotation swapped for their per-frame forms. */
    private fun keyframedGrade(
        clip: Clip,
        edit: ClipEdit,
        sheet: KeyframeSheet,
        lut: CubeLut?,
    ): List<Effect> {
        val editAt: (Long) -> ClipEdit = { ms -> AnimatableParams.applyToClip(edit, sheet.valuesAt(clip.startMs + ms, clip.id)) }
        val statics = edit.copy(brightness = 0f, contrast = 0f, saturation = 0f, hueDegrees = 0f, rotationDegrees = 0f).gradeEffects(lut)
        return listOf(KeyframedGrade(editAt), KeyframedRotation { ms -> editAt(ms).rotationDegrees }) + statics
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

    private fun audioSequence(project: EditorProject): EditedMediaItemSequence? {
        val lane =
            project.timeline.lanes.firstOrNull { it.kind == LaneKind.Audio && !it.muted && it.clips.any(Clip::enabled) }
                ?: return null
        val builder = EditedMediaItemSequence.Builder()
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
        }
        return builder.build()
    }

    private val SPEED = ParamId("clip.speed")
    private const val STILL_FPS = 30
}
