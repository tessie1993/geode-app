package dev.geode.data

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.provider.MediaStore
import androidx.annotation.WorkerThread
import dev.geode.RingLog

/**
 * Where shake to screenshot's picture goes: a PNG in Pictures/BeatRealms, inserted with
 * `IS_PENDING` and published once written (no permission: the app owns rows it inserts).
 */
class StillFiles(
    context: Context,
) {
    private val resolver = context.applicationContext.contentResolver

    /** The picture's URI, or null when it could not be written (the row is gone then). */
    @WorkerThread
    fun save(
        still: Bitmap,
        nowMs: Long,
    ): Uri? {
        val values =
            ContentValues().apply {
                put(MediaStore.Images.Media.DISPLAY_NAME, "beatrealms_$nowMs.png")
                put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/BeatRealms")
                put(MediaStore.Images.Media.IS_PENDING, 1)
            }
        val row = runCatching { resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values) }.getOrNull() ?: return null
        val written =
            runCatching {
                resolver.openOutputStream(row)?.use { still.compress(Bitmap.CompressFormat.PNG, 100, it) } == true
            }.onFailure { RingLog.note(TAG, "screenshot write failed", it) }.getOrDefault(false)
        val rows = resolver.pendingRows()
        if (!written) {
            rows.delete(row)
            return null
        }
        return row.takeIf { publishPending(rows, row) }
    }

    private companion object {
        const val TAG = "StillFiles"
    }
}
