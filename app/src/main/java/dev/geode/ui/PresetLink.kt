package dev.geode.ui

import java.io.ByteArrayOutputStream
import java.util.zip.GZIPInputStream
import java.util.zip.GZIPOutputStream

object PresetLink {
    const val SCHEME = "geode"
    const val HOST = "preset"

    private const val PREFIX = "$SCHEME://$HOST/"

    const val MAX_LINK_LENGTH = 8_000

    private const val MAX_INFLATED_BYTES = 4 * 1024 * 1024

    private const val MAX_PAYLOAD_LENGTH = 1024 * 1024

    fun isPresetLink(text: String): Boolean = text.trim().startsWith(PREFIX, ignoreCase = true)

    fun encode(json: String): String {
        val deflated = ByteArrayOutputStream()
        GZIPOutputStream(deflated).use { it.write(json.toByteArray(Charsets.UTF_8)) }
        val payload =
            java.util.Base64
                .getUrlEncoder()
                .withoutPadding()
                .encodeToString(deflated.toByteArray())
        return PREFIX + payload
    }

    fun decode(text: String): String? {
        val trimmed = text.trim()
        if (!isPresetLink(trimmed)) return null
        // The producer refuses to mint a link longer than this and shares a file
        // instead, so anything longer arrived from somewhere else. Enforcing it on
        // the way IN too keeps a hostile link from spending the inflate budget
        // below on the main thread before the first frame is drawn.
        if (trimmed.length > MAX_LINK_LENGTH) return null
        val payload = trimmed.substring(PREFIX.length).substringBefore('#').substringBefore('?')
        if (payload.length > MAX_PAYLOAD_LENGTH) return null
        return runCatching {
            val bytes =
                java.util.Base64
                    .getUrlDecoder()
                    .decode(payload)
            GZIPInputStream(bytes.inputStream()).use { inflateBounded(it) }.toString(Charsets.UTF_8)
        }.getOrNull()?.takeIf { it.isNotBlank() }
    }

    private fun inflateBounded(gzip: GZIPInputStream): ByteArray {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        while (true) {
            val n = gzip.read(buffer)
            if (n < 0) return out.toByteArray()
            out.write(buffer, 0, n)
            if (out.size() > MAX_INFLATED_BYTES) error("link inflates past $MAX_INFLATED_BYTES bytes")
        }
    }

    fun findIn(text: String): String? {
        val at = text.indexOf(PREFIX, ignoreCase = true)
        if (at < 0) return null
        val end = text.indexOfFirst(at) { it.isWhitespace() }
        return text.substring(at, end)
    }

    private inline fun String.indexOfFirst(
        from: Int,
        predicate: (Char) -> Boolean,
    ): Int {
        for (i in from until length) if (predicate(this[i])) return i
        return length
    }
}
