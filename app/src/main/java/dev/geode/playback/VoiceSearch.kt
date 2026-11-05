package dev.geode.playback

import android.provider.MediaStore
import androidx.media3.common.MediaItem

/**
 * A "play X" request as the media session receives it (Android Auto voice, Assistant). Media3 turns the
 * legacy `playFromSearch` / `prepareFromSearch` into one [MediaItem] with no URI, an empty media id and the
 * spoken words in `requestMetadata.searchQuery`, plus MediaStore's focus extras when the assistant parsed the
 * sentence. Handed to ExoPlayer unchanged, that item has no `localConfiguration` and `DefaultMediaSourceFactory`
 * rejects it (#382), so [PlaybackService] turns it into library tracks first.
 *
 * Only the reading of Media3's fields is here; which tracks a request plays is native
 * (`core/library/Search`, through [LibraryTree.search]).
 */
data class VoiceSearch(
    val query: String,
    val focus: String? = null,
    val artist: String? = null,
    val album: String? = null,
    val title: String? = null,
) {
    companion object {
        /**
         * The search [item] asks for, or null when it is something else: an item with a URI already, a
         * browse-tree id, or a play-from-URI request (`requestMetadata.mediaUri`). An item with neither a
         * query nor an id ("play music on MuzViz") is a blank search, which plays the library.
         */
        fun of(item: MediaItem): VoiceSearch? {
            if (item.localConfiguration != null) return null
            val meta = item.requestMetadata
            val query = meta.searchQuery
            if (query == null && (item.mediaId != MediaItem.DEFAULT_MEDIA_ID || meta.mediaUri != null)) return null
            val extras = meta.extras
            return VoiceSearch(
                query = query.orEmpty(),
                focus = extras?.getString(MediaStore.EXTRA_MEDIA_FOCUS),
                artist = extras?.getString(MediaStore.EXTRA_MEDIA_ARTIST),
                album = extras?.getString(MediaStore.EXTRA_MEDIA_ALBUM),
                title = extras?.getString(MediaStore.EXTRA_MEDIA_TITLE),
            )
        }
    }
}
