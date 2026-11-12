package dev.geode.audio

import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.MimeTypes
import androidx.media3.common.ParserException
import androidx.media3.common.util.UnstableApi
import androidx.media3.extractor.Extractor
import androidx.media3.extractor.ExtractorInput
import androidx.media3.extractor.ExtractorOutput
import androidx.media3.extractor.PositionHolder
import androidx.media3.extractor.SeekMap
import androidx.media3.extractor.SeekPoint
import androidx.media3.extractor.TrackOutput
import dev.geode.engine.bridge.GeodeNative

/**
 * Plays AIFF and AIFF-C (NONE, sowt) through Media3, which has no extractor for them. This class only moves
 * bytes: the header is read by the native scan the export's AIFF reader uses (core/audio/ingest/AiffReader),
 * fed here from Media3's input, and the seek map's times and offsets are native's too, so the player and an
 * export agree on what a file says.
 */
@UnstableApi
class AiffExtractor : Extractor {
    private lateinit var output: ExtractorOutput
    private lateinit var track: TrackOutput

    private var channels = 0
    private var sampleRate = 0
    private var pcmEncoding = C.ENCODING_INVALID
    private var dataStart = 0L
    private var dataBytes = 0L
    private var bytesPerFrame = 0
    private var pendingBytes = 0
    private var formatEmitted = false

    override fun sniff(input: ExtractorInput): Boolean {
        val scan = GeodeNative.aiffScanCreate()
        try {
            val head = ByteArray(GeodeNative.AIFF_SCAN_MAX_READ)
            val want = GeodeNative.aiffScanRead(scan)
            input.peekFully(head, 0, want)
            return GeodeNative.aiffScanStep(scan, head, want) == GeodeNative.AIFF_SCAN_MORE
        } finally {
            GeodeNative.aiffScanDestroy(scan)
        }
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
        val gap = GeodeNative.aiffGapBytes(input.position, dataStart)
        if (gap > 0) input.skipFully(gap.toInt())
        val maxRead = GeodeNative.aiffReadBytes(input.position, dataStart, dataBytes)
        if (maxRead <= 0) return Extractor.RESULT_END_OF_INPUT
        val appended = track.sampleData(input, maxRead, true)
        if (appended == C.RESULT_END_OF_INPUT) {
            flushPending(input.position)
            return Extractor.RESULT_END_OF_INPUT
        }
        pendingBytes += appended
        flushPending(input.position)
        return Extractor.RESULT_CONTINUE
    }

    /** Hands on the whole frames held so far, timed by where their first byte lies; a part frame waits. */
    private fun flushPending(position: Long) {
        val emit = GeodeNative.aiffWholeBytes(pendingBytes, bytesPerFrame)
        if (emit <= 0) return
        val timeUs = GeodeNative.aiffPendingTimeUs(position, dataStart, pendingBytes, bytesPerFrame, sampleRate)
        track.sampleMetadata(timeUs, C.BUFFER_FLAG_KEY_FRAME, emit, pendingBytes - emit, null)
        pendingBytes -= emit
    }

    /** Feeds the native scan from [input] up to the first sample byte and keeps what the header said. */
    private fun parseHeaders(input: ExtractorInput) {
        val fields = LongArray(GeodeNative.AIFF_HEADER_COUNT)
        val scan = GeodeNative.aiffScanCreate()
        try {
            val bytes = ByteArray(GeodeNative.AIFF_SCAN_MAX_READ)
            var status = GeodeNative.AIFF_SCAN_MORE
            while (status == GeodeNative.AIFF_SCAN_MORE) {
                val gap = GeodeNative.aiffScanSkip(scan).toInt()
                if (gap > 0) input.skipFully(gap)
                val want = GeodeNative.aiffScanRead(scan)
                if (want > 0) input.readFully(bytes, 0, want)
                status = GeodeNative.aiffScanStep(scan, bytes, want)
            }
            val read = status == GeodeNative.AIFF_SCAN_DONE && GeodeNative.aiffScanHeader(scan, fields)
            pcmEncoding = if (read) encodingOf(fields[GeodeNative.AIFF_HEADER_PCM].toInt()) else C.ENCODING_INVALID
        } finally {
            GeodeNative.aiffScanDestroy(scan)
        }
        if (pcmEncoding == C.ENCODING_INVALID) {
            throw ParserException.createForMalformedContainer("AIFF: not a header this plays", null)
        }
        channels = fields[GeodeNative.AIFF_HEADER_CHANNELS].toInt()
        sampleRate = fields[GeodeNative.AIFF_HEADER_SAMPLE_RATE].toInt()
        bytesPerFrame = fields[GeodeNative.AIFF_HEADER_BYTES_PER_FRAME].toInt()
        // The scan leaves the input on the first sample byte.
        dataStart = input.position
        dataBytes = fields[GeodeNative.AIFF_HEADER_DATA_BYTES]
    }

    private fun emitFormat() {
        track.format(
            Format
                .Builder()
                .setSampleMimeType(MimeTypes.AUDIO_RAW)
                .setChannelCount(channels)
                .setSampleRate(sampleRate)
                .setPcmEncoding(pcmEncoding)
                .setMaxInputSize(GeodeNative.AIFF_MAX_SAMPLE_BYTES)
                .build(),
        )
        val start = dataStart
        val samples = dataBytes
        val frameBytes = bytesPerFrame
        val rate = sampleRate
        val durationUs = GeodeNative.aiffTimeUs(samples, frameBytes, rate)
        output.seekMap(
            object : SeekMap {
                override fun isSeekable() = true

                override fun getDurationUs() = durationUs

                override fun getSeekPoints(timeUs: Long): SeekMap.SeekPoints {
                    val offset = GeodeNative.aiffSeekOffset(timeUs, frameBytes, rate, samples)
                    val clampedUs = GeodeNative.aiffTimeUs(offset, frameBytes, rate)
                    return SeekMap.SeekPoints(SeekPoint(clampedUs, start + offset))
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

    private companion object {
        /** Media3's name for each sample layout native reports; invalid for one the sink cannot take. */
        fun encodingOf(pcm: Int): Int =
            when (pcm) {
                GeodeNative.AIFF_PCM_S16_LE -> C.ENCODING_PCM_16BIT
                GeodeNative.AIFF_PCM_S16_BE -> C.ENCODING_PCM_16BIT_BIG_ENDIAN
                GeodeNative.AIFF_PCM_S24_BE -> C.ENCODING_PCM_24BIT_BIG_ENDIAN
                GeodeNative.AIFF_PCM_S32_BE -> C.ENCODING_PCM_32BIT_BIG_ENDIAN
                else -> C.ENCODING_INVALID
            }
    }
}
