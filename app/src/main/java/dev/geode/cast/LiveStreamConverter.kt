package dev.geode.cast

import androidx.annotation.OptIn
import androidx.media3.cast.MediaItemConverter
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.util.UnstableApi
import com.google.android.gms.cast.HlsSegmentFormat
import com.google.android.gms.cast.HlsVideoSegmentFormat
import com.google.android.gms.cast.MediaInfo
import com.google.android.gms.cast.MediaMetadata
import com.google.android.gms.cast.MediaQueueItem

/**
 * Describes the cast stream to the Default Media Receiver: live HLS with fragmented MP4 segments.
 *
 * Media3's `DefaultMediaItemConverter` can't say "fMP4": the receiver then assumes MPEG-TS
 * segments for HLS and fails on [LiveSegments]' CMAF segments. Everything else matches the
 * default: the URL is the content id and the stream type is live.
 */
@OptIn(UnstableApi::class)
internal class LiveStreamConverter(
    private val title: String,
) : MediaItemConverter {
    override fun toMediaQueueItem(mediaItem: MediaItem): MediaQueueItem {
        val url = checkNotNull(mediaItem.localConfiguration).uri.toString()
        val metadata = MediaMetadata(MediaMetadata.MEDIA_TYPE_MOVIE).apply { putString(MediaMetadata.KEY_TITLE, title) }
        val info =
            MediaInfo
                .Builder(url)
                .setContentUrl(url)
                .setContentType(MimeTypes.APPLICATION_M3U8)
                .setStreamType(MediaInfo.STREAM_TYPE_LIVE)
                .setHlsSegmentFormat(HlsSegmentFormat.FMP4)
                .setHlsVideoSegmentFormat(HlsVideoSegmentFormat.FMP4)
                .setMetadata(metadata)
                .build()
        return MediaQueueItem.Builder(info).build()
    }

    override fun toMediaItem(mediaQueueItem: MediaQueueItem): MediaItem =
        MediaItem
            .Builder()
            .setUri(mediaQueueItem.media?.contentUrl ?: mediaQueueItem.media?.contentId)
            .setMimeType(MimeTypes.APPLICATION_M3U8)
            .build()

    companion object {
        /** The item [CastController] loads: the playlist URL (live comes from [toMediaQueueItem]). */
        fun liveItem(playlistUrl: String): MediaItem =
            MediaItem
                .Builder()
                .setUri(playlistUrl)
                .setMimeType(MimeTypes.APPLICATION_M3U8)
                .build()
    }
}
