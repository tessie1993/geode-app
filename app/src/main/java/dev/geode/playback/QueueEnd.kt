package dev.geode.playback

import android.content.Context
import androidx.media3.common.Player
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.QueueEndAction
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Settings › Behaviour › At queue end: stop (default), start the queue again, or keep going with
 * similar tracks from the library ([LibraryTree.similarTo]). Repeat modes come first: with repeat on,
 * the queue never ends, and this does nothing.
 *
 * It lives with the playback service, so it also works with the app closed, for example while
 * Android Auto or a headset button drives playback.
 */
internal class QueueEnd(
    context: Context,
    private val player: Player,
    private val tree: LibraryTree,
    private val scope: CoroutineScope,
) : Player.Listener {
    private val behaviour = BehaviourStore(GeodePrefsFiles(context).general)

    override fun onPlaybackStateChanged(playbackState: Int) {
        if (playbackState != Player.STATE_ENDED || player.repeatMode != Player.REPEAT_MODE_OFF) return
        when (behaviour.load().queueEnd) {
            QueueEndAction.STOP -> Unit
            QueueEndAction.REPEAT -> {
                player.seekTo(0, 0L)
                player.play()
            }
            QueueEndAction.SIMILAR -> playSimilar()
        }
    }

    private fun playSimilar() {
        val last = player.currentMediaItem?.localConfiguration?.uri?.toString() ?: return
        val queued =
            (0 until player.mediaItemCount).mapNotNullTo(HashSet()) { i ->
                player.getMediaItemAt(i).localConfiguration?.uri?.toString()
            }
        scope.launch {
            val more = withContext(Dispatchers.IO) { tree.similarTo(last, queued) }
            if (more.isEmpty() || player.playbackState != Player.STATE_ENDED) return@launch
            val first = player.mediaItemCount
            player.addMediaItems(more)
            player.seekTo(first, 0L)
            player.play()
        }
    }
}
