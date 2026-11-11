package dev.geode.ui

import android.view.TextureView
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import dev.geode.R
import dev.geode.RingLog
import dev.geode.ui.theme.LocalReducedMotion
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private const val TAG = "BootIntro"

/** The splash art fades into the video's first frame, which is dark nebula, over this long. */
private const val MARK_FADE_MS = 400

/** The wordmark comes in over the clip's last two seconds, under the finished eye. */
private const val WORDMARK_LEAD_MS = 2000L
private const val TEXT_IN_MS = 600
private const val FADE_OUT_MS = 300

/**
 * If no frame has been drawn this long after start (no decoder, a stalled codec), the intro gives
 * up and opens the app rather than holding the splash art on screen.
 */
private const val FIRST_FRAME_TIMEOUT_MS = 1500L

/** res/raw/splash_intro.mp4 is 720x1280; tools/brand_raster.sh makes it. */
private const val VIDEO_ASPECT = 720f / 1280f

/**
 * Where the wordmark sits: below the eye, whose waves reach about 17% of the clip's height under
 * its centre, as a fraction of the clip's height.
 */
private const val WORDMARK_DROP = 0.22f

/**
 * The system splash's icon box when the icon has no background of its own: 288dp, with the art
 * inside its 192dp circle. Drawing the same art at the same size, centred on the same colour
 * (brand void), makes BootIntro's first frame the splash's last, so the two read as one intro.
 */
private val SPLASH_ICON_SIZE = 288.dp

/**
 * The cold-start intro: the splash art dissolves into the splash video (silent, about 4.5 s: the
 * eye blooms, forms and lights its iris), the wordmark fades in under the finished eye, then the
 * whole layer fades away. A tap skips it, reduced motion skips it entirely, and Settings > Look
 * turns it off (BootAnimationStore).
 *
 * The clip plays through its own muted ExoPlayer, not the app's PlaybackEngine. It holds no audio
 * focus and has no audio track, so music the user already has playing carries on. It draws into a
 * TextureView because a SurfaceView would punch through the fading Compose layer above it.
 */
@Composable
fun BootIntro(onDone: () -> Unit) {
    val reducedMotion = LocalReducedMotion.current
    if (reducedMotion) {
        // Reduced motion means no intro at all, not a slower one.
        LaunchedEffect(Unit) { onDone() }
        return
    }
    val finish by rememberUpdatedState(onDone)
    val context = LocalContext.current
    val overlayAlpha = remember { Animatable(1f) }
    val markAlpha = remember { Animatable(1f) }
    val textAlpha = remember { Animatable(0f) }
    val textScale = remember { Animatable(0.85f) }
    var firstFrame by remember { mutableStateOf(false) }
    var ended by remember { mutableStateOf(false) }

    val player =
        remember {
            ExoPlayer.Builder(context).build().apply {
                volume = 0f
                repeatMode = Player.REPEAT_MODE_OFF
                setMediaItem(MediaItem.fromUri(splashUri(context.packageName)))
                prepare()
                playWhenReady = true
            }
        }
    DisposableEffect(player) {
        val listener =
            object : Player.Listener {
                override fun onRenderedFirstFrame() {
                    firstFrame = true
                }

                override fun onPlaybackStateChanged(playbackState: Int) {
                    if (playbackState == Player.STATE_ENDED) ended = true
                }

                override fun onPlayerError(error: PlaybackException) {
                    RingLog.note(TAG, "splash video failed; opening the app", error)
                    ended = true
                }
            }
        player.addListener(listener)
        onDispose {
            player.removeListener(listener)
            // Detach the TextureView and stop before releasing: released with the surface still
            // attached, the decoder kept posting to its callback thread after that thread had quit
            // ("sending message to a Handler on a dead thread", and a GraphicsTracker error per
            // buffer it could no longer hand back) on every cold start.
            player.clearVideoSurface()
            player.stop()
            player.release()
        }
    }

    LaunchedEffect(Unit) {
        delay(FIRST_FRAME_TIMEOUT_MS)
        if (!firstFrame) ended = true
    }
    LaunchedEffect(firstFrame) {
        if (!firstFrame) return@LaunchedEffect
        launch { markAlpha.animateTo(0f, tween(MARK_FADE_MS, easing = LinearOutSlowInEasing)) }
        val duration = player.duration
        if (duration != C.TIME_UNSET) {
            delay((duration - player.currentPosition - WORDMARK_LEAD_MS).coerceAtLeast(0L))
        }
        launch { textAlpha.animateTo(1f, tween(TEXT_IN_MS, easing = LinearOutSlowInEasing)) }
        textScale.animateTo(1f, tween(TEXT_IN_MS + 100, easing = FastOutSlowInEasing))
    }
    LaunchedEffect(ended) {
        if (!ended) return@LaunchedEffect
        overlayAlpha.animateTo(0f, tween(FADE_OUT_MS))
        finish()
    }

    val primary = MaterialTheme.colorScheme.primary
    val onBackground = MaterialTheme.colorScheme.onBackground
    val wordmark = LocalFontColor.current ?: lerp(primary, onBackground, 0.72f)
    Box(
        Modifier
            .fillMaxSize()
            .graphicsLayer { alpha = overlayAlpha.value }
            .background(colorResource(R.color.brand_void))
            .pointerInput(Unit) { detectTapGestures { ended = true } },
        contentAlignment = Alignment.Center,
    ) {
        // Fitted, never cropped: the eye spans almost the clip's full width, and the clip's edges
        // are the same void as the background, so the bars on a tall or wide screen don't show.
        BoxWithConstraints(
            Modifier.aspectRatio(VIDEO_ASPECT),
            contentAlignment = Alignment.Center,
        ) {
            AndroidView(
                factory = { TextureView(it).also(player::setVideoTextureView) },
                modifier =
                    Modifier
                        .fillMaxSize()
                        .graphicsLayer { alpha = if (firstFrame) 1f else 0f },
            )
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                modifier =
                    Modifier
                        .offset(y = maxHeight * WORDMARK_DROP)
                        .graphicsLayer {
                            alpha = textAlpha.value
                            scaleX = textScale.value
                            scaleY = textScale.value
                        },
            ) {
                Text(
                    stringResource(R.string.boot_wordmark),
                    color = wordmark,
                    style = MaterialTheme.typography.displaySmall,
                )
                Text(
                    stringResource(R.string.boot_tagline).uppercase(),
                    color = primary.copy(alpha = 0.85f),
                    style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 3.5.sp),
                    modifier = Modifier.padding(top = 10.dp),
                )
            }
        }
        Image(
            painterResource(R.drawable.splash_mark),
            contentDescription = null,
            modifier = Modifier.size(SPLASH_ICON_SIZE).graphicsLayer { alpha = markAlpha.value },
        )
    }
}

/** The clip in res/raw, as a URI ExoPlayer's default data source opens directly. */
private fun splashUri(packageName: String) = "android.resource://$packageName/${R.raw.splash_intro}"
