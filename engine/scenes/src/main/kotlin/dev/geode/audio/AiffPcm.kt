package dev.geode.audio

import android.content.Context
import android.net.Uri
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.io.InputStream

/**
 * An AIFF file as interleaved 16-bit samples, for the export (MediaExtractor cannot open AIFF). Only the
 * Android half is here: the content resolver's bytes, handed to the native reader
 * (`core/audio/ingest/Aiff`, #653), which walks the header, reads COMM's 80-bit rate and converts every
 * depth and byte order to s16 (`core/tests/aiff_test.cpp`). One thread, the export's; [close] frees both.
 */
class AiffPcm private constructor(
    private val input: InputStream,
    private var handle: Long,
    val channels: Int,
    val sampleRate: Int,
) {
    private val raw = ByteArray(CHUNK_BYTES)

    val progress: Float get() = GeodeNative.aiffProgress(handle)

    /** Up to [out]`.size` samples, whole frames; -1 at the end. */
    fun read(out: ShortArray): Int {
        while (true) {
            val n = GeodeNative.aiffRead(handle, out, out.size)
            if (n != 0) return n
            val got = input.read(raw)
            if (got <= 0) return -1
            GeodeNative.aiffWrite(handle, raw, got)
        }
    }

    fun close() {
        bestEffort(TAG, "input.close()") { input.close() }
        val h = handle
        handle = 0L
        if (h != 0L) GeodeNative.aiffDestroy(h)
    }

    companion object {
        fun open(
            context: Context,
            uri: Uri,
        ): AiffPcm? {
            val stream = context.contentResolver.openInputStream(uri) ?: return null
            return runCatching { parse(stream) }.getOrNull().also { if (it == null) runCatching { stream.close() } }
        }

        private fun parse(input: InputStream): AiffPcm? {
            val handle = GeodeNative.aiffCreate()
            val raw = ByteArray(CHUNK_BYTES)
            var state = 0
            try {
                while (state == 0) {
                    val got = input.read(raw)
                    if (got <= 0) break
                    state = GeodeNative.aiffWrite(handle, raw, got)
                }
                val format = IntArray(2)
                if (state == 1 && GeodeNative.aiffFormat(handle, format)) {
                    return AiffPcm(input, handle, format[0], format[1])
                }
            } catch (t: Throwable) {
                GeodeNative.aiffDestroy(handle)
                throw t
            }
            GeodeNative.aiffDestroy(handle)
            return null
        }
    }
}

private const val TAG = "AiffPcm"
private const val CHUNK_BYTES = 1 shl 16
