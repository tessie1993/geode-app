package dev.geode.cast

import dev.geode.RingLog
import dev.geode.util.bestEffort
import java.io.OutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.security.SecureRandom
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

/**
 * Serves [LiveSegments] to the TV over the local network: with the Cast SDK's own traffic, the
 * one thing the app's INTERNET permission is for (the owner's rule: Cast is the only internet use).
 *
 * The Cast receiver fetches the playlist and segments by plain HTTP GET from this phone's LAN
 * address. Nothing else is served, nothing is used from the request but its method and path, and
 * every path sits under a random [token], so another device on the same Wi-Fi can't find the stream
 * by guessing a port and a file name. CORS is open because the receiver's HLS player (Shaka, in the
 * Cast web receiver) fetches with XHR from its own https origin; a preflight OPTIONS is answered, and
 * a Range header is ignored for the whole body (200, `Accept-Ranges: none`), which HTTP allows.
 *
 * Bounds: the request line is capped at 8 KB (414 past it) and its headers at 16 KB (431), a
 * connection gets one 10 s deadline from accept however slowly it sends or reads (the socket is
 * closed when it passes, mid-write too), and one remote address holds at most two connections, so a
 * single device on the Wi-Fi can't take the pool. A response ends with a half-close and a short drain
 * of what the client still sends, so closing never resets a connection with the body in flight.
 * Accepted risks, written down: the stream is plain HTTP with the token in the path (the Default
 * Media Receiver can't do TLS to a LAN address), and CORS stays `*` because the receiver's origin is not one we can pin; the server
 * binds only to the phone's site-local Wi-Fi address, which limits who can reach it.
 *
 * A small fixed pool serves requests: the receiver asks for one segment a second and the playlist
 * as often, so a handful of threads is plenty, and a slow or stuck client can't grow it.
 */
internal class LiveHttpServer(
    private val stream: LiveSegments,
    private val bindAddress: InetAddress,
) {
    val token: String = randomToken()
    private val socket = ServerSocket()
    private val pool: ExecutorService = Executors.newFixedThreadPool(POOL_SIZE)
    private val deadlines =
        ScheduledThreadPoolExecutor(1) { r -> Thread(r, "cast-http-deadline").apply { isDaemon = true } }
            .apply { removeOnCancelPolicy = true }
    private val perAddress = PerAddressLimit(MAX_PER_ADDRESS)
    private var acceptThread: Thread? = null

    /** The port the OS gave the server; valid after [start]. */
    val port: Int get() = socket.localPort

    /** The playlist's URL, as the receiver loads it. */
    val playlistUrl: String
        get() = "http://${bindAddress.hostAddress}:$port/$token/${LiveSegments.PLAYLIST_NAME}"

    fun start() {
        socket.reuseAddress = true
        socket.bind(InetSocketAddress(bindAddress, 0))
        acceptThread =
            Thread({ acceptLoop() }, "cast-http").apply {
                isDaemon = true
                start()
            }
    }

    fun stop() {
        bestEffort(TAG, "socket.close()") { socket.close() }
        pool.shutdownNow()
        deadlines.shutdownNow()
        acceptThread = null
    }

    private fun acceptLoop() {
        while (!socket.isClosed) {
            val client =
                try {
                    socket.accept()
                } catch (_: SocketException) {
                    return // closed by stop()
                }
            val who = client.inetAddress?.hostAddress.orEmpty()
            if (!perAddress.tryAcquire(who)) {
                bestEffort(TAG, "client.close()") { client.close() }
                continue
            }
            val deadline = System.nanoTime() + CONNECTION_DEADLINE_MS * NANOS_PER_MS
            try {
                pool.execute {
                    try {
                        serve(client, deadline)
                    } finally {
                        perAddress.release(who)
                    }
                }
            } catch (_: RejectedExecutionException) {
                perAddress.release(who)
                bestEffort(TAG, "client.close()") { client.close() }
            }
        }
    }

    private fun serve(
        client: Socket,
        deadlineNanos: Long,
    ) {
        val closeAtDeadline = Runnable { bestEffort(TAG, "client.close()") { client.close() } }
        val cutOff =
            runCatching {
                deadlines.schedule(
                    closeAtDeadline,
                    deadlineNanos - System.nanoTime(),
                    TimeUnit.NANOSECONDS,
                )
            }.getOrNull()
        client.use { s ->
            runCatching {
                answer(s, deadlineNanos)
                s.shutdownOutput()
                drain(s, deadlineNanos)
            }.onFailure { RingLog.note(TAG, "request failed: ${it.javaClass.simpleName}") }
        }
        cutOff?.cancel(false)
    }

    private fun answer(
        s: Socket,
        deadlineNanos: Long,
    ) {
        val out = s.getOutputStream()
        val requestLine =
            when (val r = RequestLineReader.read(s, deadlineNanos)) {
                is RequestLine.Line -> r.text
                RequestLine.TooLong -> return respond(out, 414, null, null)
                RequestLine.HeadersTooLarge -> return respond(out, 431, null, null)
                RequestLine.Dropped -> return
            }
        val parts = requestLine.split(' ')
        val method = parts[0]
        if (parts.size < 2 || method !in METHODS) return respond(out, 405, null, null)
        if (method == "OPTIONS") return respond(out, 204, null, null)
        val body = resolve(parts[1]) ?: return respond(out, 404, null, null)
        respond(out, 200, body.second, if (method == "HEAD") null else body.first, body.first.size)
    }

    /** Reads what the client still sends after the response, until it closes, briefly, so the close is clean. */
    private fun drain(
        s: Socket,
        deadlineNanos: Long,
    ) {
        val leftMs = (deadlineNanos - System.nanoTime()) / NANOS_PER_MS
        if (leftMs <= 0) return
        s.soTimeout = minOf(leftMs, DRAIN_MS).toInt()
        val input = s.getInputStream()
        val sink = ByteArray(DRAIN_CHUNK)
        runCatching { while (input.read(sink) >= 0) Unit }
    }

    /** The body and content type for [path], or null for anything that isn't this stream. */
    private fun resolve(path: String): Pair<ByteArray, String>? {
        val prefix = "/$token/"
        if (!path.startsWith(prefix)) return null
        val name = path.removePrefix(prefix).substringBefore('?')
        return when {
            name == LiveSegments.PLAYLIST_NAME -> stream.playlist()?.let { it.toByteArray(Charsets.UTF_8) to MIME_PLAYLIST }
            name == LiveSegments.INIT_NAME -> stream.initSegment()?.let { it to MIME_MP4 }
            name.startsWith(LiveSegments.SEGMENT_PREFIX) && name.endsWith(LiveSegments.SEGMENT_SUFFIX) ->
                name
                    .removePrefix(LiveSegments.SEGMENT_PREFIX)
                    .removeSuffix(LiveSegments.SEGMENT_SUFFIX)
                    .toLongOrNull()
                    ?.let { stream.segment(it) }
                    ?.let { it to MIME_MP4 }
            else -> null
        }
    }

    private fun respond(
        out: OutputStream,
        status: Int,
        contentType: String?,
        body: ByteArray?,
        length: Int = body?.size ?: 0,
    ) {
        val reason =
            when (status) {
                200 -> "OK"
                204 -> "No Content"
                404 -> "Not Found"
                414 -> "URI Too Long"
                431 -> "Request Header Fields Too Large"
                else -> "Method Not Allowed"
            }
        val head =
            buildString {
                append("HTTP/1.1 ")
                    .append(status)
                    .append(' ')
                    .append(reason)
                    .append("\r\n")
                if (contentType != null) append("Content-Type: ").append(contentType).append("\r\n")
                if (status != 204) append("Content-Length: ").append(length).append("\r\n")
                append("Access-Control-Allow-Origin: *\r\n")
                append("Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n")
                append("Access-Control-Allow-Headers: Range, Content-Type, Accept-Encoding\r\n")
                append("Access-Control-Expose-Headers: Content-Length\r\n")
                append("Access-Control-Max-Age: 600\r\n")
                append("Accept-Ranges: none\r\n")
                // Live: the playlist changes every second and segments are never re-read.
                append("Cache-Control: no-cache\r\n")
                append("Connection: close\r\n\r\n")
            }
        out.write(head.toByteArray(Charsets.US_ASCII))
        if (body != null) out.write(body)
        out.flush()
    }

    private companion object {
        const val TAG = "CastHttp"
        const val POOL_SIZE = 4
        const val CONNECTION_DEADLINE_MS = 10_000L
        const val NANOS_PER_MS = 1_000_000L
        const val MAX_PER_ADDRESS = 2
        const val TOKEN_BYTES = 16
        const val MIME_PLAYLIST = "application/vnd.apple.mpegurl"
        const val MIME_MP4 = "video/mp4"
        const val DRAIN_MS = 1_000L
        const val DRAIN_CHUNK = 1_024
        val METHODS = setOf("GET", "HEAD", "OPTIONS")

        fun randomToken(): String {
            val bytes = ByteArray(TOKEN_BYTES).also { SecureRandom().nextBytes(it) }
            return bytes.joinToString("") { "%02x".format(it) }
        }
    }
}
