package dev.geode.playback

import android.content.Context
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.billing.ProSnapshot
import dev.geode.data.AutoDirectorStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Tells the auto-director (core/viz/Director, through [AutoDirectorStore]) when a new play starts and,
 * while playing, how far in it is, so the free taste can end after its minute. The entitlement is read
 * with [ProSnapshot] like the wallpaper does, so a purchase or a trial reaches the next tick.
 * Listener callbacks and the ticker run on the main thread.
 */
class AutoDirectorFeed(
    private val context: Context,
    private val player: Player,
    private val scope: CoroutineScope,
) : Player.Listener {
    private val store = AutoDirectorStore.get(context)
    private var ticker: Job? = null

    init {
        store.newPlay(ProSnapshot.gate(context))
        if (player.isPlaying) startTicker()
    }

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        store.newPlay(ProSnapshot.gate(context))
    }

    override fun onIsPlayingChanged(isPlaying: Boolean) {
        if (isPlaying) startTicker() else stopTicker()
    }

    fun release() = stopTicker()

    private fun startTicker() {
        if (ticker?.isActive == true) return
        ticker =
            scope.launch {
                while (isActive) {
                    store.onPosition(player.currentPosition, ProSnapshot.gate(context))
                    delay(TICK_MS)
                }
            }
    }

    private fun stopTicker() {
        ticker?.cancel()
        ticker = null
    }

    private companion object {
        const val TICK_MS = 1_000L
    }
}
