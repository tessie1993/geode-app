package dev.geode.playback

import android.content.ContentResolver
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.data.NativeTags
import dev.geode.engine.bridge.GeodeNative
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Reads the ReplayGain tags of the tracks around the one playing, ahead of time, and hands them to the
 * native gain, which owns the gain rule (mode, preamp, clip guard) and the sample it applies from
 * (`core/audio/dsp/ReplayGain`, `GainStage`).
 *
 * This used to work the gain out here and set it from [Player.Listener.onMediaItemTransition], dropping
 * to 0 dB until the tag read came back (#351). That callback runs on the main thread at playout time, a
 * sink buffer or more after the DSP has processed the head of the new track, so each track began at the
 * last one's gain, then jumped to 0 dB, then to its own. Now the current, next and previous items' tags
 * are read whenever the timeline, the current item or the play order changes; native takes a track's
 * gain on its first sample, and keeps the previous gain while a read is still out.
 *
 * Listener callbacks and [configure] run on the main thread; the tag read runs on IO.
 */
class ReplayGain(
    private val resolver: ContentResolver,
    private val scope: CoroutineScope,
    private val dsp: NativeDspProcessor,
) : Player.Listener {
    /** Items whose tags were read or are being read, oldest first; fewer than the native table keeps. */
    private val requested = LinkedHashSet<String>()

    fun configure(
        mode: Int,
        preampDb: Float,
        clipGuard: Boolean,
    ) = GeodeNative.dspSetReplayGain(dsp.nativeHandle(), mode, preampDb, clipGuard)

    override fun onEvents(
        player: Player,
        events: Player.Events,
    ) {
        if (READ_AHEAD_EVENTS.any(events::contains)) readAhead(player)
    }

    /** The current item first, then the one that plays next on its own, then the one before. */
    private fun readAhead(player: Player) {
        val count = player.mediaItemCount
        listOf(player.currentMediaItemIndex, player.nextMediaItemIndex, player.previousMediaItemIndex)
            .filter { it != C.INDEX_UNSET && it < count }
            .forEach { index -> read(player.getMediaItemAt(index)) }
    }

    private fun read(item: MediaItem) {
        val uri = item.localConfiguration?.uri ?: return
        val name = NativeDspProcessor.streamName(item) ?: return
        if (!requested.add(name)) return
        while (requested.size > MAX_REQUESTED) requested.remove(requested.first())
        scope.launch {
            val tags = withContext(Dispatchers.IO) { NativeTags.read(resolver, uri) }
            // An unreadable file goes down as read and untagged (mask 0): native plays it at 0 dB rather
            // than keeping the last track's gain for the whole of it.
            var mask = 0
            val values = listOf(tags?.trackGainDb, tags?.trackPeak, tags?.albumGainDb, tags?.albumPeak)
            values.forEachIndexed { bit, v -> if (v != null) mask = mask or (1 shl bit) }
            val v = values.map { it ?: 0f }
            GeodeNative.dspSetStreamTags(dsp.nativeHandle(), name, mask, v[0], v[1], v[2], v[3])
        }
    }

    companion object {
        const val MODE_OFF = 0
        const val MODE_TRACK = 1
        const val MODE_ALBUM = 2

        /** Below the native table's 32, so nothing asked for here has been pushed out there. */
        private const val MAX_REQUESTED = 16

        /** What changes which items are current, next or previous. */
        private val READ_AHEAD_EVENTS =
            intArrayOf(
                Player.EVENT_TIMELINE_CHANGED,
                Player.EVENT_MEDIA_ITEM_TRANSITION,
                Player.EVENT_SHUFFLE_MODE_ENABLED_CHANGED,
                Player.EVENT_REPEAT_MODE_CHANGED,
            )
    }
}
