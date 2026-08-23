package dev.geode.engine.audio

class FeatureFrame(
    continuousSlots: Int,
    eventSlots: Int,
) {
    val continuous: FloatArray = FloatArray(continuousSlots)

    val events: FloatArray = FloatArray(eventSlots)

    var epoch: Int = -1
        internal set
}

class FeatureRing(
    val continuousSlots: Int,
    val eventSlots: Int,
    val capacityFrames: Int = 512,
) {
    init {
        require(continuousSlots >= 0 && eventSlots >= 0 && continuousSlots + eventSlots > 0) {
            "need at least one slot"
        }
        require(capacityFrames > 1 && capacityFrames and (capacityFrames - 1) == 0) {
            "capacityFrames must be a power of two above 1, was $capacityFrames"
        }
    }

    enum class Acquire { OK, GAP, NOT_YET_AVAILABLE, EMPTY, DISCONTINUITY }

    private val mask = capacityFrames - 1
    private val sampleAt = LongArray(capacityFrames)
    private val continuousRows = FloatArray(capacityFrames * continuousSlots)
    private val eventRows = FloatArray(capacityFrames * eventSlots)

    @Volatile
    private var written: Int = 0

    @Volatile
    private var epochValue: Int = 0

    val epoch: Int get() = epochValue

    private val oldestReadable: Int get() = maxOf(0, written + capacityFrames / 4 - capacityFrames)

    fun beginEpoch() {
        written = 0
        epochValue += 1
    }

    fun publish(
        sampleIndex: Long,
        continuous: FloatArray,
        events: FloatArray,
    ) {
        require(continuous.size == continuousSlots) { "expected $continuousSlots continuous slots" }
        require(events.size == eventSlots) { "expected $eventSlots event slots" }
        val w = written
        require(w == 0 || sampleIndex > sampleAt[(w - 1) and mask]) {
            "sampleIndex must increase; got $sampleIndex after ${sampleAt[(w - 1) and mask]}"
        }
        val slot = w and mask
        sampleAt[slot] = sampleIndex
        continuous.copyInto(continuousRows, slot * continuousSlots)
        events.copyInto(eventRows, slot * eventSlots)
        written = w + 1
    }

    fun acquireAt(
        sampleIndex: Long,
        spanSamples: Long,
        out: FeatureFrame,
    ): Acquire {
        require(spanSamples >= 0L) { "spanSamples must not be negative" }
        val epochBefore = epochValue
        val w = written
        if (w == 0) return Acquire.EMPTY
        val newestSlot = (w - 1) and mask
        if (sampleIndex > sampleAt[newestSlot]) return Acquire.NOT_YET_AVAILABLE
        val oldest = oldestReadable
        if (sampleIndex < sampleAt[oldest and mask]) return Acquire.GAP

        val base = frameNearest(sampleIndex, oldest, w - 1)
        interpolateInto(sampleIndex, base, w - 1, out)

        val last =
            if (spanSamples <= 0L) {
                base
            } else {
                (frameNearest(sampleIndex + spanSamples, oldest, w - 1) - 1).coerceIn(base, w - 1)
            }
        for (e in 0 until eventSlots) out.events[e] = eventRows[(base and mask) * eventSlots + e]
        for (frame in base + 1..last) {
            val row = (frame and mask) * eventSlots
            for (e in 0 until eventSlots) {
                val v = eventRows[row + e]
                if (v > out.events[e]) out.events[e] = v
            }
        }

        if (epochValue != epochBefore) return Acquire.DISCONTINUITY
        val writtenNow = written
        val oldestNow = maxOf(0, writtenNow + capacityFrames / 4 - capacityFrames)
        if (base < oldestNow) return Acquire.GAP
        out.epoch = epochBefore
        return Acquire.OK
    }

    private fun frameNearest(
        sampleIndex: Long,
        first: Int,
        last: Int,
    ): Int {
        var lo = first
        var hi = last
        while (lo < hi) {
            val mid = (lo + hi + 1) ushr 1
            if (sampleAt[mid and mask] <= sampleIndex) lo = mid else hi = mid - 1
        }
        if (lo < last) {
            val below = sampleIndex - sampleAt[lo and mask]
            val above = sampleAt[(lo + 1) and mask] - sampleIndex
            if (above < below) return lo + 1
        }
        return lo
    }

    private fun interpolateInto(
        sampleIndex: Long,
        base: Int,
        last: Int,
        out: FeatureFrame,
    ) {
        val floor = if (sampleAt[base and mask] <= sampleIndex) base else (base - 1).coerceAtLeast(0)
        val ceil = (floor + 1).coerceAtMost(last)
        val a = sampleAt[floor and mask]
        val b = sampleAt[ceil and mask]
        val t = if (b <= a) 0f else ((sampleIndex - a).toFloat() / (b - a).toFloat()).coerceIn(0f, 1f)
        val rowA = (floor and mask) * continuousSlots
        val rowB = (ceil and mask) * continuousSlots
        for (c in 0 until continuousSlots) {
            val va = continuousRows[rowA + c]
            out.continuous[c] = va + (continuousRows[rowB + c] - va) * t
        }
    }
}
