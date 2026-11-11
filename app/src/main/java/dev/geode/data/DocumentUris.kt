package dev.geode.data

import android.content.ContentResolver
import android.net.Uri
import android.provider.OpenableColumns

/**
 * Keeps the grant a picker offered for [uri] across restarts ([flags]: read, write or both). It
 * has to run while the picker's own grant is still active. False when the provider refuses; the
 * callers treat the grant as best effort, or check what is held afterwards.
 */
fun ContentResolver.persistGrant(
    uri: Uri,
    flags: Int,
): Boolean = runCatching { takePersistableUriPermission(uri, flags) }.isSuccess

/**
 * The name the provider shows for the document at [uri], or null when it has none or the query
 * fails. Only the query: each caller keeps its own fallback and extension handling.
 */
fun ContentResolver.displayNameOf(uri: Uri): String? =
    runCatching {
        query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    }.getOrNull()
