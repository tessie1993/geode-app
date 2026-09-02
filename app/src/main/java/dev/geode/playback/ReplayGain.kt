package dev.geode.playback

import android.content.ContentResolver
import android.net.Uri
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.data.NativeTags
import dev.geode.data.TrackTags
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.log10
import kotlin.math.min

/**
 * Reads the ReplayGain tags of every track the player moves to and drives the gain stage with them.
 * Listener callbacks and [configure] run on the main thread; the tag read runs on IO.
 */
class ReplayGain(
    private val resolver: ContentResolver,
    private val scope: CoroutineScope,
    private val applyGainDb: (Float) -> Unit,
) : Player.Listener {
    private var mode = MODE_OFF
    private var preampDb = 0f
    private var clipGuard = true
    private var uri: Uri? = null
    private var tags: TrackTags? = null
    private var read: Job? = null

    fun configure(
        mode: Int,
        preampDb: Float,
        clipGuard: Boolean,
    ) {
        this.mode = mode
        this.preampDb = preampDb
        this.clipGuard = clipGuard
        push()
    }

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        val next = mediaItem?.localConfiguration?.uri
        if (next == uri) return
        uri = next
        tags = null
        read?.cancel()
        push()
        if (next == null) return
        read =
            scope.launch {
                val found = withContext(Dispatchers.IO) { NativeTags.read(resolver, next) }
                if (uri == next) {
                    tags = found
                    push()
                }
            }
    }

    private fun push() = applyGainDb(gainDb(tags, mode, preampDb, clipGuard))

    companion object {
        const val MODE_OFF = 0
        const val MODE_TRACK = 1
        const val MODE_ALBUM = 2

        /** The tag gain plus preamp, held below the level at which the tagged peak would clip. */
        fun gainDb(
            tags: TrackTags?,
            mode: Int,
            preampDb: Float,
            clipGuard: Boolean,
        ): Float {
            if (mode == MODE_OFF || tags == null) return 0f
            val album = mode == MODE_ALBUM
            val gain = (if (album) tags.albumGainDb ?: tags.trackGainDb else tags.trackGainDb ?: tags.albumGainDb) ?: return 0f
            val peak = if (album) tags.albumPeak ?: tags.trackPeak else tags.trackPeak ?: tags.albumPeak
            val wanted = gain + preampDb
            if (!clipGuard || peak == null || peak <= 0f) return wanted
            return min(wanted, -20f * log10(peak))
        }
    }
}
