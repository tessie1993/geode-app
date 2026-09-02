package dev.geode.widget

import android.content.Context
import androidx.glance.appwidget.updateAll
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import dev.geode.playback.MediaArtwork
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/** Mirrors the session's player into [WidgetState] and redraws the widget; artwork is decoded off the main thread. */
class WidgetPublisher(
    context: Context,
    private val player: Player,
) : Player.Listener {
    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var artJob: Job? = null
    private var artUri: String? = null

    override fun onEvents(
        player: Player,
        events: Player.Events,
    ) {
        if (events.containsAny(
                Player.EVENT_MEDIA_METADATA_CHANGED,
                Player.EVENT_MEDIA_ITEM_TRANSITION,
                Player.EVENT_IS_PLAYING_CHANGED,
                Player.EVENT_PLAYBACK_STATE_CHANGED,
            )
        ) {
            publish()
        }
    }

    fun publish() {
        val uri = player.currentMediaItem?.localConfiguration?.uri?.toString()
        val metadata = player.mediaMetadata
        val previous = WidgetState.load(appContext)
        val state =
            WidgetState(
                title = titleOf(metadata, uri),
                artist = metadata.artist?.toString().orEmpty(),
                playing = player.isPlaying,
                artPath = if (uri == artUri) previous.artPath else null,
            )
        WidgetState.save(appContext, state)
        scope.launch { NowPlayingWidget().updateAll(appContext) }
        if (uri != artUri) {
            artUri = uri
            artJob?.cancel()
            artJob =
                scope.launch {
                    val art = uri?.let { MediaArtwork.decodeEmbedded(appContext, it, ART_PX) }
                    val path = WidgetState.writeArt(appContext, art)
                    WidgetState.save(appContext, WidgetState.load(appContext).copy(artPath = path))
                    NowPlayingWidget().updateAll(appContext)
                }
        }
    }

    fun clear() {
        WidgetState.save(appContext, WidgetState.load(appContext).copy(playing = false))
        scope.launch { NowPlayingWidget().updateAll(appContext) }
        scope.cancel()
    }

    private fun titleOf(
        metadata: MediaMetadata,
        uri: String?,
    ): String = metadata.title?.toString()?.ifBlank { null } ?: uri?.substringAfterLast('/').orEmpty()

    private companion object {
        const val ART_PX = 256
    }
}
