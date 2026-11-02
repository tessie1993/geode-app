package dev.geode.ui

import android.content.Context
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.SeekParameters
import dev.geode.audio.TapRenderersFactory
import dev.geode.data.PerformanceTake
import dev.geode.editor.AnimatableParams
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.EditorProject
import dev.geode.editor.LaneKind
import dev.geode.editor.OverlayBlend
import dev.geode.editor.sourceTimeAt
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import dev.geode.engine.audioandroid.PcmTap
import dev.geode.render.scene.SceneParams
import java.io.Closeable
import kotlin.math.abs

/**
 * What the Studio preview shows at one programme time: the topmost visual clip's style and look,
 * the topmost video clip and the frame of its source, every still, text and overlay on screen.
 */
internal data class PreviewFrame(
    val scene: Scene? = null,
    val video: Video? = null,
    val stills: List<Still> = emptyList(),
    val texts: List<String> = emptyList(),
    val overlays: List<Overlay> = emptyList(),
) {
    data class Scene(
        val sceneId: String,
        val params: SceneParams,
    )

    data class Video(
        val clip: Clip,
        val content: ClipContent.Video,
        val sourceMs: Long,
    )

    data class Still(
        val uri: String,
    )

    data class Overlay(
        val uri: String,
        val blend: OverlayBlend,
        val opacity: Float,
    )
}

/**
 * The Studio preview's engine room (wave 1, part 2B): it plays a project live, with sound.
 *
 * - **Sound.** The song lane (the first unmuted audio lane, as the render path hears it) plays
 *   through its own player whose PCM is tapped into a private [SampleRing]. [soundInput] is that
 *   ring, so the preview's GL view hears the project's song, not whatever the app is playing, and
 *   its visuals react to the audio lane exactly as a render's do.
 * - **Time.** [StudioPreviewClock] (native, ADR 0003) keeps programme time, runs through gaps in
 *   the song and is pulled toward the player's position; [tick] reads it once per frame and keeps
 *   the player on the span the clock is in, so a gap is silence and a clip after it starts on time.
 * - **Picture.** [frameAt] says what is on screen at a time; the composable draws it. The footage
 *   player is muted (the song lane is the sound) and follows the clock, re-seeking when it drifts.
 *
 * Main thread only, like the players. [close] it.
 */
@UnstableApi
internal class StudioPreviewController(
    context: Context,
) : Closeable {
    private val ring = SampleRing(RING_FRAMES, 2)

    @Volatile
    private var rateHz = DEFAULT_RATE_HZ

    /** What the preview's GL view hears: the song lane, through [ring]. */
    val soundInput = SoundInput(ring) { rateHz }

    private val tap = PcmTap(ring) { rateHz = it.sampleRateHz }

    private val song: ExoPlayer =
        ExoPlayer.Builder(context, TapRenderersFactory(context, tap)).build()

    /** The footage player; the composable gives it a surface. Muted: the song lane is the sound. */
    val footage: ExoPlayer =
        ExoPlayer.Builder(context).build().apply {
            volume = 0f
            setSeekParameters(SeekParameters.EXACT)
        }

    private val clock = StudioPreviewClock()

    private var project = EditorProject()
    private var songClips: List<Clip> = emptyList()
    private var takes: Map<String, PerformanceTake.Timeline> = emptyMap()
    private var footageClipId: String? = null
    private var fps = DEFAULT_FPS

    var playing: Boolean = false
        private set

    /** Takes the (new) project. Only a changed song lane reloads the song player. */
    fun bind(
        project: EditorProject,
        fps: Int,
        lengthMs: Long,
        takes: Map<String, PerformanceTake.Timeline>,
    ) {
        this.project = project
        this.takes = takes
        this.fps = fps
        val clips =
            project.timeline.lanes
                .firstOrNull { it.kind == LaneKind.Audio && !it.muted && it.clips.any(Clip::enabled) }
                ?.clips
                .orEmpty()
                .filter { it.enabled && it.content is ClipContent.Audio }
                .sortedBy(Clip::startMs)
        if (clips != songClips) {
            songClips = clips
            song.setMediaItems(clips.map(::songItem))
            song.prepare()
            clock.setSpans(clips.map { it.startMs to it.durationMs })
        }
        clock.setRate(fps, lengthMs)
    }

    fun play(fromMs: Long) {
        playing = true
        clock.play(fromMs)
        ring.beginEpoch()
        follow(fromMs, seek = true)
    }

    fun pause() {
        playing = false
        clock.pause()
        song.playWhenReady = false
        footage.playWhenReady = false
    }

    /** Moves to [programmeMs] (a scrub, or a jump while playing); returns the frame shown there. */
    fun seek(programmeMs: Long): Long {
        val frame = StudioPreviewClock.frameOf(programmeMs, fps)
        clock.seek(frame)
        if (playing) {
            ring.beginEpoch()
            follow(frame, seek = true)
        }
        return frame
    }

    /**
     * One frame while playing: reports the song player's position to the clock, keeps the player on
     * the span the clock is in (silent through a gap) and returns the programme time to draw.
     */
    fun tick(): Long {
        val now = clock.nowMs()
        follow(now, seek = false)
        return clock.nowMs()
    }

    fun ended(): Boolean = clock.ended()

    private fun follow(
        programmeMs: Long,
        seek: Boolean,
    ) {
        val at = clock.locate(programmeMs)
        if (at == null) {
            // A gap in the song: silence until the clock reaches the next clip.
            song.playWhenReady = false
            return
        }
        val (span, offsetMs) = at
        val onSpan = song.currentMediaItemIndex == span
        when {
            seek || !song.playWhenReady || !onSpan -> {
                song.seekTo(span, offsetMs)
                song.playWhenReady = true
            }
            song.playbackState == Player.STATE_BUFFERING -> {
                // Hold the picture where the sound will start rather than run ahead of it.
                clock.seek(songClips[span].startMs + song.currentPosition)
            }
            song.isPlaying -> {
                if (abs(song.currentPosition - offsetMs) > RESYNC_MS) song.seekTo(span, offsetMs)
                clock.heard(span, song.currentPosition)
            }
        }
    }

    /** What is on screen at [programmeMs]; [look] is the stage's look, which a look clip shows. */
    fun frameAt(
        programmeMs: Long,
        look: SceneParams,
    ): PreviewFrame {
        var scene: PreviewFrame.Scene? = null
        var video: PreviewFrame.Video? = null
        val stills = mutableListOf<PreviewFrame.Still>()
        val texts = mutableListOf<String>()
        val overlays = mutableListOf<PreviewFrame.Overlay>()
        // Hidden (muted) visual, text and overlay lanes draw nothing; a muted media lane is only silent.
        val onScreen =
            project.timeline.lanes
                .filterNot { it.muted && it.kind != LaneKind.Media }
                .mapNotNull { lane -> lane.clipAt(programmeMs)?.takeIf(Clip::enabled)?.let { lane to it } }
        for ((lane, clip) in onScreen) {
            when (val content = clip.content) {
                is ClipContent.Scene -> if (lane.kind == LaneKind.Visual) scene = sceneAt(clip, content, programmeMs, look)
                is ClipContent.Video ->
                    if (lane.kind == LaneKind.Media) {
                        val sourceMs = clip.sourceInMs + ((programmeMs - clip.startMs) * content.edit.speed).toLong()
                        video = PreviewFrame.Video(clip, content, sourceMs)
                    }
                is ClipContent.Still -> if (lane.kind == LaneKind.Media) stills += PreviewFrame.Still(content.uri)
                is ClipContent.Text -> if (lane.kind == LaneKind.Text) texts += content.text
                is ClipContent.Overlay -> overlays += PreviewFrame.Overlay(content.uri, content.blend, content.opacity)
                is ClipContent.Audio -> Unit
            }
        }
        return PreviewFrame(scene, video, stills, texts, overlays)
    }

    /** The look a visual clip shows: its take's state at take time, else [look]; scene keyframes over it. */
    private fun sceneAt(
        clip: Clip,
        content: ClipContent.Scene,
        programmeMs: Long,
        look: SceneParams,
    ): PreviewFrame.Scene {
        val take = content.takeId?.let(takes::get)
        val state = take?.stateAt(clip.sourceTimeAt(programmeMs))
        val base = state?.params ?: look
        val sheet = project.keyframes
        val animated =
            sheet.tracks.any {
                it.enabled && (it.clipId == null || it.clipId == clip.id) && AnimatableParams.find(it.paramId)?.isScene == true
            }
        val params = if (animated) AnimatableParams.applyToScene(base, sheet.valuesAt(programmeMs, clip.id)) else base
        val sceneId = state?.sceneId?.takeIf { it.isNotEmpty() } ?: content.sceneId
        return PreviewFrame.Scene(sceneId, params)
    }

    /**
     * Points the footage player at [video] (a new clip loads it with its grade and speed) and keeps
     * it on [PreviewFrame.Video.sourceMs]: exactly while paused, within [RESYNC_MS] while playing.
     * Null stops it.
     */
    fun showFootage(video: PreviewFrame.Video?) {
        if (video == null) {
            footage.playWhenReady = false
            return
        }
        val edit = video.content.edit
        if (footageClipId != video.clip.id.value) {
            footageClipId = video.clip.id.value
            footage.setVideoEffects(edit.videoEffects(null))
            footage.setMediaItem(MediaItem.fromUri(video.content.uri), video.sourceMs)
            footage.playbackParameters = PlaybackParameters(edit.speed)
            footage.prepare()
        }
        if (playing) {
            if (!footage.playWhenReady || abs(footage.currentPosition - video.sourceMs) > RESYNC_MS) footage.seekTo(video.sourceMs)
            footage.playWhenReady = true
        } else {
            footage.playWhenReady = false
            if (footage.currentPosition != video.sourceMs) footage.seekTo(video.sourceMs)
        }
    }

    private fun songItem(clip: Clip): MediaItem =
        MediaItem
            .Builder()
            .setUri((clip.content as ClipContent.Audio).uri)
            .setClippingConfiguration(
                MediaItem.ClippingConfiguration
                    .Builder()
                    .setStartPositionMs(clip.sourceInMs.coerceAtLeast(0L))
                    .setEndPositionMs(clip.sourceInMs.coerceAtLeast(0L) + clip.durationMs)
                    .build(),
            ).build()

    override fun close() {
        song.release()
        footage.release()
        clock.close()
    }

    private companion object {
        /** Power of two, as SampleRing needs: about 1.4 s of stereo at 48 kHz. */
        const val RING_FRAMES = 1 shl 16
        const val DEFAULT_RATE_HZ = 48_000
        const val DEFAULT_FPS = 30

        /** How far a player may drift from the clock before it is re-seeked. */
        const val RESYNC_MS = 300L
    }
}
