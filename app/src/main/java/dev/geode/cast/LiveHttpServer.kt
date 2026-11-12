package dev.geode.cast

import android.os.SystemClock
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.io.OutputStream
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.nio.ByteBuffer
import java.security.SecureRandom
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

/**
 * Serves [LiveSegments] over the local network, to the TV's Cast receiver and to a browser through "Show on a
 * computer": with the Cast SDK's own traffic, the one thing the app's INTERNET permission is for (the owner's
 * rule: Cast is the only internet use).
 *
 * Both fetch by plain HTTP GET from this phone's LAN address. A browser also POSTs its WebRTC offer once
 * ([WebRtcLink], the low-delay path whose media is encrypted), and a computer that typed the short link
 * ([pairUrl], no token in it) POSTs the code the sheet shows: the two requests with a body, whose length native
 * reads from the headers and caps, the bytes read under the same deadline. Only a right code is answered with
 * the token's path (native, core/viz/frame/LinkPair: the code, its tries and lock-out). Every decision is native
 * (core/viz/frame/LinkShare): which path is what under the random [token] (16 bytes from [SecureRandom], new
 * per stream, compared in constant time), the status, and every header (no-store, nosniff, no-referrer, the
 * page's CSP, CORS on the stream's files for the receiver, which fetches from its own https origin). Nothing
 * is used from a request but its method and target; anything else is a bare 404. This class only reads the
 * request line, hands over the bytes the route names and writes the head native gives.
 *
 * Who may connect is decided before a byte is read, by the peer's address (native, `geode_link_admit`): a TV
 * cast lets in the Cast device alone, a live link any site-local peer on the bound subnet; anyone else is
 * closed unread. The token is not secret from the Wi-Fi on a TV cast (the receiver reports the URL to every
 * sender that joins it), so the address is what keeps a third device out. [admit] sets who is wanted.
 *
 * Bounds: the request line is capped at 8 KB (414 past it) and its headers at 16 KB (431), a
 * connection gets one 10 s deadline from accept however slowly it sends or reads (the socket is
 * closed when it passes, mid-write too), and one remote address holds at most six connections (a browser
 * opens up to six to one host, and preconnects some it may never use), so a single device can't take the pool.
 * A response ends with a half-close and a short drain of what the client still sends, so closing never
 * resets a connection with the body in flight.
 * Accepted risks, written down: the stream is plain HTTP with the token in the path (no certificate can be
 * trusted for a LAN address, and the Default Media Receiver can't do TLS to one), so anyone on the Wi-Fi who
 * has the link, or sees the traffic, can watch; the server binds only to the phone's site-local Wi-Fi or
 * Ethernet address and runs only while the stream does.
 *
 * A small fixed pool serves requests: a player asks for one segment a second and the playlist
 * as often, so a handful of threads is plenty, and a slow or stuck client can't grow it. Its queue is bounded
 * too ([QUEUE_SIZE]): past it a connection is closed at once, so many addresses can't pile up sockets.
 *
 * What it refused and what it could not find are noted once, at [stop], in [RingLog]: counts and the first
 * refused address, never a path or the token.
 */
internal class LiveHttpServer(
    private val stream: LiveSegments,
    private val bindAddress: InetAddress,
    /** [bindAddress]'s prefix length on its network: the subnet a live link admits. */
    private val bindPrefix: Int,
    /** Answers a browser's WebRTC offer: the HTTP status and the answer (null unless 200). */
    private val answerOffer: (String) -> Pair<Int, String?> = { STATUS_BAD_REQUEST to null },
) {
    val token: String = randomToken()
    private val socket = ServerSocket()
    private val pool =
        ThreadPoolExecutor(POOL_SIZE, POOL_SIZE, 0L, TimeUnit.MILLISECONDS, ArrayBlockingQueue(QUEUE_SIZE))
    private val deadlines =
        ScheduledThreadPoolExecutor(1) { r -> Thread(r, "cast-http-deadline").apply { isDaemon = true } }
            .apply { removeOnCancelPolicy = true }
    private val perAddress = PerAddressLimit(MAX_PER_ADDRESS)
    private var acceptThread: Thread? = null

    /** Who the stream is for (GEODE_LINK_WANT_*) and the Cast device's IPv4 address (0 = none named). */
    @Volatile
    private var wants = 0

    @Volatile
    private var tvAddress = 0

    /** When the TV last got a file (SystemClock.elapsedRealtime, 0 = never): [CastService] asks if it still plays. */
    private val lastReceiverFetch = AtomicLong(0L)
    val lastReceiverFetchMs: Long get() = lastReceiverFetch.get()

    private val refused = AtomicInteger(0)

    @Volatile
    private var firstRefused: String? = null
    private val notFound = AtomicInteger(0)
    private val overflow = AtomicInteger(0)

    /** Browsers on the live feed now ([feed]). */
    private val feeds = AtomicInteger(0)

    @Volatile
    private var stopped = false

    /** Lets in the Cast device at [tv] while [forTv], and the bound subnet while [forLink]; nobody else. */
    fun admit(
        tv: InetAddress?,
        forTv: Boolean,
        forLink: Boolean,
    ) {
        tvAddress = ipv4(tv)
        wants = (if (forTv) GeodeNative.LINK_WANT_TV else 0) or (if (forLink) GeodeNative.LINK_WANT_LINK else 0)
        GeodeNative.linkPairOpen(forLink, SystemClock.elapsedRealtime())
    }

    /** The port the OS gave the server; valid after [start]. */
    val port: Int get() = socket.localPort

    /** The playlist's URL, as the receiver loads it. */
    val playlistUrl: String
        get() = "${pageUrl}${LiveSegments.PLAYLIST_NAME}"

    /** The page at the bound address, dotted; what the TV's receiver is handed ([playlistUrl]). */
    val pageUrl: String
        get() = "http://${bindAddress.hostAddress}:$port/$token/"

    /**
     * "Show on a computer"'s link as the user sees, copies and shares it: the same page with the host written as one
     * number, not the dotted address (owner: "so link is not exact ip adress"; native, `geode_link_share_url`).
     * It hides the address from the eye, not from the network.
     */
    val shareUrl: String
        get() = GeodeNative.linkShareUrl(ipv4(bindAddress), port, token) ?: pageUrl

    /** The short link a computer types, with no token: its page asks for the code the sheet shows. */
    val pairUrl: String
        get() = GeodeNative.linkPairUrl(ipv4(bindAddress), port) ?: "http://${bindAddress.hostAddress}:$port/"

    /** A short name for this link ("Blue Comet") to show beside its QR code. */
    val label: String get() = GeodeNative.linkLabel(token)

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
        stopped = true
        GeodeNative.linkPairOpen(false, 0L)
        bestEffort(TAG, "socket.close()") { socket.close() }
        pool.shutdownNow()
        deadlines.shutdownNow()
        acceptThread = null
        val refusedCount = refused.get()
        val missing = notFound.get()
        val dropped = overflow.get()
        if (refusedCount > 0 || missing > 0 || dropped > 0) {
            RingLog.note(TAG, "refused $refusedCount (first ${firstRefused ?: "-"}), not found $missing, queue full $dropped")
        }
    }

    private fun acceptLoop() {
        while (!socket.isClosed) {
            val client =
                try {
                    socket.accept()
                } catch (_: SocketException) {
                    return // closed by stop()
                }
            admit(client)
        }
    }

    /** Refuses a stranger or a peer over its share, else hands the connection to the pool. */
    private fun admit(client: Socket) {
        val peer =
            GeodeNative.linkAdmit(ipv4(client.inetAddress), ipv4(bindAddress), bindPrefix, tvAddress, wants)
        val who = client.inetAddress?.hostAddress.orEmpty()
        if (peer == GeodeNative.LINK_PEER_REFUSED) {
            if (refused.getAndIncrement() == 0) firstRefused = who
            bestEffort(TAG, "client.close()") { client.close() }
            return
        }
        if (!perAddress.tryAcquire(who)) {
            bestEffort(TAG, "client.close()") { client.close() }
            return
        }
        val deadline = System.nanoTime() + CONNECTION_DEADLINE_MS * NANOS_PER_MS
        try {
            pool.execute {
                try {
                    serve(client, deadline, peer)
                } finally {
                    perAddress.release(who)
                }
            }
        } catch (_: RejectedExecutionException) {
            overflow.incrementAndGet()
            perAddress.release(who)
            bestEffort(TAG, "client.close()") { client.close() }
        }
    }

    private fun serve(
        client: Socket,
        deadlineNanos: Long,
        peer: Int,
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
                // The browser's feed outlives the connection deadline; its writes get their own (see [feed]).
                if (answer(s, deadlineNanos) { cutOff?.cancel(false) } && peer == GeodeNative.LINK_PEER_RECEIVER) {
                    lastReceiverFetch.set(SystemClock.elapsedRealtime())
                }
                s.shutdownOutput()
                drain(s, deadlineNanos)
            }.onFailure { RingLog.note(TAG, "request failed: ${it.javaClass.simpleName}") }
        }
        cutOff?.cancel(false)
    }

    /** Answers one request; true when it was served a file (200). */
    private fun answer(
        s: Socket,
        deadlineNanos: Long,
        keepOpen: () -> Unit,
    ): Boolean {
        val out = s.getOutputStream()
        val head = RequestLineReader.read(s, deadlineNanos)
        val requestLine =
            when (head) {
                is RequestLine.Line -> head.text
                RequestLine.TooLong -> return respond(out, STATUS_URI_TOO_LONG, NO_RESOURCE, null)
                RequestLine.HeadersTooLarge -> return respond(out, STATUS_HEADERS_TOO_LARGE, NO_RESOURCE, null)
                RequestLine.Dropped -> return false
            }
        val parts = requestLine.split(' ')
        val route = LongArray(2)
        val status = GeodeNative.linkRoute(parts[0], parts.getOrElse(1) { "" }, token, route)
        val resource = route[0].toInt()
        if (status == STATUS_NOT_FOUND) notFound.incrementAndGet()
        if (status != STATUS_OK) return respond(out, status, resource, null)
        if (resource == GeodeNative.LINK_STREAM) return feed(s, out, sendBody = parts[0] != "HEAD", keepOpen)
        if (resource == GeodeNative.LINK_RTC) return offer(s, out, (head as RequestLine.Line).headers, deadlineNanos)
        if (resource == GeodeNative.LINK_PAIR) return pair(s, out, (head as RequestLine.Line).headers, deadlineNanos)
        // A known name with nothing behind it yet (the stream still starting, a segment out of the window): 404.
        val body = body(resource, route[1]) ?: return respond(out, STATUS_NOT_FOUND, NO_RESOURCE, null)
        return respond(out, STATUS_OK, resource, body, sendBody = parts[0] != "HEAD")
    }

    /**
     * The browser's live feed (stream.mp4, "Show on a computer"): the native head (no length: the body ends when the
     * connection does), the init segment, then each fragment the moment [LiveSegments] cuts it, from the newest on,
     * until the stream stops or the browser goes. One response instead of a playlist poll and a request per
     * segment, so a fragment reaches the browser as soon as it exists and the page can hold half a second.
     * At most [GeodeNative.LINK_MAX_STREAMS] at once (503 past it; the page retries). Each write must finish within
     * [FEED_WRITE_MS] or the socket is closed: a browser that stops reading can't hold a pool thread.
     * Never true: the TV's fetches are counted on its own requests.
     */
    private fun feed(
        s: Socket,
        out: OutputStream,
        sendBody: Boolean,
        keepOpen: () -> Unit,
    ): Boolean {
        val init = stream.initSegment()
        val status =
            when {
                init == null -> STATUS_NOT_FOUND
                !sendBody -> STATUS_OK
                feeds.incrementAndGet() > GeodeNative.LINK_MAX_STREAMS -> {
                    feeds.decrementAndGet()
                    STATUS_BUSY
                }
                else -> 0
            }
        if (status != 0 || init == null) {
            respond(out, status, if (status == STATUS_NOT_FOUND) NO_RESOURCE else GeodeNative.LINK_STREAM, null, sendBody = false)
            return false
        }
        try {
            keepOpen()
            s.tcpNoDelay = true
            val head = GeodeNative.linkResponseHead(STATUS_OK, GeodeNative.LINK_STREAM, 0L, token)
            writeWithin(s) {
                out.write(head.toByteArray(Charsets.US_ASCII))
                out.write(init)
                out.flush()
            }
            var sent = -1L
            while (!stopped && !s.isClosed) {
                val next = stream.nextAfter(sent)
                if (next == null) {
                    Thread.sleep(FEED_POLL_MS)
                    continue
                }
                sent = next.first
                writeWithin(s) {
                    out.write(next.second)
                    out.flush()
                }
            }
        } finally {
            feeds.decrementAndGet()
        }
        return false
    }

    /**
     * The browser's WebRTC offer (POST rtc): its body, at most [GeodeNative.WEBRTC_OFFER_MAX] bytes by the length
     * its headers announce (native), answered with the phone's answer or the status native gives a refusal. The
     * answer can take up to two seconds (the phone gathers its own candidate first). Never true: no TV fetch.
     */
    private fun offer(
        s: Socket,
        out: OutputStream,
        headers: List<String>,
        deadlineNanos: Long,
    ): Boolean {
        val body = postBody(s, out, headers, deadlineNanos, GeodeNative.LINK_RTC, GeodeNative.WEBRTC_OFFER_MAX.toLong()) ?: return false
        val (status, answer) = answerOffer(String(body, Charsets.US_ASCII))
        respond(out, status, GeodeNative.LINK_RTC, answer?.toByteArray(Charsets.US_ASCII))
        return false
    }

    /**
     * The typed code (POST pair): native compares it and, when right, gives the token's path, the 200's body; a
     * wrong one is 403, too soon after one or locked out 429, no pairing open 404. Never true: no TV fetch.
     */
    private fun pair(
        s: Socket,
        out: OutputStream,
        headers: List<String>,
        deadlineNanos: Long,
    ): Boolean {
        val body = postBody(s, out, headers, deadlineNanos, GeodeNative.LINK_PAIR, GeodeNative.LINK_PAIR_BODY_MAX) ?: return false
        val path = arrayOfNulls<String>(1)
        val status = GeodeNative.linkPairTry(String(body, Charsets.US_ASCII), SystemClock.elapsedRealtime(), token, path)
        respond(out, status, GeodeNative.LINK_PAIR, path[0]?.toByteArray(Charsets.US_ASCII))
        return false
    }

    /**
     * A POST's body on [resource], at most [max] bytes by the length its headers announce (native); null when it
     * was refused (411 or 413, answered here) or the client stopped sending.
     */
    private fun postBody(
        s: Socket,
        out: OutputStream,
        headers: List<String>,
        deadlineNanos: Long,
        resource: Int,
        max: Long,
    ): ByteArray? {
        val length = GeodeNative.linkBodyLength(headers.toTypedArray(), max)
        val refused =
            when (length) {
                GeodeNative.LINK_NO_BODY -> STATUS_LENGTH_REQUIRED
                GeodeNative.LINK_BAD_BODY -> STATUS_TOO_LARGE
                else -> return RequestLineReader.readBody(s, deadlineNanos, length.toInt())
            }
        respond(out, refused, resource, null)
        return null
    }

    /** Runs [write], closing [s] if it has not finished within [FEED_WRITE_MS] (the write then throws). */
    private inline fun writeWithin(
        s: Socket,
        write: () -> Unit,
    ) {
        val closeIt = Runnable { bestEffort(TAG, "client.close()") { s.close() } }
        val cut = runCatching { deadlines.schedule(closeIt, FEED_WRITE_MS, TimeUnit.MILLISECONDS) }.getOrNull()
        try {
            write()
        } finally {
            cut?.cancel(false)
        }
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

    /** The bytes of a routed [resource] (native names it); null while there are none. */
    private fun body(
        resource: Int,
        sequence: Long,
    ): ByteArray? =
        when (resource) {
            GeodeNative.LINK_PAGE, GeodeNative.LINK_SCRIPT, GeodeNative.LINK_STYLE -> GeodeNative.linkAsset(resource)
            GeodeNative.LINK_PAIR_PAGE, GeodeNative.LINK_PAIR_SCRIPT -> GeodeNative.linkAsset(resource)
            GeodeNative.LINK_MASTER -> stream.masterPlaylist()?.toByteArray(Charsets.UTF_8)
            GeodeNative.LINK_PLAYLIST -> stream.playlist()?.toByteArray(Charsets.UTF_8)
            GeodeNative.LINK_INIT -> stream.initSegment()
            GeodeNative.LINK_SEGMENT -> stream.segment(sequence)
            else -> null
        }

    /** Writes the native head for [status] and [resource], then [body] unless it is a HEAD; true for a 200. */
    private fun respond(
        out: OutputStream,
        status: Int,
        resource: Int,
        body: ByteArray?,
        sendBody: Boolean = true,
    ): Boolean {
        val head = GeodeNative.linkResponseHead(status, resource, body?.size?.toLong() ?: 0L, token)
        out.write(head.toByteArray(Charsets.US_ASCII))
        if (body != null && sendBody) out.write(body)
        out.flush()
        return status == STATUS_OK
    }

    companion object {
        private const val TAG = "CastHttp"
        const val POOL_SIZE = 8

        /** Connections waiting for a thread; a player has one or two in flight, a browser up to six. */
        const val QUEUE_SIZE = 16
        const val CONNECTION_DEADLINE_MS = 10_000L
        const val NANOS_PER_MS = 1_000_000L
        const val MAX_PER_ADDRESS = 6
        const val TOKEN_BYTES = 16
        const val NO_RESOURCE = 0
        const val STATUS_OK = 200
        const val STATUS_BAD_REQUEST = 400
        const val STATUS_NOT_FOUND = 404
        const val STATUS_LENGTH_REQUIRED = 411
        const val STATUS_TOO_LARGE = 413
        const val STATUS_URI_TOO_LONG = 414
        const val STATUS_HEADERS_TOO_LARGE = 431
        const val STATUS_BUSY = 503

        /** How often an idle feed looks for the next fragment, and how long one write to a browser may take. */
        const val FEED_POLL_MS = 10L
        const val FEED_WRITE_MS = 5_000L
        const val DRAIN_MS = 1_000L
        const val DRAIN_CHUNK = 1_024

        /** An IPv4 address as native reads it (host order); 0 for none or IPv6. */
        internal fun ipv4(address: InetAddress?): Int = if (address is Inet4Address) ByteBuffer.wrap(address.address).int else 0

        private fun randomToken(): String {
            val bytes = ByteArray(TOKEN_BYTES).also { SecureRandom().nextBytes(it) }
            return bytes.joinToString("") { "%02x".format(it) }
        }
    }
}
