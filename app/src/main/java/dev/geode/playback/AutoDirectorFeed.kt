package dev.geode.playback

import android.content.Context
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.billing.ProSnapshot
import dev.geode.data.AutoDirectorStore
import dev.geode.geodeContainer
import dev.geode.pro.FeatureGate
import dev.geode.pro.ProFeature
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Tells the auto-director (core/viz/Director, through [AutoDirectorStore]) when a new play starts and,
 * while playing, how far in it is, so the free taste can end after its minute. The entitlement is read
 * with [ProSnapshot] like the wallpaper does, at each new play and play/pause, and on a tick only while
 * the taste can still end (switch on, not Pro): a buyer's ticks never re-run the purchase signature check,
 * and a purchase still reaches the next tick. Listener callbacks and the ticker run on the main thread.
 */
class AutoDirectorFeed(
    private val context: Context,
    private val player: Player,
    private val scope: CoroutineScope,
) : Player.Listener {
    private val store = context.geodeContainer.autoDirector
    private var ticker: Job? = null
    private var gate: FeatureGate = ProSnapshot.gate(context)

    init {
        store.newPlay(gate)
        store.onPlaying(player.isPlaying, gate)
        if (player.isPlaying) startTicker()
    }

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        gate = ProSnapshot.gate(context)
        store.newPlay(gate)
    }

    override fun onIsPlayingChanged(isPlaying: Boolean) {
        gate = ProSnapshot.gate(context)
        store.onPlaying(isPlaying, gate)
        if (isPlaying) startTicker() else stopTicker()
    }

    fun release() = stopTicker()

    private fun startTicker() {
        if (ticker?.isActive == true) return
        ticker =
            scope.launch {
                while (isActive) {
                    if (store.enabled.value && !gate.allows(ProFeature.AUTO_DIRECTOR)) {
                        gate = ProSnapshot.gate(context)
                    }
                    store.onPosition(player.currentPosition, gate)
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
