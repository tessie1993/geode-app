package dev.geode.data

import android.content.ContentResolver
import android.content.ContentValues
import android.net.Uri
import android.provider.MediaStore

/** The two MediaStore calls a render's publish needs, so a test can answer "0 rows changed". */
interface PendingRows<U> {
    /** Clears IS_PENDING; the row count, 0 when nothing changed or the call threw. */
    fun clearPending(row: U): Int

    fun delete(row: U)
}

/**
 * Publishes a finished render: clears IS_PENDING, retrying once, and deletes the row when it still
 * will not publish (a pending row is hidden from the gallery and other apps, so "Saved" would lie).
 * Returns whether the file is now visible.
 */
fun <U> publishPending(
    rows: PendingRows<U>,
    row: U,
): Boolean {
    if (rows.clearPending(row) > 0 || rows.clearPending(row) > 0) return true
    rows.delete(row)
    return false
}

fun ContentResolver.pendingRows(): PendingRows<Uri> =
    object : PendingRows<Uri> {
        override fun clearPending(row: Uri): Int =
            runCatching {
                update(row, ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }, null, null)
            }.getOrDefault(0)

        override fun delete(row: Uri) {
            runCatching { delete(row, null, null) }
        }
    }
