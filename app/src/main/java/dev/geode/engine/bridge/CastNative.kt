package dev.geode.engine.bridge

// The JNI declarations of geode_cast_jni.cpp (CLAUDE.md), and the cast tee, link and QR calls geode_viz_jni.cpp carries.
object CastNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * The cast thread, its EGL context current: opens the tee the stage copies into, [width] x [height] at [fps];
     * 0 when the driver lacks what it needs (the cast then renders on its own).
     */
    external fun castTeeOpen(
        width: Int,
        height: Int,
        fps: Int,
    ): Long

    /** The cast thread: blits the stage's newest copy into the encoder surface; false while the stage draws nothing. */
    external fun castTeeDraw(
        tee: Long,
        nowNs: Long,
        width: Int,
        height: Int,
    ): Boolean

    /** The cast thread, its context still current: closes the tee. */
    external fun castTeeClose(tee: Long)

    /** True while a new stream leaves this slot empty for the stage's first copy (`geode_cast_tee_await_stage`). Pure. */
    external fun castTeeAwaitStage(
        stageDraws: Boolean,
        fallbackMade: Boolean,
        sinceOpenNs: Long,
    ): Boolean

    /** Ints [castFormat] fills (GEODE_CAST_FORMAT_INTS). */
    const val CAST_FORMAT_INTS = 6

    /** The Cast stream's format into [out] ([CAST_FORMAT_INTS] ints): width, height, fps, keyframe interval (s), video and audio bit/s. */
    external fun castFormat(out: IntArray)

    // Show on a computer (core/viz/frame/LinkShare, LinkPair): the link's state and the typed path's code; and its
    // QR code (core/util/QrCode). The server, its routes and its pairing are native ([castServerOpen]).

    /** The code to show now ("ABC-DEF"), "" while closed; a new one once the last has lived its time. */
    external fun linkPairCode(nowMs: Long): String

    /** Size of the share's state (GEODE_LINK_STATE): phase, reason. */
    const val LINK_STATE = 2
    const val LINK_PHASE_IDLE = 0
    const val LINK_PHASE_STARTING = 1
    const val LINK_PHASE_LIVE = 2
    const val LINK_PHASE_ENDED = 3
    const val LINK_REASON_STREAM = 1
    const val LINK_REASON_NO_LAN = 2
    const val LINK_REASON_BACKGROUND = 3
    const val LINK_REASON_NETWORK = 4
    const val LINK_REASON_TRIAL = 5
    const val LINK_EVENT_START = 1
    const val LINK_EVENT_READY = 2
    const val LINK_EVENT_FAILED = 3
    const val LINK_EVENT_STOP = 4
    const val LINK_EVENT_SHOWN = 5
    const val LINK_ENTITLED = 1
    const val LINK_HAS_LAN = 2
    const val LINK_DO_START_STREAM = 1
    const val LINK_DO_STOP_STREAM = 2
    const val LINK_DO_PAYWALL = 4

    /** One event of the share's state machine over [state] ([LINK_STATE] ints); returns LINK_DO_* bits. */
    external fun linkStep(
        state: IntArray,
        event: Int,
        arg: Int,
    ): Int

    /** [castServerAdmit]'s wants (GEODE_LINK_WANT_*). */
    const val LINK_WANT_TV = 1
    const val LINK_WANT_LINK = 2

    /** [linkReceiverHealth]'s answers (GEODE_LINK_RECEIVER_*). */
    const val LINK_RECEIVER_OK = 0
    const val LINK_RECEIVER_ERROR = 1
    const val LINK_RECEIVER_GONE = 2
    const val LINK_RECEIVER_UNREACHABLE = 3

    /**
     * Whether the TV plays the stream: the LOAD's and the TV's last fetch's times ([nowMs]'s clock, 0 = never)
     * and the receiver's MediaStatus [playerState] and [idleReason]; returns LINK_RECEIVER_*.
     */
    external fun linkReceiverHealth(
        nowMs: Long,
        loadMs: Long,
        lastFetchMs: Long,
        playerState: Int,
        idleReason: Int,
    ): Int

    /** Modules a QR code can have (GEODE_QR_MAX_MODULES), the size of [qrEncode]'s buffer. */
    const val QR_MAX_MODULES = 57 * 57

    /** [text]'s QR code (byte mode, level M) into [out], row-major, 1 = dark; returns its side, 0 when too long. */
    external fun qrEncode(
        text: String,
        out: ByteArray,
    ): Int

    // The cast's HTTP server (core/cast/LiveServer, LiveHttp, LiveSegments): the live stream cut from the muxer's
    // bytes and served on the LAN address under a random token, every request read and answered in native. One
    // handle per cast stream; 0 = none.

    /** [castServerText]'s texts (GEODE_CAST_TEXT_*). */
    const val CAST_TEXT_PLAYLIST_URL = 0
    const val CAST_TEXT_SHARE_URL = 1
    const val CAST_TEXT_LABEL = 2
    const val CAST_TEXT_REFUSALS = 3
    const val CAST_TEXT_PAIR_URL = 4

    /** Size of [castServerState]'s array (GEODE_CAST_STATE) and what each long is. */
    const val CAST_STATE = 5
    const val CAST_STATE_LAST_FETCH_MS = 0
    const val CAST_STATE_SEGMENT_AGE_MS = 1
    const val CAST_STATE_READY = 2
    const val CAST_STATE_AS_MUXED = 3
    const val CAST_STATE_BROKEN = 4

    /**
     * Makes the server for the address [boundDotted] ([bound] IPv4 in host order, [prefix] its length); [webrtc] is the
     * stream's [WebrtcNative.webrtcOpen] handle, asked for the answer to a browser's offer, and must be closed after this one.
     */
    external fun castServerOpen(
        boundDotted: String,
        bound: Int,
        prefix: Int,
        webrtc: Long,
    ): Long

    /** Binds and serves; the port, or 0 when the address could not be bound. */
    external fun castServerStart(handle: Long): Int

    /** Ends every connection and the server's threads; the texts and state stay readable. */
    external fun castServerStop(handle: Long)

    /** Stops and frees. */
    external fun castServerClose(handle: Long)

    /** Who may connect: the Cast device at [tv] (IPv4, host order, 0 = none named) and [wants] LINK_WANT_* bits. */
    external fun castServerAdmit(
        handle: Long,
        tv: Int,
        wants: Int,
    )

    // The cast's encoders (core/cast/CastEncoder, Fmp4Writer): H.264, AAC and, for a browser, Opus, written into the
    // server's stream and to WebRTC on a native thread. One handle per cast stream; 0 = none.

    /** Opens them for the stream's [server] and [webrtc] handles (both closed after this one) and the player's ring [music]. */
    external fun castEncoderOpen(
        server: Long,
        webrtc: Long,
        music: Long,
    ): Long

    /** The video encoder's input surface (an android.view.Surface; the caller releases it), or null. */
    external fun castEncoderSurface(handle: Long): Any?

    /** One video frame's sound up to the frame clock's cut [untilUs]; [current] is the ring the visuals hear now. */
    external fun castEncoderPump(
        handle: Long,
        current: Long,
        untilUs: Long,
    )

    /** True once the video or AAC encoder failed: the cast should end. */
    external fun castEncoderFailed(handle: Long): Boolean

    /** Stops the encoders and frees them, once nothing draws into the surface. */
    external fun castEncoderClose(handle: Long)

    /** [castWatch]'s verdicts (GEODE_CAST_WATCH_*). */
    const val CAST_WATCH_WAIT = 0
    const val CAST_WATCH_READY = 1
    const val CAST_WATCH_START_TIMEOUT = 2
    const val CAST_WATCH_PLAYING = 3
    const val CAST_WATCH_FROZEN = 4
    const val CAST_WATCH_NETWORK_GONE = 5

    /** Whether a cast goes on (core/cast/CastWatch): one CAST_WATCH_* verdict per poll; times in ms on one clock. */
    external fun castWatch(
        announced: Boolean,
        nowMs: Long,
        waitingSinceMs: Long,
        playlistReady: Boolean,
        segmentAgeMs: Long,
        addressHere: Boolean,
    ): Int

    /** When to poll [castWatch] again, in ms. */
    external fun castWatchNextMs(announced: Boolean): Long

    /** True when the cast's own renderer, last drawing at [drewNs], should be freed now that the stage leads. */
    external fun castFallbackRelease(
        nowNs: Long,
        drewNs: Long,
    ): Boolean

    /** One of the CAST_TEXT_* texts; "" when there is none. */
    external fun castServerText(
        handle: Long,
        which: Int,
    ): String?

    /** Fills [out] ([CAST_STATE] longs, CAST_STATE_*). */
    external fun castServerState(
        handle: Long,
        out: LongArray,
    )
}
