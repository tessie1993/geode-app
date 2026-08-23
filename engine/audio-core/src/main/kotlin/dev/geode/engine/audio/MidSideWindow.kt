package dev.geode.engine.audio

class MidSideWindow(
    private val ring: SampleRing,
    windowFrames: Int,
) {
    init {
        require(windowFrames > 0) { "windowFrames must be positive, was $windowFrames" }
        require(ring.channelCount >= 2) { "a mid/side view needs two ring channels" }
    }

    private val planar = Array(ring.channelCount) { FloatArray(windowFrames) }

    val mid: FloatArray = FloatArray(windowFrames)

    val side: FloatArray = FloatArray(windowFrames)

    fun refresh(): Boolean {
        if (!ring.snapshotLatest(planar)) return false
        val left = planar[0]
        val right = planar[1]
        val sources = ring.sourceChannelCount
        if (sources >= 2) {
            for (i in mid.indices) {
                mid[i] = (left[i] + right[i]) / 2f
                side[i] = (left[i] - right[i]) * 0.5f
            }
        } else {
            left.copyInto(mid)
            side.fill(0f)
        }
        return true
    }
}
