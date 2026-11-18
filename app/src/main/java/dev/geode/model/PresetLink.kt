package dev.geode.model

import dev.geode.engine.bridge.LooksNative
import java.io.ByteArrayOutputStream
import java.util.zip.GZIPInputStream
import java.util.zip.GZIPOutputStream

/** A preset whole in one link. What is framed and what is accepted is native (core/viz/LookFiles); here only the packing. */
object PresetLink {
    val MAX_LINK_LENGTH: Int get() = LooksNative.presetLinkMaxLength()

    fun isPresetLink(text: String): Boolean = LooksNative.presetLinkIs(text)

    fun encode(json: String): String = LooksNative.presetLinkFrame(LinkPacking.pack(json))

    /** [encode], or null when the link is too long to share and a file should go instead. */
    fun shareable(json: String): String? = encode(json).takeIf(LooksNative::presetLinkFits)

    fun decode(text: String): String? {
        val payload = LooksNative.presetLinkPayload(text) ?: return null
        return LinkPacking.unpack(payload, LooksNative.presetLinkMaxInflated())?.takeIf(LooksNative::presetLinkBodyAccepted)
    }

    fun findIn(text: String): String? = LooksNative.presetLinkFind(text)
}

/** The bytes of a share link's payload: gzip, then URL-safe base64 without padding, and back. */
internal object LinkPacking {
    fun pack(text: String): String {
        val deflated = ByteArrayOutputStream()
        GZIPOutputStream(deflated).use { it.write(text.toByteArray(Charsets.UTF_8)) }
        return java.util.Base64
            .getUrlEncoder()
            .withoutPadding()
            .encodeToString(deflated.toByteArray())
    }

    /** The text [payload] packs, or null when it is not one or unpacks past [maxBytes]. */
    fun unpack(
        payload: String,
        maxBytes: Int,
    ): String? =
        runCatching {
            val bytes =
                java.util.Base64
                    .getUrlDecoder()
                    .decode(payload)
            GZIPInputStream(bytes.inputStream()).use { inflateBounded(it, maxBytes) }.toString(Charsets.UTF_8)
        }.getOrNull()

    private fun inflateBounded(
        gzip: GZIPInputStream,
        maxBytes: Int,
    ): ByteArray {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        while (true) {
            val n = gzip.read(buffer)
            if (n < 0) return out.toByteArray()
            out.write(buffer, 0, n)
            if (out.size() > maxBytes) error("payload unpacks past $maxBytes bytes")
        }
    }
}
