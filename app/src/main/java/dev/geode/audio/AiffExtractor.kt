package dev.geode.audio

import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.MimeTypes
import androidx.media3.common.ParserException
import androidx.media3.common.util.ParsableByteArray
import androidx.media3.common.util.UnstableApi
import androidx.media3.extractor.Extractor
import androidx.media3.extractor.ExtractorInput
import androidx.media3.extractor.ExtractorOutput
import androidx.media3.extractor.PositionHolder
import androidx.media3.extractor.SeekMap
import androidx.media3.extractor.SeekPoint
import androidx.media3.extractor.TrackOutput

@UnstableApi
class AiffExtractor : Extractor {
    private lateinit var output: ExtractorOutput
    private lateinit var track: TrackOutput

    private var channels = 0
    private var sampleRate = 0
    private var bitsPerSample = 0
    private var pcmEncoding = C.ENCODING_INVALID
    private var dataStart = 0L
    private var dataEnd = 0L
    private var bytesPerFrame = 0
    private var pendingBytes = 0
    private var formatEmitted = false

    override fun sniff(input: ExtractorInput): Boolean {
        val header = ParsableByteArray(12)
        input.peekFully(header.data, 0, 12)
        if (header.readInt() != FORM) return false
        header.skipBytes(4)
        val type = header.readInt()
        return type == AIFF || type == AIFC
    }

    override fun init(output: ExtractorOutput) {
        this.output = output
        track = output.track(0, C.TRACK_TYPE_AUDIO)
        output.endTracks()
    }

    override fun read(
        input: ExtractorInput,
        seekPosition: PositionHolder,
    ): Int {
        if (!formatEmitted) {
            parseHeaders(input)
            emitFormat()
            formatEmitted = true
        }
        if (input.position < dataStart) {
            input.skipFully((dataStart - input.position).toInt())
        }
        if (input.position >= dataEnd) return Extractor.RESULT_END_OF_INPUT

        val maxRead = minOf(MAX_SAMPLE_BYTES.toLong(), dataEnd - input.position).toInt()
        val appended = track.sampleData(input, maxRead, true)
        if (appended == C.RESULT_END_OF_INPUT) {
            flushPending(input.position)
            return Extractor.RESULT_END_OF_INPUT
        }
        pendingBytes += appended
        val emitBytes = (pendingBytes / bytesPerFrame) * bytesPerFrame
        if (emitBytes > 0) {
            val endPosition = input.position
            val startFrame = (endPosition - dataStart - pendingBytes) / bytesPerFrame
            val timeUs = startFrame * C.MICROS_PER_SECOND / sampleRate
            track.sampleMetadata(timeUs, C.BUFFER_FLAG_KEY_FRAME, emitBytes, pendingBytes - emitBytes, null)
            pendingBytes -= emitBytes
        }
        return Extractor.RESULT_CONTINUE
    }

    private fun flushPending(position: Long) {
        if (pendingBytes >= bytesPerFrame) {
            val emit = (pendingBytes / bytesPerFrame) * bytesPerFrame
            val startFrame = (position - dataStart - pendingBytes) / bytesPerFrame
            track.sampleMetadata(startFrame * C.MICROS_PER_SECOND / sampleRate, C.BUFFER_FLAG_KEY_FRAME, emit, pendingBytes - emit, null)
        }
        pendingBytes = 0
    }

    private fun parseHeaders(input: ExtractorInput) {
        val scratch = ParsableByteArray(18)
        input.readFully(scratch.data, 0, 12)
        scratch.setPosition(8)
        val isAifc = scratch.readInt() == AIFC
        var commSeen = false
        while (true) {
            scratch.reset(8)
            input.readFully(scratch.data, 0, 8)
            scratch.setPosition(0)
            val id = scratch.readInt()
            val size = scratch.readInt().toLong() and 0xFFFFFFFFL
            when (id) {
                COMM -> {
                    if (size !in 18L..MAX_COMM_BYTES) {
                        throw ParserException.createForMalformedContainer(
                            "AIFF: implausible COMM chunk size ($size)",
                            null,
                        )
                    }
                    val comm = ByteArray(size.toInt())
                    input.readFully(comm, 0, comm.size)
                    val parsed = parseComm(comm, isAifc)
                    if (!parsed.isPlausible()) {
                        throw ParserException.createForMalformedContainer(
                            "AIFF: invalid COMM fields (channels=${parsed.channels}, " +
                                "bits=${parsed.bitsPerSample}, rate=${parsed.sampleRate})",
                            null,
                        )
                    }
                    channels = parsed.channels
                    sampleRate = parsed.sampleRate
                    bitsPerSample = parsed.bitsPerSample
                    pcmEncoding = parsed.pcmEncoding
                    commSeen = true
                }
                SSND -> {
                    if (!commSeen) {
                        throw ParserException.createForMalformedContainer("AIFF: SSND before COMM", null)
                    }
                    scratch.reset(8)
                    input.readFully(scratch.data, 0, 8)
                    scratch.setPosition(0)
                    val offset = scratch.readInt().toLong() and 0xFFFFFFFFL
                    scratch.skipBytes(4)
                    dataStart = input.position + offset
                    dataEnd = input.position + (size - 8)
                    bytesPerFrame = channels * (bitsPerSample / 8)
                    return
                }
                else -> {
                    val padded = size + (size and 1L)
                    if (padded > Int.MAX_VALUE) {
                        throw ParserException.createForMalformedContainer(
                            "AIFF: implausible chunk size ($size)",
                            null,
                        )
                    }
                    input.skipFully(padded.toInt())
                }
            }
            if (id == COMM && (size and 1L) == 1L) input.skipFully(1)
        }
    }

    private fun emitFormat() {
        if (pcmEncoding == C.ENCODING_INVALID) {
            throw ParserException.createForMalformedContainer(
                "AIFF: unsupported sample format ($bitsPerSample-bit)",
                null,
            )
        }
        track.format(
            Format
                .Builder()
                .setSampleMimeType(MimeTypes.AUDIO_RAW)
                .setChannelCount(channels)
                .setSampleRate(sampleRate)
                .setPcmEncoding(pcmEncoding)
                .setMaxInputSize(MAX_SAMPLE_BYTES)
                .build(),
        )
        val durationUs = (dataEnd - dataStart) / bytesPerFrame * C.MICROS_PER_SECOND / sampleRate
        output.seekMap(
            object : SeekMap {
                override fun isSeekable() = true

                override fun getDurationUs() = durationUs

                override fun getSeekPoints(timeUs: Long): SeekMap.SeekPoints {
                    val frame = (timeUs * sampleRate / C.MICROS_PER_SECOND)
                    val position = (dataStart + frame * bytesPerFrame).coerceAtMost(dataEnd)
                    val clampedUs = (position - dataStart) / bytesPerFrame * C.MICROS_PER_SECOND / sampleRate
                    return SeekMap.SeekPoints(SeekPoint(clampedUs, position))
                }
            },
        )
    }

    override fun seek(
        position: Long,
        timeUs: Long,
    ) {
        pendingBytes = 0
    }

    override fun release() {}

    internal data class CommInfo(
        val channels: Int,
        val sampleRate: Int,
        val bitsPerSample: Int,
        val pcmEncoding: @C.PcmEncoding Int,
    ) {
        fun isPlausible(): Boolean = channels > 0 && bitsPerSample > 0 && sampleRate > 0
    }

    companion object {
        private const val FORM = 0x464F524D
        private const val AIFF = 0x41494646
        private const val AIFC = 0x41494643
        private const val COMM = 0x434F4D4D
        private const val SSND = 0x53534E44
        private const val SOWT = 0x736F7774
        private const val NONE = 0x4E4F4E45
        private const val MAX_SAMPLE_BYTES = 32 * 1024

        internal const val MAX_COMM_BYTES = 256L

        internal fun parseComm(
            body: ByteArray,
            isAifc: Boolean,
        ): CommInfo {
            val p = ParsableByteArray(body)
            val channels = p.readShort().toInt()
            p.skipBytes(4)
            val bits = p.readShort().toInt()
            val rate = parseExtendedFloat80(body, 8)
            val compression =
                if (isAifc && body.size >= 22) {
                    java.nio.ByteBuffer
                        .wrap(body, 18, 4)
                        .int
                } else {
                    NONE
                }
            val littleEndian = compression == SOWT
            val encoding =
                when {
                    compression != NONE && compression != SOWT -> C.ENCODING_INVALID
                    bits == 16 && littleEndian -> C.ENCODING_PCM_16BIT
                    bits == 16 -> C.ENCODING_PCM_16BIT_BIG_ENDIAN
                    bits == 24 && !littleEndian -> C.ENCODING_PCM_24BIT_BIG_ENDIAN
                    bits == 32 && !littleEndian -> C.ENCODING_PCM_32BIT_BIG_ENDIAN
                    else -> C.ENCODING_INVALID
                }
            return CommInfo(channels, rate, bits, encoding)
        }

        internal fun parseExtendedFloat80(
            bytes: ByteArray,
            offset: Int,
        ): Int {
            val exponent = (((bytes[offset].toInt() and 0x7F) shl 8) or (bytes[offset + 1].toInt() and 0xFF)) - 16383
            var mantissa = 0L
            for (i in 0 until 8) {
                mantissa = (mantissa shl 8) or (bytes[offset + 2 + i].toLong() and 0xFF)
            }
            if (mantissa == 0L) return 0
            val m = mantissa.toULong().toDouble()
            val shift = exponent - 63
            val value =
                if (shift >= 0) {
                    m * Math.pow(2.0, shift.toDouble())
                } else {
                    m / Math.pow(2.0, (-shift).toDouble())
                }
            return Math.round(value).toInt()
        }
    }
}
