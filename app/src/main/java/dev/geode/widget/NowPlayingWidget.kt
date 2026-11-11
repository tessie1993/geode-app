package dev.geode.widget

import android.content.ComponentName
import android.content.Context
import android.graphics.Bitmap
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
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
import androidx.glance.action.Action
import androidx.glance.action.ActionParameters
import androidx.glance.action.actionParametersOf
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.LinearProgressIndicator
import androidx.glance.appwidget.SizeMode
import androidx.glance.appwidget.action.ActionCallback
import androidx.glance.appwidget.action.actionRunCallback
import androidx.glance.appwidget.action.actionStartActivity
import androidx.glance.appwidget.appWidgetBackground
import androidx.glance.appwidget.cornerRadius
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.layout.Alignment
import androidx.glance.layout.Box
import androidx.glance.layout.Column
import androidx.glance.layout.ContentScale
import androidx.glance.layout.Row
import androidx.glance.layout.RowScope
import androidx.glance.layout.Spacer
import androidx.glance.layout.fillMaxSize
import androidx.glance.layout.fillMaxWidth
import androidx.glance.layout.height
import androidx.glance.layout.padding
import androidx.glance.layout.size
import androidx.glance.text.FontWeight
import androidx.glance.text.Text
import androidx.glance.text.TextStyle
import androidx.glance.unit.ColorProvider
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import dev.geode.R
import dev.geode.appLaunchIntent
import dev.geode.playback.MediaArtwork
import dev.geode.playback.PlaybackService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.runningFold
import java.io.File
import kotlin.math.roundToInt

/**
 * The now-playing widget in the app's black glass: the track, its progress, the transport, and at
 * two rows and up the visual on screen with buttons to step its style.
 *
 * Sizes follow Glance's canonical layouts: [SizeMode.Exact] and one layout per size class
 * ([layoutFor]), so a resize gets the layout that fits rather than a stretched smaller one.
 */
class NowPlayingWidget : GlanceAppWidget() {
    override val sizeMode: SizeMode = SizeMode.Exact

    // Glance calls this from its session worker, off the main thread. The session's composition
    // outlives this call, so the first frame is built here and later ones come from snapshots().
    override suspend fun provideGlance(
        context: Context,
        id: GlanceId,
    ) {
        val first = WidgetSnapshot.of(context, WidgetState.load(context), WidgetStyles.listening.value)
        provideContent {
            val snapshot by remember { snapshots(context, first) }.collectAsState(first)
            NowPlaying(snapshot)
        }
    }

    // Each snapshot is built from the one before it: the progress tick changes the position every
    // 20 s, and the pictures must not be read and decoded again for that (see WidgetSnapshot.of).
    private fun snapshots(
        context: Context,
        first: WidgetSnapshot,
    ): Flow<WidgetSnapshot> =
        combine(WidgetState.changes.filterNotNull(), WidgetStyles.listening) { state, listening -> state to listening }
            .distinctUntilChanged()
            .runningFold(first) { previous, (state, listening) -> WidgetSnapshot.of(context, state, listening, previous) }
            .flowOn(Dispatchers.IO)
}

class NowPlayingWidgetReceiver : GlanceAppWidgetReceiver() {
    override val glanceAppWidget: GlanceAppWidget = NowPlayingWidget()
}

/** One frame of the widget: the state with its pictures decoded, and whether a session takes style steps. */
internal class WidgetSnapshot(
    val state: WidgetState,
    val art: Bitmap?,
    val listening: Boolean,
    private val artKey: ArtKey?,
    private val picture: WidgetVisual.Picture,
) {
    val visual: Bitmap get() = picture.bitmap

    companion object {
        /**
         * The frame for [state]. A picture whose source did not change since [previous] is taken
         * from it, the same Bitmap, and not decoded again: the artwork while its file is the same
         * one ([ArtKey]), the visual while it draws the same thing ([WidgetVisual.render]).
         */
        fun of(
            context: Context,
            state: WidgetState,
            listening: Boolean,
            previous: WidgetSnapshot? = null,
        ): WidgetSnapshot {
            val artKey = state.artPath?.let { ArtKey.of(it) }
            val art =
                when {
                    artKey == null -> null
                    previous != null && previous.artKey == artKey -> previous.art
                    else -> loadArt(context, artKey.path)
                }
            return WidgetSnapshot(state, art, listening, artKey, WidgetVisual.render(context, state, previous?.picture))
        }
    }
}

/** Which artwork file a decoded bitmap came from: the path alone is not enough, every track overwrites one file. */
internal data class ArtKey(
    val path: String,
    val lastModified: Long,
    val length: Long,
) {
    companion object {
        fun of(path: String): ArtKey {
            val file = File(path)
            return ArtKey(path, file.lastModified(), file.length())
        }
    }
}

internal enum class WidgetLayout { SMALL, MEDIUM, LARGE, TALL }

/**
 * The layout for a size: under [MEDIUM_MIN_WIDTH] only play/pause and the title fit (2x1, 3x1);
 * one row tall is the classic bar (4x1); two rows add the visual behind the glass and the style
 * buttons (4x2); from [TALL_MIN_HEIGHT] the visual gets its own tile (4x3 and up).
 */
internal fun layoutFor(size: DpSize): WidgetLayout =
    when {
        size.width < MEDIUM_MIN_WIDTH -> WidgetLayout.SMALL
        size.height < LARGE_MIN_HEIGHT -> WidgetLayout.MEDIUM
        size.height < TALL_MIN_HEIGHT -> WidgetLayout.LARGE
        else -> WidgetLayout.TALL
    }

// One size from each layout's range, as the launcher hands them over for the common grids.
internal val SMALL_SIZE = DpSize(110.dp, 48.dp)
internal val MEDIUM_SIZE = DpSize(250.dp, 48.dp)
internal val LARGE_SIZE = DpSize(250.dp, 110.dp)

internal val MEDIUM_MIN_WIDTH = 200.dp
internal val LARGE_MIN_HEIGHT = 100.dp
internal val TALL_MIN_HEIGHT = 170.dp

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

/** "m:ss", or "h:mm:ss" from an hour; the progress bar's labels. */
internal fun formatTime(ms: Long): String {
    val total = (ms / 1000).coerceAtLeast(0)
    val hours = total / 3600
    val minutes = (total % 3600) / 60
    val seconds = total % 60
    val mmss = "${if (hours > 0) minutes.twoDigits() else minutes}:${seconds.twoDigits()}"
    return if (hours > 0) "$hours:$mmss" else mmss
}

private fun Long.twoDigits(): String = toString().padStart(2, '0')

/**
 * Decodes the stored artwork at no more than the largest size any layout draws it, so the
 * RemoteViews bitmap (parcelled to the launcher, and capped in memory) stays small.
 */
private fun loadArt(
    context: Context,
    path: String,
): Bitmap? {
    val maxPx = (LARGEST_ART.value * context.resources.displayMetrics.density).roundToInt()
    val bytes = runCatching { File(path).readBytes() }.getOrNull() ?: return null
    val decoded = MediaArtwork.decodeBytes(bytes, maxPx) ?: return null
    val (width, height) = fitWithin(decoded.width, decoded.height, maxPx)
    return Bitmap.createScaledBitmap(decoded, width, height, true).also { if (it !== decoded) decoded.recycle() }
}

@Composable
private fun NowPlaying(snapshot: WidgetSnapshot) {
    val layout = layoutFor(LocalSize.current)
    val behindGlass = layout == WidgetLayout.LARGE
    Box(
        modifier =
            GlanceModifier
                .fillMaxSize()
                .appWidgetBackground()
                .background(ImageProvider(R.drawable.widget_glass_background))
                .cornerRadius(R.dimen.widget_corner_radius)
                .clickable(actionStartActivity(LocalContext.current.appLaunchIntent())),
    ) {
        if (behindGlass) {
            // The visual under a dark scrim: the app's glass over the live picture, at widget scale.
            Image(
                provider = ImageProvider(snapshot.visual),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = GlanceModifier.fillMaxSize(),
            )
            Image(
                provider = ImageProvider(R.drawable.widget_scrim),
                contentDescription = null,
                contentScale = ContentScale.FillBounds,
                modifier = GlanceModifier.fillMaxSize(),
            )
        }
        when (layout) {
            WidgetLayout.SMALL -> SmallLayout(snapshot)
            WidgetLayout.MEDIUM -> MediumLayout(snapshot)
            WidgetLayout.LARGE -> LargeLayout(snapshot)
            WidgetLayout.TALL -> TallLayout(snapshot)
        }
    }
}

/** 2x1 and 3x1: play/pause and the title. */
@Composable
private fun SmallLayout(snapshot: WidgetSnapshot) {
    Row(
        modifier = GlanceModifier.fillMaxSize().padding(horizontal = EDGE),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PlayPauseButton(snapshot.state.playing)
        TrackText(
            snapshot.state,
            titleSize = 14.sp,
            artistSize = null,
            modifier = GlanceModifier.defaultWeight().padding(start = GAP),
        )
    }
}

/** 4x1: art, title and artist over a thin progress bar, and the transport. */
@Composable
private fun MediumLayout(snapshot: WidgetSnapshot) {
    Row(
        modifier = GlanceModifier.fillMaxSize().padding(horizontal = EDGE),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Artwork(snapshot.art, MEDIUM_ART)
        Column(modifier = GlanceModifier.defaultWeight().padding(horizontal = GAP)) {
            TrackText(snapshot.state, titleSize = 14.sp, artistSize = 12.sp)
            Spacer(GlanceModifier.height(6.dp))
            Progress(snapshot.state)
        }
        Transport(snapshot.state.playing)
    }
}

/** 4x2: the visual behind the glass, the track with its art, progress, and transport between the two style buttons. */
@Composable
private fun LargeLayout(snapshot: WidgetSnapshot) {
    Column(
        modifier = GlanceModifier.fillMaxSize().padding(horizontal = EDGE, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(modifier = GlanceModifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Artwork(snapshot.art, LARGE_ART)
            TrackText(
                snapshot.state,
                titleSize = 15.sp,
                artistSize = 12.sp,
                styleLabel = snapshot.state.styleId?.let(WidgetVisual::label),
                modifier = GlanceModifier.defaultWeight().padding(start = GAP),
            )
        }
        Spacer(GlanceModifier.height(6.dp))
        Progress(snapshot.state)
        Spacer(GlanceModifier.height(2.dp))
        Row(modifier = GlanceModifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            StyleButton(snapshot.listening, step = -1)
            Spacer(GlanceModifier.defaultWeight())
            Transport(snapshot.state.playing)
            Spacer(GlanceModifier.defaultWeight())
            StyleButton(snapshot.listening, step = 1)
        }
    }
}

/** 4x3 and up: the visual as its own tile with the style buttons on it, then the track, progress with times, and transport. */
@Composable
private fun TallLayout(snapshot: WidgetSnapshot) {
    Column(modifier = GlanceModifier.fillMaxSize().padding(EDGE)) {
        VisualTile(snapshot, GlanceModifier.defaultWeight().fillMaxWidth())
        Spacer(GlanceModifier.height(GAP))
        Row(modifier = GlanceModifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Artwork(snapshot.art, LARGE_ART)
            TrackText(
                snapshot.state,
                titleSize = 16.sp,
                artistSize = 13.sp,
                modifier = GlanceModifier.defaultWeight().padding(start = GAP),
            )
        }
        Spacer(GlanceModifier.height(GAP))
        Progress(snapshot.state)
        Times(snapshot.state)
        Row(modifier = GlanceModifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
            Transport(snapshot.state.playing)
        }
    }
}

/** The visual on its own: the style's name in a glass chip, and the style buttons at its sides. */
@Composable
private fun VisualTile(
    snapshot: WidgetSnapshot,
    modifier: GlanceModifier,
) {
    Box(modifier = modifier.cornerRadius(R.dimen.widget_inner_radius)) {
        Image(
            provider = ImageProvider(snapshot.visual),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = GlanceModifier.fillMaxSize(),
        )
        Column(modifier = GlanceModifier.fillMaxSize().padding(4.dp)) {
            Row(
                modifier = GlanceModifier.fillMaxWidth().defaultWeight(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                StyleButton(snapshot.listening, step = -1, onGlass = true)
                Spacer(GlanceModifier.defaultWeight())
                StyleButton(snapshot.listening, step = 1, onGlass = true)
            }
            snapshot.state.styleId?.let { id ->
                Text(
                    text = WidgetVisual.label(id),
                    style = TextStyle(color = TEXT, fontSize = 12.sp, fontWeight = FontWeight.Medium),
                    maxLines = 1,
                    modifier =
                        GlanceModifier
                            .background(ImageProvider(R.drawable.widget_chip))
                            .padding(horizontal = 10.dp, vertical = 4.dp),
                )
            }
        }
    }
}

@Composable
private fun Artwork(
    art: Bitmap?,
    size: Dp,
) {
    Image(
        provider = art?.let(::ImageProvider) ?: ImageProvider(R.drawable.ic_widget_art),
        contentDescription = null,
        contentScale = if (art != null) ContentScale.Crop else ContentScale.Fit,
        modifier =
            GlanceModifier
                .size(size)
                .cornerRadius(R.dimen.widget_inner_radius)
                .then(if (art == null) GlanceModifier.background(RAISED).padding(size / 4) else GlanceModifier),
    )
}

@Composable
private fun TrackText(
    state: WidgetState,
    titleSize: TextUnit,
    artistSize: TextUnit?,
    modifier: GlanceModifier = GlanceModifier,
    styleLabel: String? = null,
) {
    val context = LocalContext.current
    Column(modifier = modifier) {
        Text(
            text = if (state.hasTrack) state.title else context.getString(R.string.widget_nothing_playing),
            style = TextStyle(color = TEXT, fontSize = titleSize, fontWeight = FontWeight.Medium),
            maxLines = 1,
        )
        // The artist, and in the two-row layout the style on screen after it: the picture behind
        // the glass is that style, and its buttons sit just below.
        val second = listOfNotNull(state.artist.takeIf { it.isNotBlank() }, styleLabel).joinToString(" · ")
        if (artistSize != null && second.isNotEmpty()) {
            Text(text = second, style = TextStyle(color = TEXT_SECONDARY, fontSize = artistSize), maxLines = 1)
        }
    }
}

@Composable
private fun Progress(state: WidgetState) {
    LinearProgressIndicator(
        progress = state.progress,
        modifier = GlanceModifier.fillMaxWidth().height(3.dp),
        color = ACCENT,
        backgroundColor = TRACK,
    )
}

@Composable
private fun Times(state: WidgetState) {
    if (state.durationMs <= 0) return
    Row(modifier = GlanceModifier.fillMaxWidth().padding(top = 2.dp)) {
        Text(text = formatTime(state.positionMs), style = TextStyle(color = TEXT_SECONDARY, fontSize = 11.sp))
        Spacer(GlanceModifier.defaultWeight())
        Text(text = formatTime(state.durationMs), style = TextStyle(color = TEXT_SECONDARY, fontSize = 11.sp))
    }
}

@Composable
private fun RowScope.Transport(playing: Boolean) {
    TransportButton(R.drawable.widget_symbol_previous, R.string.widget_previous, WidgetTransportAction.PREVIOUS)
    PlayPauseButton(playing)
    TransportButton(R.drawable.widget_symbol_next, R.string.widget_next, WidgetTransportAction.NEXT)
}

@Composable
private fun PlayPauseButton(playing: Boolean) {
    TransportButton(
        icon = if (playing) R.drawable.widget_symbol_pause else R.drawable.widget_symbol_play,
        label = if (playing) R.string.widget_pause else R.string.widget_play,
        command = WidgetTransportAction.PLAY_PAUSE,
        accent = true,
    )
}

@Composable
private fun TransportButton(
    icon: Int,
    label: Int,
    command: String,
    accent: Boolean = false,
) {
    IconButton(
        icon = icon,
        description = LocalContext.current.getString(label),
        action = actionRunCallback<WidgetTransportAction>(actionParametersOf(WidgetTransportAction.COMMAND to command)),
        tint = if (accent) ON_ACCENT else TEXT,
        container = if (accent) R.drawable.widget_play_container else null,
    )
}

/**
 * A style button: steps the session's style while one listens, else opens the app, where the
 * style can be chosen (a button that silently did nothing would read as broken).
 */
@Composable
private fun StyleButton(
    listening: Boolean,
    step: Int,
    onGlass: Boolean = false,
) {
    val context = LocalContext.current
    val description =
        when {
            !listening -> R.string.widget_style_open
            step > 0 -> R.string.widget_style_next
            else -> R.string.widget_style_previous
        }
    IconButton(
        icon = if (step > 0) R.drawable.widget_symbol_style_next else R.drawable.widget_symbol_style_previous,
        description = context.getString(description),
        action =
            if (listening) {
                actionRunCallback<WidgetStyleAction>(actionParametersOf(WidgetStyleAction.STEP to step))
            } else {
                actionStartActivity(context.appLaunchIntent())
            },
        tint = TEXT,
        container = if (onGlass) R.drawable.widget_chip else null,
        size = if (onGlass) STYLE_ON_GLASS else TOUCH_TARGET,
    )
}

@Composable
private fun IconButton(
    icon: Int,
    description: String,
    action: Action,
    tint: ColorProvider,
    container: Int? = null,
    size: Dp = TOUCH_TARGET,
) {
    Image(
        provider = ImageProvider(icon),
        contentDescription = description,
        colorFilter = ColorFilter.tint(tint),
        modifier =
            GlanceModifier
                .size(size)
                .then(if (container != null) GlanceModifier.background(ImageProvider(container)) else GlanceModifier)
                .padding((size - ICON) / 2)
                .clickable(action),
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

/** A style button's tap, handed to the session through [WidgetStyles]. */
class WidgetStyleAction : ActionCallback {
    override suspend fun onAction(
        context: Context,
        glanceId: GlanceId,
        parameters: ActionParameters,
    ) {
        val step = parameters[STEP] ?: return
        // No session took it: the widget was drawn while one lived and has outlived it. Redraw,
        // so its style buttons open the app from now on instead of failing again.
        if (!WidgetStyles.request(step)) NowPlayingWidget().update(context, glanceId)
    }

    companion object {
        val STEP = ActionParameters.Key<Int>("step")
    }
}

private val EDGE = 12.dp
private val GAP = 10.dp
private val TOUCH_TARGET = 48.dp
private val STYLE_ON_GLASS = 36.dp
private val ICON = 24.dp
private val MEDIUM_ART = 40.dp
private val LARGE_ART = 40.dp

// The biggest artwork any layout draws; the decoded bitmap is capped to it.
private val LARGEST_ART = 48.dp

// Black-glass tokens, copied by hand from the app theme's tokens (ui/theme/GeodeTokens) because
// widget/ may not name the UI layer (tools/layer-gate.sh): OnCanvas, OnSurfaceVariant (#C4C6D0),
// the #9CC7FF accent and its OnPrimary, and GeodeGlass.FILL_RAISED (white at 0.05). They are
// copies, so a token change there must be repeated here. The glass, scrim, chip and accent
// container live in the widget_* drawables.
private val TEXT = ColorProvider(Color(0xFFECECF0))
private val TEXT_SECONDARY = ColorProvider(Color(0xFFC4C6D0))
private val ACCENT = ColorProvider(Color(0xFF9CC7FF))
private val ON_ACCENT = ColorProvider(Color(0xFF00213F))
private val TRACK = ColorProvider(Color(0x33FFFFFF))
private val RAISED = ColorProvider(Color(0x0DFFFFFF))
