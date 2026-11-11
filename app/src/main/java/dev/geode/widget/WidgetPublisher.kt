package dev.geode.widget

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.PowerManager
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
import kotlinx.coroutines.ExperimentalCoroutinesApi
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
 * move. Nothing ticks while paused, with no widget placed, with the screen off or in battery saver:
 * a widget update wakes the launcher, and a music app that redraws an unseen widget every few
 * seconds costs battery for nothing. Turning the screen on refreshes once, so the bar is current
 * the moment someone can see it.
 *
 * The player is read on the main thread, into plain values; the [WidgetState] write (the first one
 * reads the widget's SharedPreferences) and the redraw run on one serial IO lane, so no event or
 * tick touches disk on main and writes keep the order the events came in.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class WidgetPublisher(
    context: Context,
    private val player: Player,
) : Player.Listener {
    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // The player is only read on its application thread (main); the tick runs there too.
    private val mainScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    // Prefs reads and writes plus the redraw; one at a time so a late event never overwrites a newer one.
    private val stateScope = CoroutineScope(SupervisorJob() + Dispatchers.IO.limitedParallelism(1))
    private var screenOnRegistered = false
    private val screenOn =
        object : BroadcastReceiver() {
            override fun onReceive(
                context: Context,
                intent: Intent,
            ) {
                if (player.isPlaying) refreshProgress(advanceTick = false)
            }
        }
    private var artJob: Job? = null
    private var tickJob: Job? = null

    @Volatile private var artUri: String? = null

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
        val title = titleOf(metadata, uri)
        val artist = metadata.artist?.toString().orEmpty()
        val playing = player.isPlaying
        val position = player.currentPosition.coerceAtLeast(0L)
        val duration = durationOf(player)
        stateScope.launch {
            WidgetState.update(appContext) {
                it.copy(
                    title = title,
                    artist = artist,
                    playing = playing,
                    artPath = if (sameArt) it.artPath else null,
                    artHue = if (sameArt) it.artHue else WidgetState.NO_HUE,
                    artSpan = if (sameArt) it.artSpan else 0f,
                    positionMs = position,
                    durationMs = duration,
                )
            }
            NowPlayingWidget().updateAll(appContext)
        }
        if (!sameArt) {
            artUri = uri
            artJob?.cancel()
            artJob =
                scope.launch {
                    val art = uri?.let { MediaArtwork.decodeEmbedded(appContext, it) }
                    if (uri != artUri) return@launch
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
        // The publisher's own scopes are cancelled right below, which would cancel this write and
        // redraw before they dispatch and leave the widget showing a stale "playing" state; run
        // them on the process-scoped appScope instead so they survive the publisher's teardown.
        appContext.geodeContainer.appScope.launch(Dispatchers.IO) {
            WidgetState.update(appContext) { it.copy(playing = false) }
            NowPlayingWidget().updateAll(appContext)
        }
        scope.cancel()
        stateScope.cancel()
        mainScope.cancel()
    }

    private fun startTick() {
        if (tickJob?.isActive == true) return
        tickJob =
            mainScope.launch {
                while (isActive) {
                    delay(TICK_MS)
                    if (!player.isPlaying) break
                    if (unseen()) continue
                    refreshProgress(advanceTick = true)
                }
            }
        if (!screenOnRegistered) {
            screenOnRegistered = true
            appContext.registerReceiver(screenOn, IntentFilter(Intent.ACTION_SCREEN_ON), Context.RECEIVER_NOT_EXPORTED)
        }
    }

    private fun stopTick() {
        tickJob?.cancel()
        tickJob = null
        if (screenOnRegistered) {
            screenOnRegistered = false
            runCatching { appContext.unregisterReceiver(screenOn) }
        }
    }

    /** True while nobody can see the widget: screen off, or battery saver on. */
    private fun unseen(): Boolean {
        val power = appContext.getSystemService(PowerManager::class.java) ?: return false
        return !power.isInteractive || power.isPowerSaveMode
    }

    /** Reads the player here (main); the write and the redraw run on the state lane. */
    private fun refreshProgress(advanceTick: Boolean) {
        val position = player.currentPosition.coerceAtLeast(0L)
        val duration = durationOf(player)
        stateScope.launch {
            if (!hasWidgets()) return@launch
            WidgetState.update(appContext) {
                it.copy(positionMs = position, durationMs = duration, tick = if (advanceTick) it.tick + 1 else it.tick)
            }
            NowPlayingWidget().updateAll(appContext)
        }
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
        /**
         * How often a playing, visible widget redraws its progress and visual (#878). Every redraw wakes
         * the process and the launcher and the bar is approximate anyway; a minute keeps a four-minute
         * track within a few percent. Transport changes redraw at once, and so does turning the screen on.
         */
        const val TICK_MS = 60_000L
    }
}
