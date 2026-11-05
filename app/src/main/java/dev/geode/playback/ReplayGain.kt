package dev.geode.playback

import android.content.ContentResolver
import android.net.Uri
import androidx.media3.common.C
import androidx.media3.common.Player
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.data.NativeTags
import dev.geode.data.TrackTags
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Reads the ReplayGain tags of the tracks around the one playing, ahead of time, and hands them to the
 * native gain stage, which owns the gain rule and the moment it applies (`core/audio/dsp/ReplayGain`).
 *
 * This used to set the gain itself from [Player.Listener.onMediaItemTransition], dropping to 0 dB until
 * the tag read came back (#351). That callback runs on the main thread at playout time, a sink buffer
 * or more after the DSP has already processed the head of the new track, so each track began at the
 * last one's gain, then jumped to 0 dB, then to its own. Now the current, next and previous items'
 * tags are read whenever the timeline, the current item or the play order changes, and the chain picks
 * a track's gain on that track's first sample, keeping the previous gain while a read is still out.
 *
 * Listener callbacks and [configure] run on the main thread; the tag read runs on IO.
 */
class ReplayGain(
    private val resolver: ContentResolver,
    private val scope: CoroutineScope,
    private val dsp: NativeDspProcessor,
) : Player.Listener {
    /** Items whose tags were read or are being read, oldest first; fewer than the native table holds. */
    private val requested = LinkedHashSet<String>()

    fun configure(
        mode: Int,
        preampDb: Float,
        clipGuard: Boolean,
    ) = dsp.configureReplayGain(mode, preampDb, clipGuard)

    override fun onEvents(
        player: Player,
        events: Player.Events,
    ) {
        if (READ_AHEAD_EVENTS.any(events::contains)) readAhead(player)
    }

    /** The current item first, then the one auto-advance plays next, then the one before. */
    private fun readAhead(player: Player) {
        val timeline = player.currentTimeline
        if (timeline.isEmpty) return
        listOf(player.currentMediaItemIndex, player.nextMediaItemIndex, player.previousMediaItemIndex)
            .filter { it != C.INDEX_UNSET && it < timeline.windowCount }
            .forEach { index -> read(player.getMediaItemAt(index).localConfiguration?.uri) }
    }

    private fun read(uri: Uri?) {
        val name = uri?.toString() ?: return
        if (!requested.add(name)) return
        while (requested.size > MAX_REQUESTED) requested.remove(requested.first())
        scope.launch {
            val tags = withContext(Dispatchers.IO) { NativeTags.read(resolver, uri) }
            val values = FloatArray(TAG_VALUES)
            dsp.setReplayGainTags(name, mask(tags, values), values)
        }
    }

    companion object {
        const val MODE_OFF = 0
        const val MODE_TRACK = 1
        const val MODE_ALBUM = 2

        /** Below the native table's 32, so nothing asked for here has been pushed out there. */
        private const val MAX_REQUESTED = 16

        private const val TAG_VALUES = 4

        /** What changes which items are current, next or previous. */
        private val READ_AHEAD_EVENTS =
            intArrayOf(
                Player.EVENT_TIMELINE_CHANGED,
                Player.EVENT_MEDIA_ITEM_TRANSITION,
                Player.EVENT_SHUFFLE_MODE_ENABLED_CHANGED,
                Player.EVENT_REPEAT_MODE_CHANGED,
            )

        /** The tag values in native order, and the mask of the ones the file carries (0 if unreadable). */
        internal fun mask(
            tags: TrackTags?,
            values: FloatArray,
        ): Int {
            if (tags == null) return 0
            var mask = 0
            listOf(tags.trackGainDb, tags.trackPeak, tags.albumGainDb, tags.albumPeak).forEachIndexed { i, v ->
                if (v != null) {
                    values[i] = v
                    mask = mask or (1 shl i)
                }
            }
            return mask
        }
    }
}
