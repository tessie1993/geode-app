package dev.geode.data

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import dev.geode.R
import dev.geode.model.LyricLine

data class Lyrics(
    val lines: List<LyricLine>,
    val synced: Boolean,
    val source: String,
) {
    fun indexAt(positionMs: Long): Int {
        if (!synced || lines.isEmpty()) return -1
        var lo = 0
        var hi = lines.size - 1
        var found = -1
        while (lo <= hi) {
            val mid = (lo + hi) / 2
            if (lines[mid].timeMs <= positionMs) {
                found = mid
                lo = mid + 1
            } else {
                hi = mid - 1
            }
        }
        return found
    }
}

object LyricsLoader {
    fun load(
        context: Context,
        trackUri: Uri,
    ): Lyrics? = attached(context, trackUri) ?: sidecar(context, trackUri) ?: embedded(context, trackUri)

    /**
     * Where an .lrc the user loaded by hand (Settings › Lyrics) is kept for [trackUri]: in the
     * app's own files, keyed by a hash of the URI, because a track behind a content URI has no
     * folder the app may write a sidecar into. It wins over the sidecar and the tags, since the
     * user chose it for this song.
     */
    fun attachedFile(
        context: Context,
        trackUri: Uri,
    ): java.io.File {
        val digest =
            java.security.MessageDigest
                .getInstance("SHA-1")
                .digest(trackUri.toString().toByteArray())
        val name = digest.joinToString("") { "%02x".format(it) }
        return java.io.File(java.io.File(context.filesDir, ATTACHED_DIR), "$name.lrc")
    }

    /** Copies the .lrc at [source] into [attachedFile] for [trackUri]; false when it cannot be read. */
    fun attach(
        context: Context,
        trackUri: Uri,
        source: Uri,
    ): Boolean =
        runCatching {
            val text = context.contentResolver.openInputStream(source)?.use { it.readBytes().toString(Charsets.UTF_8) } ?: return false
            val file = attachedFile(context, trackUri)
            file.parentFile?.mkdirs()
            file.writeText(text)
            true
        }.getOrDefault(false)

    private fun attached(
        context: Context,
        trackUri: Uri,
    ): Lyrics? =
        runCatching {
            val file = attachedFile(context, trackUri)
            if (file.isFile) parse(file.readText(), context.getString(R.string.lyrics_source_attached)) else null
        }.getOrNull()

    private const val ATTACHED_DIR = "lyrics"

    private fun sidecar(
        context: Context,
        trackUri: Uri,
    ): Lyrics? =
        runCatching {
            when (trackUri.scheme) {
                "file" -> {
                    val path = trackUri.path ?: return null
                    val lrc = java.io.File(path.substringBeforeLast('.', path) + ".lrc")
                    if (lrc.isFile) parse(lrc.readText(), context.getString(R.string.lyrics_source_sidecar)) else null
                }
                "content" -> {
                    val doc = DocumentFile.fromSingleUri(context, trackUri) ?: return null
                    val name = doc.name ?: return null
                    val stem = name.substringBeforeLast('.', name)
                    val sibling =
                        doc.parentFile?.listFiles()?.firstOrNull {
                            it.name.equals("$stem.lrc", ignoreCase = true)
                        } ?: return null
                    context.contentResolver.openInputStream(sibling.uri)?.use {
                        parse(it.readBytes().toString(Charsets.UTF_8), context.getString(R.string.lyrics_source_sidecar))
                    }
                }
                else -> null
            }
        }.getOrNull()

    private fun embedded(
        context: Context,
        trackUri: Uri,
    ): Lyrics? =
        runCatching {
            val retriever = android.media.MediaMetadataRetriever()
            val raw =
                try {
                    retriever.setDataSource(context, trackUri)
                    retriever.extractMetadata(android.media.MediaMetadataRetriever.METADATA_KEY_WRITER)
                } finally {
                    retriever.release()
                }
            raw?.takeIf { it.isNotBlank() }?.let { parse(it, context.getString(R.string.lyrics_source_tags)) }
        }.getOrNull()

    fun parse(
        text: String,
        source: String,
    ): Lyrics? {
        if (text.isBlank()) return null
        val timed = ArrayList<LyricLine>()
        val plain = ArrayList<LyricLine>()
        for (raw in text.lines()) {
            val stamps = TAG.findAll(raw).toList()
            val body = raw.replace(TAG, "").trim()
            if (stamps.isEmpty()) {
                if (body.isNotBlank() && !METADATA.matches(raw.trim())) plain += LyricLine(-1, body)
                continue
            }
            if (body.isBlank()) continue
            for (stamp in stamps) {
                val minutes = stamp.groupValues[1].toLongOrNull() ?: continue
                val seconds = stamp.groupValues[2].toLongOrNull() ?: continue
                val fractionText = stamp.groupValues[3]
                val fraction = fractionText.toLongOrNull() ?: 0L
                val fractionMs =
                    when (fractionText.length) {
                        0 -> 0L
                        1 -> fraction * 100
                        2 -> fraction * 10
                        else -> fraction
                    }
                timed += LyricLine(minutes * 60_000 + seconds * 1_000 + fractionMs, body)
            }
        }
        return when {
            timed.isNotEmpty() -> Lyrics(timed.sortedBy { it.timeMs }, synced = true, source = source)
            plain.isNotEmpty() -> Lyrics(plain, synced = false, source = source)
            else -> null
        }
    }

    private val TAG = Regex("""\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?]""")

    private val METADATA = Regex("""^\[[a-zA-Z]+:.*]$""")
}
