package dev.geode.ui

import android.graphics.BitmapFactory
import android.view.SurfaceView
import android.view.TextureView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.zIndex
import androidx.core.net.toUri
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import dev.geode.R
import dev.geode.data.EditorProjectStore
import dev.geode.data.PerformanceTake
import dev.geode.editor.ProjectId
import dev.geode.export.ClipEdit
import dev.geode.export.CubeLut
import dev.geode.export.ExportAspect
import dev.geode.export.ExportRatio
import dev.geode.export.StudioClip
import dev.geode.render.PerformanceSettings
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import kotlin.math.abs

@UnstableApi
@Composable
internal fun ClipPreview(
    clip: StudioClip,
    edit: ClipEdit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var playing by remember(clip.uri) { mutableStateOf(true) }
    val player =
        remember(clip.uri) {
            ExoPlayer.Builder(context).build().apply {
                repeatMode = Player.REPEAT_MODE_ONE
                volume = 0f
                playWhenReady = true
            }
        }
    DisposableEffect(player) {
        onDispose { player.release() }
    }

    LaunchedEffect(player, edit) {
        delay(REBUILD_SETTLE_MS)
        val lut = edit.lutUri?.let { uri -> withContext(Dispatchers.IO) { CubeLut.load(context, uri) } }
        player.setVideoEffects(edit.videoEffects(lut))
        player.setMediaItems(
            listOf(
                MediaItem
                    .Builder()
                    .setUri(clip.uri)
                    .setClippingConfiguration(edit.clipping())
                    .build(),
            ),
        )
        player.playbackParameters = PlaybackParameters(edit.speed)
        player.prepare()
    }
    LaunchedEffect(player, playing) {
        player.playWhenReady = playing
    }

    val aspect =
        edit.ratio?.let { r ->
            ExportAspect.of(edit.quality, r).let { it.width.toFloat() / it.height }
        } ?: if (clip.width > 0 && clip.height > 0) {
            clip.width.toFloat() / clip.height
        } else {
            16f / 9f
        }
    // The whole preview is the play/pause button, so it carries the label and the state; the
    // PlayArrow icon below only shows while paused and cannot be the label.
    val previewLabel = stringResource(R.string.studio_preview_toggle)
    val clickLabel = stringResource(if (playing) R.string.action_pause else R.string.action_play)
    val playState = stringResource(if (playing) R.string.studio_a11y_playing else R.string.state_paused)
    Box(
        modifier
            .fillMaxWidth()
            .aspectRatio(aspect.coerceIn(0.3f, 3.4f))
            .clip(
                androidx.compose.foundation.shape
                    .RoundedCornerShape(8.dp),
            ).background(Color.Black)
            .clickable(role = Role.Button, onClickLabel = clickLabel) { playing = !playing }
            .semantics {
                contentDescription = previewLabel
                stateDescription = playState
            },
        contentAlignment = Alignment.Center,
    ) {
        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).also { player.setVideoSurfaceView(it) }
            },
            modifier = Modifier.fillMaxWidth(),
        )
        if (!playing) {
            Icon(
                Icons.Filled.PlayArrow,
                contentDescription = null,
                tint = Color.White.copy(alpha = 0.85f),
                modifier = Modifier.zIndex(1f).size(44.dp),
            )
        }
    }
}

private const val REBUILD_SETTLE_MS = 250L

/**
 * The Studio timeline's live preview (wave 1, part 2B): the whole project, playing with sound, in a
 * window the timeline sits under.
 *
 * - Visual clips are drawn live by the GL renderer (a [VisualizerView] at reduced resolution, see
 *   [PREVIEW_PERFORMANCE]) hearing the project's song lane, so they react to the audio lane as a
 *   render does. Footage, stills, text and overlays are drawn over it.
 * - While [playing], the preview keeps time (the song player and `core/studio/PreviewClock`) and
 *   reports each new frame through [onSeek], which the timeline's playhead follows; it calls
 *   [onEnded] at the project's end. A [playheadMs] that jumps while playing (a tap on the ruler)
 *   moves the preview there.
 * - Paused, it shows the frame under [playheadMs] (frame-aligned at the project's frame rate), so
 *   scrubbing the playhead scrubs the picture.
 *
 * [look] is what a look clip without a take shows: pass the stage's current look, as a render does.
 * The window takes the project's aspect ratio; place it with [modifier].
 *
 * Not drawn yet: MilkDrop look clips show the renderer's remembered preset (loading the clip's own
 * would change the stage's too), a video's LUT, and overlay blend modes other than plain alpha.
 */
@UnstableApi
@Composable
fun StudioPreview(
    state: EditorUiState,
    playheadMs: Long,
    playing: Boolean,
    onSeek: (Long) -> Unit,
    modifier: Modifier = Modifier,
    look: SceneParams = SceneParams(),
    onEnded: () -> Unit = {},
) {
    val context = LocalContext.current
    val controller = remember { StudioPreviewController(context) }
    val glView =
        remember {
            VisualizerView(context).apply {
                visualizerRenderer.soundInput = { controller.soundInput }
                applyPerformance(PREVIEW_PERFORMANCE)
            }
        }
    DisposableEffect(Unit) {
        glView.onResume()
        onDispose {
            glView.onPause()
            controller.close()
        }
    }

    val project = state.project
    val open = state.open
    val fps = open?.export?.fps ?: DEFAULT_PREVIEW_FPS
    val lengthMs = open?.lengthMs ?: project.timeline.durationMs
    val ratio = open?.export?.ratio ?: ExportRatio.R16_9
    val takes = rememberProjectTakes(open?.id, open?.takeIds.orEmpty())

    var bound by remember { mutableIntStateOf(0) }
    LaunchedEffect(project, fps, lengthMs, takes) {
        controller.bind(project, fps, lengthMs, takes)
        bound++
    }

    val latestOnSeek by rememberUpdatedState(onSeek)
    val latestOnEnded by rememberUpdatedState(onEnded)
    var shownMs by remember { mutableLongStateOf(playheadMs) }
    LaunchedEffect(playing) {
        if (!playing) {
            controller.pause()
            shownMs = controller.seek(playheadMs)
            return@LaunchedEffect
        }
        controller.play(playheadMs)
        while (isActive) {
            withFrameNanos { }
            val ms = controller.tick()
            if (ms != shownMs) {
                shownMs = ms
                latestOnSeek(ms)
            }
            if (controller.ended()) {
                controller.pause()
                latestOnEnded()
                break
            }
        }
    }
    LaunchedEffect(playheadMs) {
        // While playing, the playhead mostly echoes what this preview reported; only a real jump moves it.
        if (!controller.playing || abs(playheadMs - shownMs) > JUMP_MS) shownMs = controller.seek(playheadMs)
    }

    val frame = remember(shownMs, bound, look) { controller.frameAt(shownMs, look) }
    SideEffect { controller.showFootage(frame.video) }

    PreviewLayers(frame, glView, controller, modifier.aspectRatio(ratio.wRatio.toFloat() / ratio.hRatio))
}

/**
 * The takes recorded into project [id], read once per project and take list off the main thread.
 * Empty when no project is open, so a closed project's takes never linger.
 */
@Composable
private fun rememberProjectTakes(
    id: ProjectId?,
    takeIds: List<String>,
): Map<String, PerformanceTake.Timeline> {
    val context = LocalContext.current
    val takes by produceState(emptyMap<String, PerformanceTake.Timeline>(), id, takeIds) {
        value = emptyMap()
        if (id == null) return@produceState
        value =
            withContext(Dispatchers.IO) {
                val store = EditorProjectStore(context)
                takeIds
                    .mapNotNull { takeId ->
                        store
                            .loadTake(id, takeId)
                            ?.let { json -> runCatching { PerformanceTake.Timeline(json) }.getOrNull() }
                            ?.let { takeId to it }
                    }.toMap()
            }
    }
    return takes
}

/** One frame of the preview: the GL visual at the bottom, then footage, stills, overlays and text. */
@UnstableApi
@Composable
private fun PreviewLayers(
    frame: PreviewFrame,
    glView: VisualizerView,
    controller: StudioPreviewController,
    modifier: Modifier,
) {
    Box(
        modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(Color.Black),
    ) {
        AndroidView(
            factory = { glView },
            modifier = Modifier.fillMaxSize(),
            update = { view ->
                frame.scene?.let {
                    view.visualizerRenderer.requestedSceneId = it.sceneId
                    view.visualizerRenderer.sceneParams = it.params
                }
            },
        )
        // No visual clip here: black, as the render has.
        if (frame.scene == null) Box(Modifier.fillMaxSize().background(Color.Black))
        if (frame.video != null) {
            AndroidView(
                factory = { ctx -> TextureView(ctx).also(controller.footage::setVideoTextureView) },
                modifier = Modifier.fillMaxSize(),
            )
        }
        frame.stills.forEach { still -> PreviewImage(still.uri, alpha = 1f) }
        frame.overlays.forEach { overlay -> PreviewImage(overlay.uri, alpha = overlay.opacity) }
        if (frame.texts.isNotEmpty()) {
            Column(Modifier.align(Alignment.BottomCenter).padding(12.dp)) {
                frame.texts.forEach { line ->
                    Text(
                        text = line,
                        color = Color.White,
                        style = MaterialTheme.typography.titleMedium,
                        textAlign = TextAlign.Center,
                    )
                }
            }
        }
    }
}

/** A still or overlay image, decoded once off the main thread at preview size. */
@Composable
private fun PreviewImage(
    uri: String,
    alpha: Float,
) {
    val context = LocalContext.current
    val image by produceState<ImageBitmap?>(null, uri) {
        value =
            withContext(Dispatchers.IO) {
                runCatching {
                    val parsed = uri.toUri()
                    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    context.contentResolver.openInputStream(parsed)?.use { BitmapFactory.decodeStream(it, null, bounds) }
                    var sample = 1
                    while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= PREVIEW_IMAGE_PX) sample *= 2
                    val options = BitmapFactory.Options().apply { inSampleSize = sample }
                    context.contentResolver
                        .openInputStream(parsed)
                        ?.use { BitmapFactory.decodeStream(it, null, options) }
                        ?.asImageBitmap()
                }.getOrNull()
            }
    }
    image?.let {
        Image(
            bitmap = it,
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop,
            alpha = alpha,
        )
    }
}

/**
 * The preview's GL budget: half resolution, 30 fps, cheaper ray marching and no optional passes, so
 * the preview never fights the timeline for the GPU. The render draws at full quality.
 */
private val PREVIEW_PERFORMANCE =
    PerformanceSettings(renderScale = 0.5f, fpsCap = 30, marchScale = 0.5f, postFx = false, adaptive = true)

private const val DEFAULT_PREVIEW_FPS = 30

/** A playhead that moves further than this from the shown frame while playing is a jump, not an echo. */
private const val JUMP_MS = 250L

/** Longest side a preview image is decoded at. */
private const val PREVIEW_IMAGE_PX = 1280
