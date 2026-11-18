package dev.geode.playback

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import androidx.core.net.toUri

object MediaArtwork {
    /**
     * Media-item extras key carrying the track's own content URI, so [SessionBitmapLoader] can
     * pull artwork out of the file's embedded picture.
     *
     * The obvious encoding — `setArtworkUri(trackUri)` — is wrong: media3 republishes
     * `artworkUri` to the platform session as `METADATA_KEY_ALBUM_ART_URI`, and every
     * out-of-process consumer (system media controls, AVRCP) then tries to image-decode an
     * audio file. On the OPPO CPH2797 that produced a steady stream of SystemUI
     * `ImageDecoder.DecodeException: 'unimplemented'` failures — ~60 in one short session.
     * Keeping the URI in extras keeps it private to our own loader; media3 still hands the
     * decoded bitmap to the platform as `METADATA_KEY_ALBUM_ART`.
     */
    const val EXTRA_EMBEDDED_ART_URI: String = "dev.geode.playback.EMBEDDED_ART_URI"

    /** Extras announcing that [uri]'s embedded picture is this item's artwork source. */
    fun embeddedArtExtras(uri: String): Bundle = Bundle(1).apply { putString(EXTRA_EMBEDDED_ART_URI, uri) }

    fun decodeEmbedded(
        context: Context,
        uri: String,
        maxPx: Int = DEFAULT_MAX_PX,
    ): Bitmap? = runCatching { decodeEmbeddedOrThrow(context, uri, maxPx) }.getOrNull()

    /** Null only when the file has no picture; a file that cannot be read, or a decode that fails, throws. */
    fun decodeEmbeddedOrThrow(
        context: Context,
        uri: String,
        maxPx: Int = DEFAULT_MAX_PX,
    ): Bitmap? {
        val retriever = android.media.MediaMetadataRetriever()
        val bytes =
            try {
                retriever.setDataSource(context, uri.toUri())
                retriever.embeddedPicture
            } finally {
                retriever.release()
            } ?: return null
        return decodeBytesOrThrow(bytes, maxPx)
    }

    fun decodeBytes(
        bytes: ByteArray,
        maxPx: Int = DEFAULT_MAX_PX,
    ): Bitmap? = runCatching { decodeBytesOrThrow(bytes, maxPx) }.getOrNull()

    private fun decodeBytesOrThrow(
        bytes: ByteArray,
        maxPx: Int,
    ): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        val longest = maxOf(bounds.outWidth, bounds.outHeight)
        if (longest <= 0) return null
        var sample = 1
        while ((longest / (sample * 2)) >= maxPx) sample *= 2
        return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
    }

    const val DEFAULT_MAX_PX: Int = 384
}
