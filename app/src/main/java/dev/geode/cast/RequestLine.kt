package dev.geode.cast

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.ConcurrentHashMap

/** What reading a request line from a client came to. */
internal sealed interface RequestLine {
    /** A full line, without its line break. */
    data class Line(
        val text: String,
    ) : RequestLine

    /** The line passed its size cap before a line break. */
    data object TooLong : RequestLine

    /** The client closed, went quiet, or ran past the connection's deadline. */
    data object Dropped : RequestLine
}

/**
 * Reads the request line of [LiveHttpServer] under two bounds a plain `readLine()` lacks: a size
 * cap, so a line that never ends can't grow memory, and one deadline for the whole connection.
 * The socket timeout alone is per read, so a client sending a byte a second would hold a pool
 * thread for as long as it liked; here every read's timeout is cut to what is left of the
 * deadline.
 */
internal object RequestLineReader {
    /** Longest request line taken, in bytes; the receiver's longest path is under 100. */
    const val MAX_LINE_BYTES = 8 * 1024

    private const val NANOS_PER_MS = 1_000_000L

    fun read(
        socket: Socket,
        deadlineNanos: Long,
        maxBytes: Int = MAX_LINE_BYTES,
    ): RequestLine {
        val input = socket.getInputStream()
        val bytes = ByteArrayOutputStream()
        try {
            while (true) {
                val leftMs = (deadlineNanos - System.nanoTime()) / NANOS_PER_MS
                if (leftMs <= 0) return RequestLine.Dropped
                socket.soTimeout = leftMs.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                val b = input.read()
                if (b < 0) return RequestLine.Dropped
                if (b == '\n'.code) {
                    return RequestLine.Line(bytes.toString(Charsets.US_ASCII.name()).trimEnd('\r'))
                }
                if (bytes.size() >= maxBytes) return RequestLine.TooLong
                bytes.write(b)
            }
        } catch (_: SocketTimeoutException) {
            return RequestLine.Dropped
        } catch (_: IOException) {
            return RequestLine.Dropped
        }
    }
}

/**
 * Caps the connections one remote address may hold open, so a single device on the Wi-Fi can't
 * take the whole pool and starve the TV. [tryAcquire] before serving, [release] after.
 */
internal class PerAddressLimit(
    private val max: Int,
) {
    private val open = ConcurrentHashMap<String, Int>()

    fun tryAcquire(address: String): Boolean {
        var granted = false
        open.compute(address) { _, n ->
            val now = n ?: 0
            granted = now < max
            if (granted) now + 1 else now
        }
        return granted
    }

    fun release(address: String) {
        open.compute(address) { _, n -> if (n == null || n <= 1) null else n - 1 }
    }
}
