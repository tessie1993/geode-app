package dev.geode.engine.bridge

// The JNI declarations of geode_webrtc_jni.cpp (CLAUDE.md).
object WebrtcNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // Show on a computer over WebRTC (core/cast/WebRtcLink, rules in core/viz/frame/WebRtcRule): the browser's
    // low-delay path, its media encrypted with DTLS-SRTP. One handle per cast stream; 0 = none.

    /** Opens the link bound to the server's address: [boundDotted], and [bound] (IPv4, host order) with its [prefix] length. */
    external fun webrtcOpen(
        boundDotted: String,
        bound: Int,
        prefix: Int,
    ): Long

    external fun webrtcClose(handle: Long)
}
