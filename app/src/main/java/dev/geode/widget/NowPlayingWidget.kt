package dev.geode.widget

import android.content.ComponentName
import android.content.Context
import android.graphics.BitmapFactory
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.Image
import androidx.glance.ImageProvider
import androidx.glance.LocalContext
import androidx.glance.action.ActionParameters
import androidx.glance.action.actionParametersOf
import androidx.glance.action.actionStartActivity
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.action.ActionCallback
import androidx.glance.appwidget.action.actionRunCallback
import androidx.glance.appwidget.cornerRadius
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.layout.Alignment
import androidx.glance.layout.Column
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
import dev.geode.playback.PlaybackService
import dev.geode.ui.MainActivity

/** Artwork, title, artist and transport, redrawn whenever the playback service publishes a change. */
class NowPlayingWidget : GlanceAppWidget() {
    override suspend fun provideGlance(
        context: Context,
        id: GlanceId,
    ) {
        val state = WidgetState.load(context)
        val art = state.artPath?.let { path -> runCatching { BitmapFactory.decodeFile(path) }.getOrNull() }
        provideContent { NowPlaying(state, art?.let(::ImageProvider)) }
    }
}

class NowPlayingWidgetReceiver : GlanceAppWidgetReceiver() {
    override val glanceAppWidget: GlanceAppWidget = NowPlayingWidget()
}

@Composable
private fun NowPlaying(
    state: WidgetState,
    art: ImageProvider?,
) {
    val context = LocalContext.current
    Row(
        modifier =
            GlanceModifier
                .fillMaxSize()
                .background(ColorProvider(BACKGROUND))
                .cornerRadius(18.dp)
                .padding(12.dp)
                .clickable(actionStartActivity<MainActivity>()),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Image(
            provider = art ?: ImageProvider(R.drawable.ic_widget_art),
            contentDescription = null,
            modifier = GlanceModifier.size(56.dp).cornerRadius(10.dp),
        )
        Column(modifier = GlanceModifier.defaultWeight().padding(horizontal = 12.dp)) {
            Text(
                text = if (state.hasTrack) state.title else context.getString(R.string.widget_nothing_playing),
                style = TextStyle(color = ColorProvider(FOREGROUND), fontSize = 14.sp, fontWeight = FontWeight.Medium),
                maxLines = 1,
            )
            if (state.artist.isNotBlank()) {
                Text(text = state.artist, style = TextStyle(color = ColorProvider(MUTED), fontSize = 12.sp), maxLines = 1)
            }
        }
        TransportButton(R.drawable.ic_widget_prev, R.string.widget_previous, WidgetTransportAction.PREVIOUS)
        TransportButton(
            if (state.playing) R.drawable.ic_widget_pause else R.drawable.ic_widget_play,
            if (state.playing) R.string.widget_pause else R.string.widget_play,
            WidgetTransportAction.PLAY_PAUSE,
        )
        TransportButton(R.drawable.ic_widget_next, R.string.widget_next, WidgetTransportAction.NEXT)
    }
}

@Composable
private fun TransportButton(
    icon: Int,
    label: Int,
    command: String,
) {
    val context = LocalContext.current
    Image(
        provider = ImageProvider(icon),
        contentDescription = context.getString(label),
        modifier =
            GlanceModifier
                .size(40.dp)
                .padding(6.dp)
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

private val BACKGROUND = Color(0xFF101418)
private val FOREGROUND = Color(0xFFF2F4F8)
private val MUTED = Color(0xFFA9B1BD)
