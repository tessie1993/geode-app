package dev.geode.playback

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri

object MediaArtwork {
    fun decodeEmbedded(
        context: Context,
        uri: String,
        maxPx: Int = DEFAULT_MAX_PX,
    ): Bitmap? =
        runCatching {
            val retriever = android.media.MediaMetadataRetriever()
            val bytes =
                try {
                    retriever.setDataSource(context, Uri.parse(uri))
                    retriever.embeddedPicture
                } finally {
                    retriever.release()
                } ?: return null
            decodeBytes(bytes, maxPx)
        }.getOrNull()

    fun decodeBytes(
        bytes: ByteArray,
        maxPx: Int = DEFAULT_MAX_PX,
    ): Bitmap? =
        runCatching {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
            val longest = maxOf(bounds.outWidth, bounds.outHeight)
            if (longest <= 0) return null
            var sample = 1
            while (longest / (sample * 2) >= maxPx) sample *= 2
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
        }.getOrNull()

    const val DEFAULT_MAX_PX: Int = 384
}
