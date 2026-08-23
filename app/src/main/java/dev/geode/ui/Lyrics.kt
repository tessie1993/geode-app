package dev.geode.ui

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile

data class LyricLine(
    val timeMs: Long,
    val text: String,
)

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
    ): Lyrics? = sidecar(context, trackUri) ?: embedded(context, trackUri)

    private fun sidecar(
        context: Context,
        trackUri: Uri,
    ): Lyrics? =
        runCatching {
            when (trackUri.scheme) {
                "file" -> {
                    val path = trackUri.path ?: return null
                    val lrc = java.io.File(path.substringBeforeLast('.', path) + ".lrc")
                    if (lrc.isFile) parse(lrc.readText(), "an .lrc file next to the track") else null
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
                        parse(it.readBytes().toString(Charsets.UTF_8), "an .lrc file next to the track")
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
            raw?.takeIf { it.isNotBlank() }?.let { parse(it, "the track's own tags") }
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
