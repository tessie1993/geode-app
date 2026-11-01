package dev.geode.playback

import android.content.Context
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.billing.ProSnapshot
import dev.geode.data.AutoDirectorStore
import dev.geode.data.NoteResponseStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Tells the auto-director (core/viz/Director, through [AutoDirectorStore]) when a new play starts and,
 * while playing, how far in it is, so the free taste can end after its minute. The entitlement is read
 * with [ProSnapshot] like the wallpaper does, so a purchase or a trial reaches the next tick.
 *
 * The same entitlement goes to [NoteResponseStore] (Note & key reactions, Pro and the trial), so a trial
 * that lapses mid-song turns the note layer off within a second, and the director's seed it reads stays
 * the play's. Listener callbacks and the ticker run on the main thread.
 */
class AutoDirectorFeed(
    private val context: Context,
    private val player: Player,
    private val scope: CoroutineScope,
) : Player.Listener {
    private val store = AutoDirectorStore.get(context)
    private val notes = NoteResponseStore.get(context)
    private var ticker: Job? = null

    init {
        newPlay()
        if (player.isPlaying) startTicker()
    }

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        newPlay()
    }

    private fun newPlay() {
        val gate = ProSnapshot.gate(context)
        store.newPlay(gate)
        notes.onGate(gate)
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
                    val gate = ProSnapshot.gate(context)
                    store.onPosition(player.currentPosition, gate)
                    notes.onGate(gate)
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
