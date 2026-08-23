package dev.geode.export

import android.content.ContentUris
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.MediaStore

data class StudioClip(
    val uri: String,
    val name: String,
    val durationMs: Long,
    val sizeBytes: Long,
    val width: Int = 0,
    val height: Int = 0,
) {
    fun summary(): String =
        buildList {
            if (width > 0 && height > 0) add("$width×$height")
            if (durationMs > 0) add("%d:%02d".format(durationMs / 60_000, (durationMs / 1000) % 60))
            if (sizeBytes > 0) add("%.0f MB".format(sizeBytes / (1024f * 1024f)))
        }.joinToString(" · ")
}

object StudioClips {
    fun list(context: Context): List<StudioClip> {
        val out = mutableListOf<StudioClip>()
        val projection =
            arrayOf(
                MediaStore.Video.Media._ID,
                MediaStore.Video.Media.DISPLAY_NAME,
                MediaStore.Video.Media.DURATION,
                MediaStore.Video.Media.SIZE,
                MediaStore.Video.Media.WIDTH,
                MediaStore.Video.Media.HEIGHT,
            )
        val selection =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                "${MediaStore.Video.Media.RELATIVE_PATH} LIKE ?"
            } else {
                "${MediaStore.Video.Media.DATA} LIKE ?"
            }
        val args =
            arrayOf(
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) "Movies/Geode%" else "%/Movies/Geode/%",
            )
        runCatching {
            context.contentResolver
                .query(
                    MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
                    projection,
                    selection,
                    args,
                    "${MediaStore.Video.Media.DATE_ADDED} DESC",
                )?.use { c ->
                    val id = c.getColumnIndexOrThrow(MediaStore.Video.Media._ID)
                    val name = c.getColumnIndexOrThrow(MediaStore.Video.Media.DISPLAY_NAME)
                    val duration = c.getColumnIndexOrThrow(MediaStore.Video.Media.DURATION)
                    val size = c.getColumnIndexOrThrow(MediaStore.Video.Media.SIZE)
                    val width = c.getColumnIndexOrThrow(MediaStore.Video.Media.WIDTH)
                    val height = c.getColumnIndexOrThrow(MediaStore.Video.Media.HEIGHT)
                    while (c.moveToNext()) {
                        out +=
                            StudioClip(
                                uri =
                                    ContentUris
                                        .withAppendedId(
                                            MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
                                            c.getLong(id),
                                        ).toString(),
                                name = c.getString(name) ?: "Clip",
                                durationMs = c.getLong(duration),
                                sizeBytes = c.getLong(size),
                                width = c.getInt(width),
                                height = c.getInt(height),
                            )
                    }
                }
        }
        return out
    }

    fun describe(
        context: Context,
        uri: android.net.Uri,
    ): StudioClip {
        var durationMs = 0L
        var width = 0
        var height = 0
        runCatching {
            val retriever = android.media.MediaMetadataRetriever()
            try {
                retriever.setDataSource(context, uri)
                durationMs =
                    retriever
                        .extractMetadata(android.media.MediaMetadataRetriever.METADATA_KEY_DURATION)
                        ?.toLongOrNull() ?: 0L
                width =
                    retriever
                        .extractMetadata(android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)
                        ?.toIntOrNull() ?: 0
                height =
                    retriever
                        .extractMetadata(android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)
                        ?.toIntOrNull() ?: 0
            } finally {
                retriever.release()
            }
        }
        val name =
            runCatching {
                androidx.documentfile.provider.DocumentFile
                    .fromSingleUri(context, uri)
                    ?.name
            }.getOrNull() ?: uri.lastPathSegment ?: "Clip"
        return StudioClip(uri.toString(), name, durationMs, 0L, width, height)
    }

    fun delete(
        context: Context,
        uri: String,
    ): Boolean =
        runCatching {
            context.contentResolver.delete(Uri.parse(uri), null, null) > 0
        }.getOrDefault(false)

    @Volatile
    internal var lastRenameDiagnostic: String? = null

    @Suppress("ReturnCount")
    fun rename(
        context: Context,
        uri: String,
        name: String,
    ): Boolean {
        val trimmed = name.trim()
        if (trimmed.isEmpty()) return false
        if (trimmed.any { it == '/' || it == '\\' }) return false
        val parsed = runCatching { Uri.parse(uri) }.getOrNull() ?: return false
        val before = currentName(context, parsed)
        val extension = before?.substringAfterLast('.', "")?.takeIf { it.isNotEmpty() }
        val display = if (extension == null) trimmed else "$trimmed.$extension"
        if (display == before) return true
        val log = StringBuilder("rename '$before' -> '$display'")
        val renamed =
            runCatching {
                val resolver = context.contentResolver
                val direct = runCatching { resolver.update(parsed, displayName(display), null, null) }
                log.append("; direct=").append(direct.exceptionOrNull()?.toString() ?: direct.getOrNull())
                var after = settledName(context, parsed, display, DIRECT_SETTLE_ATTEMPTS)
                log.append(" now='").append(after).append('\'')
                if (after != display) {
                    renameWhilePending(context, parsed, display, log)
                    after = settledName(context, parsed, display, SETTLE_ATTEMPTS)
                    log.append("; final='").append(after).append('\'')
                }
                var ok = after == display
                if (!ok) {
                    val newRow = rowIdByName(context, display)
                    val oldRow = before?.let { rowIdByName(context, it) }
                    log
                        .append("; byName new=")
                        .append(newRow ?: "none")
                        .append(" old=")
                        .append(oldRow ?: "none")
                    ok = newRow != null && oldRow == null
                }
                ok
            }.getOrElse {
                log.append("; threw=").append(it)
                false
            }
        lastRenameDiagnostic = log.toString()
        return renamed
    }

    private fun displayName(display: String): ContentValues = ContentValues().apply { put(MediaStore.Video.Media.DISPLAY_NAME, display) }

    private fun renameWhilePending(
        context: Context,
        uri: Uri,
        display: String,
        log: StringBuilder,
    ) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        val resolver = context.contentResolver
        val pending = ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 1) }
        val published = ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }
        val p1 = runCatching { resolver.update(uri, pending, null, null) }
        log.append("; pend=").append(p1.exceptionOrNull()?.toString() ?: p1.getOrNull())
        try {
            val p2 = runCatching { resolver.update(uri, displayName(display), null, null) }
            log.append(" upd=").append(p2.exceptionOrNull()?.toString() ?: p2.getOrNull())
        } finally {
            val p3 = runCatching { resolver.update(uri, published, null, null) }
            log.append(" pub=").append(p3.exceptionOrNull()?.toString() ?: p3.getOrNull())
        }
    }

    private fun rowIdByName(
        context: Context,
        display: String,
    ): Long? =
        runCatching {
            context.contentResolver
                .query(
                    MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
                    arrayOf(MediaStore.Video.Media._ID),
                    "${MediaStore.Video.Media.DISPLAY_NAME} = ?",
                    arrayOf(display),
                    null,
                )?.use { c -> if (c.moveToFirst()) c.getLong(0) else null }
        }.getOrNull()

    private fun currentName(
        context: Context,
        uri: Uri,
    ): String? =
        runCatching {
            context.contentResolver
                .query(uri, arrayOf(MediaStore.Video.Media.DISPLAY_NAME), null, null, null)
                ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
        }.getOrNull()

    private fun settledName(
        context: Context,
        uri: Uri,
        display: String,
        attempts: Int,
    ): String? {
        var name: String? = null
        repeat(attempts) {
            name = currentName(context, uri) ?: pendingVisibleName(context, uri)
            if (name == display) return name
            android.os.SystemClock.sleep(SETTLE_STEP_MS)
        }
        return name
    }

    private fun pendingVisibleName(
        context: Context,
        uri: Uri,
    ): String? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return null
        val projection = arrayOf(MediaStore.Video.Media.DISPLAY_NAME)
        return runCatching {
            val cursor =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    val args =
                        android.os.Bundle().apply {
                            putInt(MediaStore.QUERY_ARG_MATCH_PENDING, MediaStore.MATCH_INCLUDE)
                        }
                    context.contentResolver.query(uri, projection, args, null)
                } else {
                    @Suppress("DEPRECATION")
                    context.contentResolver.query(MediaStore.setIncludePending(uri), projection, null, null, null)
                }
            cursor?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
        }.getOrNull()
    }

    private const val SETTLE_ATTEMPTS = 24

    private const val DIRECT_SETTLE_ATTEMPTS = 6
    private const val SETTLE_STEP_MS = 50L
}
