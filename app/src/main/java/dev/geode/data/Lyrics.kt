package dev.geode.data

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.LyricLine

data class Lyrics(
    val lines: List<LyricLine>,
    val synced: Boolean,
    val source: String,
) {
    private val timesMs = LongArray(lines.size) { lines[it].timeMs }

    fun indexAt(positionMs: Long): Int = GeodeNative.lyricsIndexAt(timesMs, positionMs)
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
            val text = context.contentResolver.openInputStream(source)?.use(ReadCap.LYRICS::text) ?: return false
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
                    val text = lrc.takeIf { it.isFile }?.inputStream()?.use(ReadCap.LYRICS::text) ?: return null
                    parse(text, context.getString(R.string.lyrics_source_sidecar))
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
                        parse(ReadCap.LYRICS.text(it), context.getString(R.string.lyrics_source_sidecar))
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

    /** The lines of [text] (an .lrc or plain lyrics), read by core/library/Lyrics; null when there is nothing to show. */
    fun parse(
        text: String,
        source: String,
    ): Lyrics? {
        val wire = String(GeodeNative.lyricsParse(text.toByteArray()), Charsets.UTF_8)
        if (wire.isEmpty()) return null
        val rows = wire.split('\n')
        val lines =
            rows.drop(1).filter { it.isNotEmpty() }.map {
                val tab = it.indexOf('\t')
                LyricLine(it.substring(0, tab).toLong(), it.substring(tab + 1))
            }
        return Lyrics(lines, synced = rows[0] == "S", source = source)
    }
}
