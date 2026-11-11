package dev.geode.data

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream

class ReadCapTest {
    /** [size] bytes of 'a', handed out without ever being held whole. */
    private fun stream(size: Long): InputStream =
        object : InputStream() {
            var left = size

            override fun read(): Int = if (left-- > 0) 'a'.code else -1
        }

    @Test
    fun readsUpToTheCap() {
        val bytes = ReadCap.LYRICS.read(stream(ReadCap.LYRICS.bytes))
        assertEquals(ReadCap.LYRICS.bytes, bytes.size.toLong())
    }

    @Test(expected = IOException::class)
    fun failsOneBytePastTheCap() {
        ReadCap.LYRICS.read(stream(ReadCap.LYRICS.bytes + 1))
    }

    @Test(expected = IOException::class)
    fun stopsAnEndlessStream() {
        ReadCap.MILK.copy(stream(Long.MAX_VALUE), ByteArrayOutputStream())
    }

    @Test
    fun textIsUtf8() {
        val text = "Ünïcode ♪"
        assertEquals(text, ReadCap.PRESET.text(text.toByteArray(Charsets.UTF_8).inputStream()))
        assertArrayEquals(byteArrayOf(1, 2, 3), ReadCap.CUBE.read(byteArrayOf(1, 2, 3).inputStream()))
    }
}
