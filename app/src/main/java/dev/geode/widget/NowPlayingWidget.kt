package dev.geode.widget

import android.content.ComponentName
import android.content.Context
import android.graphics.Bitmap
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.glance.ColorFilter
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.Image
import androidx.glance.ImageProvider
import androidx.glance.LocalContext
import androidx.glance.LocalSize
import androidx.glance.action.ActionParameters
import androidx.glance.action.actionParametersOf
import androidx.glance.action.actionStartActivity
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.SizeMode
import androidx.glance.appwidget.action.ActionCallback
import androidx.glance.appwidget.action.actionRunCallback
import androidx.glance.appwidget.appWidgetBackground
import androidx.glance.appwidget.cornerRadius
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.layout.Alignment
import androidx.glance.layout.Column
import androidx.glance.layout.ContentScale
import androidx.glance.layout.Row
import androidx.glance.layout.fillMaxSize
import androidx.glance.layout.padding
import androidx.glance.layout.size
import androidx.glance.text.FontWeight
import androidx.glance.text.Text
import androidx.glance.text.TextStyle
import androidx.glance.unit.ColorProvider
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import dev.geode.R
import dev.geode.playback.MediaArtwork
import dev.geode.playback.PlaybackService
import dev.geode.ui.MainActivity
import java.io.File
import kotlin.math.roundToInt

/** Artwork, title, artist and transport, redrawn whenever the playback service publishes a change. */
class NowPlayingWidget : GlanceAppWidget() {
    // Responsive: Glance renders one layout per breakpoint up front and the launcher picks the
    // one that fits, so resizing never waits on a redraw.
    override val sizeMode: SizeMode = SizeMode.Responsive(setOf(SMALL_SIZE, MEDIUM_SIZE, LARGE_SIZE))

    // Glance calls this from its session worker, off the main thread.
    override suspend fun provideGlance(
        context: Context,
        id: GlanceId,
    ) {
        val state = WidgetState.load(context)
        val art = state.artPath?.let { loadArt(context, it) }
        provideContent { NowPlaying(state, art?.let(::ImageProvider)) }
    }
}

class NowPlayingWidgetReceiver : GlanceAppWidgetReceiver() {
    override val glanceAppWidget: GlanceAppWidget = NowPlayingWidget()
}

internal enum class WidgetLayout { SMALL, MEDIUM, LARGE }

internal val SMALL_SIZE = DpSize(110.dp, 48.dp)
internal val MEDIUM_SIZE = DpSize(250.dp, 48.dp)
internal val LARGE_SIZE = DpSize(250.dp, 110.dp)

/** The layout for a breakpoint (or any size): large needs 4x2, medium 4x1, anything narrower is small. */
internal fun layoutFor(size: DpSize): WidgetLayout =
    when {
        size.width < MEDIUM_SIZE.width -> WidgetLayout.SMALL
        size.height < LARGE_SIZE.height -> WidgetLayout.MEDIUM
        else -> WidgetLayout.LARGE
    }

/** [width]x[height] scaled down to fit a [maxPx] square, keeping the aspect ratio; never upscales. */
internal fun fitWithin(
    width: Int,
    height: Int,
    maxPx: Int,
): Pair<Int, Int> {
    val longest = maxOf(width, height)
    if (longest <= maxPx) return width to height
    val scale = maxPx.toFloat() / longest
    return (width * scale).roundToInt().coerceAtLeast(1) to (height * scale).roundToInt().coerceAtLeast(1)
}

/**
 * Decodes the stored artwork at no more than the largest size any layout draws it, so the
 * RemoteViews bitmap (parcelled to the launcher, and capped in memory) stays small.
 */
private fun loadArt(
    context: Context,
    path: String,
): Bitmap? {
    val maxPx = (LARGE_ART.value * context.resources.displayMetrics.density).roundToInt()
    val bytes = runCatching { File(path).readBytes() }.getOrNull() ?: return null
    val decoded = MediaArtwork.decodeBytes(bytes, maxPx) ?: return null
    val (width, height) = fitWithin(decoded.width, decoded.height, maxPx)
    return Bitmap.createScaledBitmap(decoded, width, height, true).also { if (it !== decoded) decoded.recycle() }
}

@Composable
private fun NowPlaying(
    state: WidgetState,
    art: ImageProvider?,
) {
    val layout = layoutFor(LocalSize.current)
    Row(
        modifier =
            GlanceModifier
                .fillMaxSize()
                .appWidgetBackground()
                .background(ImageProvider(R.drawable.widget_glass_background))
                .cornerRadius(R.dimen.widget_corner_radius)
                .padding(horizontal = 12.dp, vertical = if (layout == WidgetLayout.LARGE) 12.dp else 0.dp)
                .clickable(actionStartActivity<MainActivity>()),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        when (layout) {
            WidgetLayout.SMALL -> {
                PlayPauseButton(state.playing)
                TrackText(
                    state,
                    titleSize = 14.sp,
                    artistSize = null,
                    modifier = GlanceModifier.defaultWeight().padding(start = 10.dp),
                )
            }

            WidgetLayout.MEDIUM -> {
                Artwork(art, MEDIUM_ART)
                TrackText(
                    state,
                    titleSize = 14.sp,
                    artistSize = 12.sp,
                    modifier = GlanceModifier.defaultWeight().padding(horizontal = 10.dp),
                )
                Transport(state.playing)
            }

            WidgetLayout.LARGE -> {
                Artwork(art, LARGE_ART)
                Column(modifier = GlanceModifier.defaultWeight().padding(start = 10.dp)) {
                    TrackText(state, titleSize = 16.sp, artistSize = 13.sp)
                    Row(verticalAlignment = Alignment.CenterVertically) { Transport(state.playing) }
                }
            }
        }
    }
}

@Composable
private fun Artwork(
    art: ImageProvider?,
    size: Dp,
) {
    Image(
        provider = art ?: ImageProvider(R.drawable.ic_widget_art),
        contentDescription = null,
        contentScale = if (art != null) ContentScale.Crop else ContentScale.Fit,
        modifier =
            GlanceModifier
                .size(size)
                .cornerRadius(R.dimen.widget_inner_radius)
                .then(if (art == null) GlanceModifier.background(PLACEHOLDER).padding(size / 4) else GlanceModifier),
    )
}

@Composable
private fun TrackText(
    state: WidgetState,
    titleSize: TextUnit,
    artistSize: TextUnit?,
    modifier: GlanceModifier = GlanceModifier,
) {
    val context = LocalContext.current
    Column(modifier = modifier) {
        Text(
            text = if (state.hasTrack) state.title else context.getString(R.string.widget_nothing_playing),
            style = TextStyle(color = TEXT, fontSize = titleSize, fontWeight = FontWeight.Medium),
            maxLines = 1,
        )
        if (artistSize != null && state.artist.isNotBlank()) {
            Text(text = state.artist, style = TextStyle(color = TEXT_SECONDARY, fontSize = artistSize), maxLines = 1)
        }
    }
}

@Composable
private fun Transport(playing: Boolean) {
    TransportButton(R.drawable.ic_widget_prev, R.string.widget_previous, WidgetTransportAction.PREVIOUS)
    PlayPauseButton(playing)
    TransportButton(R.drawable.ic_widget_next, R.string.widget_next, WidgetTransportAction.NEXT)
}

@Composable
private fun PlayPauseButton(playing: Boolean) {
    TransportButton(
        icon = if (playing) R.drawable.ic_widget_pause else R.drawable.ic_widget_play,
        label = if (playing) R.string.widget_pause else R.string.widget_play,
        command = WidgetTransportAction.PLAY_PAUSE,
        container = R.drawable.widget_play_container,
    )
}

@Composable
private fun TransportButton(
    icon: Int,
    label: Int,
    command: String,
    container: Int? = null,
) {
    val context = LocalContext.current
    Image(
        provider = ImageProvider(icon),
        contentDescription = context.getString(label),
        // The icons are shared with picture-in-picture, so they stay light and go dark here on the accent.
        colorFilter = if (container != null) ColorFilter.tint(ON_ACCENT) else null,
        modifier =
            GlanceModifier
                .size(TOUCH_TARGET)
                .then(if (container != null) GlanceModifier.background(ImageProvider(container)) else GlanceModifier)
                .padding(12.dp)
                .clickable(actionRunCallback<WidgetTransportAction>(actionParametersOf(WidgetTransportAction.COMMAND to command))),
    )
}

/** Transport goes through a MediaController on the session, so the widget drives whatever the service is playing. */
class WidgetTransportAction : ActionCallback {
    override suspend fun onAction(
        context: Context,
        glanceId: GlanceId,
        parameters: ActionParameters,
    ) {
        val command = parameters[COMMAND] ?: return
        val token = SessionToken(context, ComponentName(context, PlaybackService::class.java))
        val future = MediaController.Builder(context, token).buildAsync()
        future.addListener(
            {
                val controller = runCatching { future.get() }.getOrNull() ?: return@addListener
                when (command) {
                    PLAY_PAUSE -> if (controller.isPlaying) controller.pause() else controller.play()
                    NEXT -> controller.seekToNext()
                    PREVIOUS -> controller.seekToPrevious()
                }
                controller.release()
            },
            ContextCompat.getMainExecutor(context),
        )
    }

    companion object {
        val COMMAND = ActionParameters.Key<String>("command")
        const val PLAY_PAUSE = "play_pause"
        const val NEXT = "next"
        const val PREVIOUS = "previous"
    }
}

private val TOUCH_TARGET = 48.dp
private val MEDIUM_ART = 40.dp

// At the 250dp breakpoint width this leaves exactly the 144dp the three transport buttons need.
private val LARGE_ART = 72.dp

// Black-glass tokens. The background, hairline edge and accent live in the widget_* drawables.
private val TEXT = ColorProvider(Color(0xFFECECF0))
private val TEXT_SECONDARY = ColorProvider(Color(0xFFA6A8B2))
private val ON_ACCENT = ColorProvider(Color(0xFF0E0E10))
private val PLACEHOLDER = ColorProvider(Color(0x14FFFFFF))
