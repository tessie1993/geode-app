package dev.geode.widget

import android.content.Context
import androidx.glance.appwidget.GlanceAppWidgetManager
import androidx.glance.appwidget.updateAll
import androidx.media3.common.C
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import dev.geode.geodeContainer
import dev.geode.playback.MediaArtwork
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Mirrors the session's player into [WidgetState] and redraws the widget; artwork is decoded off
 * the main thread.
 *
 * While the player plays and a widget is on the home screen, a slow tick ([TICK_MS]) writes the
 * position again and advances [WidgetState.tick], so the progress bar and the generated visual
 * move. Nothing ticks while paused or with no widget placed: a widget update wakes the launcher,
 * and a music app that redraws an unseen widget every few seconds costs battery for nothing.
 */
class WidgetPublisher(
    context: Context,
    private val player: Player,
) : Player.Listener {
    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // The player is only read on its application thread (main); the tick runs there too.
    private val mainScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var artJob: Job? = null
    private var tickJob: Job? = null
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
                Player.EVENT_POSITION_DISCONTINUITY,
                Player.EVENT_TIMELINE_CHANGED,
            )
        ) {
            publish()
        }
    }

    fun publish() {
        val uri =
            player.currentMediaItem
                ?.localConfiguration
                ?.uri
                ?.toString()
        val metadata = player.mediaMetadata
        val sameArt = uri == artUri
        WidgetState.update(appContext) {
            it.copy(
                title = titleOf(metadata, uri),
                artist = metadata.artist?.toString().orEmpty(),
                playing = player.isPlaying,
                artPath = if (sameArt) it.artPath else null,
                artHue = if (sameArt) it.artHue else WidgetState.NO_HUE,
                artSpan = if (sameArt) it.artSpan else 0f,
                positionMs = player.currentPosition.coerceAtLeast(0L),
                durationMs = durationOf(player),
            )
        }
        scope.launch { NowPlayingWidget().updateAll(appContext) }
        if (!sameArt) {
            artUri = uri
            artJob?.cancel()
            artJob =
                scope.launch {
                    val art = uri?.let { MediaArtwork.decodeEmbedded(appContext, it) }
                    val path = WidgetState.writeArt(appContext, art)
                    val palette = art?.let(WidgetVisual::paletteOf)
                    WidgetState.update(appContext) {
                        it.copy(
                            artPath = path,
                            artHue = palette?.baseHue ?: WidgetState.NO_HUE,
                            artSpan = palette?.span ?: 0f,
                        )
                    }
                    NowPlayingWidget().updateAll(appContext)
                }
        }
        if (player.isPlaying) startTick() else stopTick()
    }

    fun clear() {
        stopTick()
        WidgetState.update(appContext) { it.copy(playing = false) }
        // The publisher's own scope is cancelled right below, which would cancel this redraw
        // before it dispatches and leave the widget showing a stale "playing" state; run it on
        // the process-scoped appScope instead so it survives the publisher's teardown.
        appContext.geodeContainer.appScope.launch { NowPlayingWidget().updateAll(appContext) }
        scope.cancel()
        mainScope.cancel()
    }

    private fun startTick() {
        if (tickJob?.isActive == true) return
        tickJob =
            mainScope.launch {
                while (isActive) {
                    delay(TICK_MS)
                    if (!player.isPlaying) break
                    val position = player.currentPosition.coerceAtLeast(0L)
                    val duration = durationOf(player)
                    if (hasWidgets()) {
                        WidgetState.update(appContext) { it.copy(positionMs = position, durationMs = duration, tick = it.tick + 1) }
                        NowPlayingWidget().updateAll(appContext)
                    }
                }
            }
    }

    private fun stopTick() {
        tickJob?.cancel()
        tickJob = null
    }

    private suspend fun hasWidgets(): Boolean =
        runCatching {
            GlanceAppWidgetManager(appContext).getGlanceIds(NowPlayingWidget::class.java).isNotEmpty()
        }.getOrDefault(false)

    private fun durationOf(player: Player): Long = player.duration.takeIf { it != C.TIME_UNSET && it > 0 } ?: 0L

    private fun titleOf(
        metadata: MediaMetadata,
        uri: String?,
    ): String = metadata.title?.toString()?.ifBlank { null } ?: uri?.substringAfterLast('/').orEmpty()

    private companion object {
        /** How often a playing widget redraws its progress and visual: often enough to see it move, rare enough to cost nothing. */
        const val TICK_MS = 20_000L
    }
}
