package dev.geode.store

import android.content.Context
import android.net.Uri
import dev.geode.thumbs.SyntheticGroove
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The music the store captures play: the thumbnail renderer's [SyntheticGroove] (a kick, hats, a
 * bass line and a pad, generated in code) written as a 16-bit stereo WAV into the app's cache.
 *
 * The app ships no demo track and the emulator has no library, and a store image must not show a
 * real artist's song the owner has no rights to; a sine tone, which the journeys play, gives a
 * visual nothing to react to. The length is a whole number of the groove's four-bar progression,
 * so it repeats without a seam.
 */
internal object DemoGroove {
    const val TITLE = "Demo groove"
    private const val SECONDS = 32
    private const val HEADER_BYTES = 44
    private const val FMT_CHUNK_BYTES = 16
    private const val RIFF_EXTRA_BYTES = 36
    private const val BYTES_PER_SAMPLE = 2
    private const val BITS_PER_SAMPLE = 16
    private const val PCM_FORMAT: Short = 1
    private const val FULL_SCALE = 32_767f

    private fun ascii(tag: String) = tag.toByteArray(Charsets.US_ASCII)

    fun write(context: Context): Uri {
        val groove = SyntheticGroove()
        val channels = SyntheticGroove.CHANNELS
        val rate = groove.sampleRateHz
        val samples = groove.next(rate * SECONDS)
        val data = samples.size * BYTES_PER_SAMPLE
        val blockAlign = channels * BYTES_PER_SAMPLE
        val buf = ByteBuffer.allocate(HEADER_BYTES + data).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(ascii("RIFF"))
        buf.putInt(RIFF_EXTRA_BYTES + data)
        buf.put(ascii("WAVE"))
        buf.put(ascii("fmt "))
        buf.putInt(FMT_CHUNK_BYTES)
        buf.putShort(PCM_FORMAT)
        buf.putShort(channels.toShort())
        buf.putInt(rate)
        buf.putInt(rate * blockAlign)
        buf.putShort(blockAlign.toShort())
        buf.putShort(BITS_PER_SAMPLE.toShort())
        buf.put(ascii("data"))
        buf.putInt(data)
        for (s in samples) buf.putShort((s * FULL_SCALE).toInt().toShort())
        val file = File(context.cacheDir, "store-demo-groove.wav")
        file.writeBytes(buf.array())
        return Uri.fromFile(file)
    }
}
