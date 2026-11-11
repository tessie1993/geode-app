package dev.geode.data

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/**
 * How much of a picked document is read, by what it claims to be. Its size is whatever the user (or the
 * provider) hands over, so a read stops at the cap and fails instead of filling the heap or the disk.
 */
enum class ReadCap(
    val bytes: Long,
) {
    MILK(2L * MIB),
    TEXTURE(16L * MIB),
    PRESET(4L * MIB),
    LYRICS(1L * MIB),
    PLAYLIST(8L * MIB),
    CUBE(32L * MIB),
    ;

    /** Copies [input] into [out]; throws [IOException] once it runs past the cap. */
    fun copy(
        input: InputStream,
        out: OutputStream,
    ): Long {
        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
        var total = 0L
        while (true) {
            val n = input.read(buffer)
            if (n < 0) return total
            total += n
            if (total > bytes) throw IOException("larger than a ${name.lowercase()} file")
            out.write(buffer, 0, n)
        }
    }

    fun read(input: InputStream): ByteArray = ByteArrayOutputStream().also { copy(input, it) }.toByteArray()

    fun text(input: InputStream): String = read(input).toString(Charsets.UTF_8)
}

private const val MIB = 1024L * 1024L
