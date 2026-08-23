package dev.geode.analysis

class FrameAccumulator(
    private val maxFrames: Int = MAX_OFFLINE_FRAMES,
) {
    init {
        require(maxFrames >= 2) { "maxFrames must be at least 2" }
    }

    private val frames = ArrayList<TimelineFrame>()
    private val pending = ArrayList<TimelineFrame>(2)

    var groupSize: Int = 1
        private set

    val size: Int get() = frames.size

    fun add(frame: TimelineFrame) {
        if (groupSize == 1) {
            frames.add(frame)
        } else {
            pending.add(frame)
            if (pending.size >= groupSize) {
                frames.add(mergeGroup(pending))
                pending.clear()
            }
        }
        if (frames.size >= maxFrames) {
            halveInPlace()
            groupSize *= 2
        }
    }

    fun finish(): List<TimelineFrame> {
        if (pending.isNotEmpty()) {
            frames.add(mergeGroup(pending))
            pending.clear()
        }
        return frames
    }

    private fun halveInPlace() {
        var w = 0
        var r = 0
        while (r + 1 < frames.size) {
            frames[w++] = merge(frames[r], frames[r + 1])
            r += 2
        }
        if (r < frames.size) frames[w++] = frames[r]
        while (frames.size > w) frames.removeAt(frames.size - 1)
    }

    companion object {
        const val MAX_OFFLINE_FRAMES = 108_000

        fun merge(
            a: TimelineFrame,
            b: TimelineFrame,
        ): TimelineFrame {
            val fa = a.features
            val fb = b.features
            val merged =
                fa.copy(
                    beat = fa.beat || fb.beat,
                    onset = maxOf(fa.onset, fb.onset),
                    flux = maxOf(fa.flux, fb.flux),
                    beatStrength = maxOf(fa.beatStrength, fb.beatStrength),
                    transient = maxOf(fa.transient, fb.transient),
                    kick = maxOf(fa.kick, fb.kick),
                    snare = maxOf(fa.snare, fb.snare),
                    hat = maxOf(fa.hat, fb.hat),
                )
            return if (merged == fa) a else TimelineFrame(a.timeMs, merged)
        }

        private fun mergeGroup(group: List<TimelineFrame>): TimelineFrame {
            var acc = group[0]
            for (i in 1 until group.size) acc = merge(acc, group[i])
            return acc
        }
    }
}
