package dev.geode.cast

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket

/** [RequestLineReader] and [PerAddressLimit] against real loopback sockets. */
class RequestLineTest {
    private lateinit var server: ServerSocket
    private lateinit var client: Socket
    private lateinit var accepted: Socket

    @Before
    fun connect() {
        server = ServerSocket(0, 1, InetAddress.getLoopbackAddress())
        client = Socket(InetAddress.getLoopbackAddress(), server.localPort)
        accepted = server.accept()
    }

    @After
    fun close() {
        client.close()
        accepted.close()
        server.close()
    }

    private fun deadlineIn(ms: Long) = System.nanoTime() + ms * 1_000_000L

    @Test
    fun normalRequestLineIsRead() {
        client.getOutputStream().apply {
            write("GET /abc/live.m3u8 HTTP/1.1\r\nHost: x\r\n\r\n".toByteArray())
            flush()
        }
        val r = RequestLineReader.read(accepted, deadlineIn(5_000))
        assertEquals(RequestLine.Line("GET /abc/live.m3u8 HTTP/1.1"), r)
    }

    @Test
    fun headersAreReadToTheBlankLine() {
        client.getOutputStream().apply {
            write("GET /a/seg3.m4s HTTP/1.1\r\nHost: x\r\nOrigin: https://r\r\n\r\nNEXT".toByteArray())
            flush()
        }
        assertEquals(RequestLine.Line("GET /a/seg3.m4s HTTP/1.1"), RequestLineReader.read(accepted, deadlineIn(5_000)))
        assertEquals('N'.code, accepted.getInputStream().read())
    }

    @Test
    fun headersOverTheirCapAreRefused() {
        client.getOutputStream().apply {
            write("GET / HTTP/1.1\r\n".toByteArray())
            repeat(RequestLineReader.MAX_HEADER_BYTES / 64 + 2) { write("X-Pad: ${"p".repeat(56)}\r\n".toByteArray()) }
            flush()
        }
        assertEquals(RequestLine.HeadersTooLarge, RequestLineReader.read(accepted, deadlineIn(5_000)))
    }

    @Test
    fun lineOverTheCapIsRefused() {
        client.getOutputStream().apply {
            write(ByteArray(RequestLineReader.MAX_LINE_BYTES + 10) { 'a'.code.toByte() })
            flush()
        }
        assertEquals(RequestLine.TooLong, RequestLineReader.read(accepted, deadlineIn(5_000)))
    }

    @Test
    fun lineExactlyAtTheCapIsKept() {
        val line = "G".repeat(RequestLineReader.MAX_LINE_BYTES)
        client.getOutputStream().apply {
            write("$line\n\n".toByteArray())
            flush()
        }
        assertEquals(RequestLine.Line(line), RequestLineReader.read(accepted, deadlineIn(5_000)))
    }

    @Test
    fun slowClientIsCutOffAtTheDeadline() {
        val writer =
            Thread {
                runCatching {
                    val out = client.getOutputStream()
                    while (true) {
                        out.write('a'.code)
                        out.flush()
                        Thread.sleep(50)
                    }
                }
            }.apply {
                isDaemon = true
                start()
            }
        val start = System.nanoTime()
        val r = RequestLineReader.read(accepted, deadlineIn(400))
        val tookMs = (System.nanoTime() - start) / 1_000_000L
        writer.interrupt()
        assertEquals(RequestLine.Dropped, r)
        // Each byte arrived well inside the per-read timeout; only the overall deadline stopped it.
        assertTrue("took $tookMs ms", tookMs in 300..3_000)
    }

    @Test
    fun silentClientIsDroppedAtTheDeadline() {
        assertEquals(RequestLine.Dropped, RequestLineReader.read(accepted, deadlineIn(200)))
    }

    @Test
    fun closedClientIsDropped() {
        client.close()
        assertEquals(RequestLine.Dropped, RequestLineReader.read(accepted, deadlineIn(5_000)))
    }

    @Test
    fun oneAddressCannotTakeMoreThanItsShare() {
        val limit = PerAddressLimit(2)
        assertTrue(limit.tryAcquire("10.0.0.5"))
        assertTrue(limit.tryAcquire("10.0.0.5"))
        assertFalse(limit.tryAcquire("10.0.0.5"))
        // A different device, such as the TV, is still served.
        assertTrue(limit.tryAcquire("10.0.0.9"))
        limit.release("10.0.0.5")
        assertTrue(limit.tryAcquire("10.0.0.5"))
    }
}
