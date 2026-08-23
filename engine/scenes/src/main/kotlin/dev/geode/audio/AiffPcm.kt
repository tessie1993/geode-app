package dev.geode.audio

import android.content.Context
import android.net.Uri
import dev.geode.util.bestEffort
import java.io.DataInputStream
import java.io.EOFException
import java.io.InputStream

class AiffPcm private constructor(
    private val input: DataInputStream,
    val channels: Int,
    val sampleRate: Int,
    private val bitsPerSample: Int,
    val totalFrames: Long,
    private val littleEndian: Boolean,
) {
    private var framesRead = 0L

    val durationUs: Long get() = if (sampleRate > 0) totalFrames * 1_000_000L / sampleRate else 0L

    val progress: Float get() = if (totalFrames > 0) (framesRead / totalFrames.toFloat()).coerceIn(0f, 1f) else 0f

    fun read(out: ShortArray): Int {
        val bytesPer = bitsPerSample / 8
        var maxSamples = out.size - (out.size % channels)
        if (totalFrames > 0) {
            val remaining = (totalFrames - framesRead) * channels
            if (remaining <= 0) return -1
            if (remaining < maxSamples) maxSamples = remaining.toInt()
        }
        val raw = ByteArray(maxSamples * bytesPer)
        var got = 0
        try {
            while (got < raw.size) {
                val n = input.read(raw, got, raw.size - got)
                if (n < 0) break
                got += n
            }
        } catch (_: EOFException) {
        }
        val samples = got / bytesPer
        if (samples == 0) return -1
        for (i in 0 until samples) {
            val o = i * bytesPer
            val hi = if (littleEndian) o + bytesPer - 1 else o
            val lo = if (littleEndian) o + bytesPer - 2 else o + 1
            out[i] =
                when (bitsPerSample) {
                    8 -> ((raw[o].toInt()) shl 8).toShort()
                    16, 24, 32 -> (((raw[hi].toInt() and 0xFF) shl 8) or (raw[lo].toInt() and 0xFF)).toShort()
                    else -> 0
                }
        }
        framesRead += samples / channels
        return samples
    }

    fun close() {
        bestEffort(TAG, "input.close()") { input.close() }
    }

    companion object {
        fun open(
            context: Context,
            uri: Uri,
        ): AiffPcm? {
            val stream = context.contentResolver.openInputStream(uri) ?: return null
            return runCatching { parse(stream) }.getOrNull().also { if (it == null) runCatching { stream.close() } }
        }

        internal fun parse(rawStream: InputStream): AiffPcm? {
            val din = DataInputStream(rawStream.buffered(1 shl 16))
            val form = ByteArray(4).also { din.readFully(it) }
            if (String(form) != "FORM") return null
            din.readInt()
            val kind = ByteArray(4).also { din.readFully(it) }
            val isAiff = String(kind) == "AIFF" || String(kind) == "AIFC"
            if (!isAiff) return null

            var channels = 0
            var bits = 0
            var rate = 0
            var frames = 0L
            var littleEndian = false
            while (true) {
                val id = ByteArray(4)
                try {
                    din.readFully(id)
                } catch (_: EOFException) {
                    return null
                }
                val size = din.readInt()
                when (String(id)) {
                    "COMM" -> {
                        channels = din.readShort().toInt()
                        frames = din.readInt().toLong() and 0xFFFFFFFFL
                        bits = din.readShort().toInt()
                        rate = readExtended80(din)
                        var consumed = 18
                        if (size > consumed) {
                            val comp = ByteArray(4).also { din.readFully(it) }
                            consumed += 4
                            when (String(comp)) {
                                "NONE" -> Unit
                                "sowt" -> littleEndian = true
                                else -> return null
                            }
                            din.skipBytes(size - consumed + (size and 1))
                        } else if (size and 1 == 1) {
                            din.skipBytes(1)
                        }
                    }
                    "SSND" -> {
                        val offset = din.readInt()
                        din.readInt()
                        if (offset > 0) din.skipBytes(offset)
                        if (channels <= 0 || rate <= 0 || bits !in intArrayOf(8, 16, 24, 32)) return null
                        return AiffPcm(din, channels, rate, bits, frames, littleEndian)
                    }
                    else -> din.skipBytes(size + (size and 1))
                }
            }
        }

        private fun readExtended80(din: DataInputStream): Int {
            val exponent = din.readShort().toInt() and 0x7FFF
            val mantissaHigh = din.readInt().toLong() and 0xFFFFFFFFL
            din.readInt()
            if (exponent == 0 && mantissaHigh == 0L) return 0
            val shift = 63 - (exponent - 16383)
            if (shift < 0 || shift > 63) return 0
            return ((mantissaHigh shl 32) ushr shift).toInt()
        }
    }
}

private const val TAG = "AiffPcm"
