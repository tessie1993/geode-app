package dev.geode.playback

object QueueOps {
    fun insertNextIndex(
        currentIndex: Int,
        size: Int,
    ): Int {
        if (size <= 0) return 0
        return (currentIndex + 1).coerceIn(0, size)
    }

    fun playOrder(
        count: Int,
        first: Int,
        next: (Int) -> Int,
    ): List<Int> {
        if (count <= 0 || first < 0 || first >= count) return emptyList()
        val order = ArrayList<Int>(count)
        var i = first
        while (i in 0 until count && order.size < count) {
            order += i
            i = next(i)
        }
        return order
    }

    fun timelineIndexOf(
        playOrder: List<Int>,
        displayedIndex: Int,
    ): Int = playOrder.getOrElse(displayedIndex) { -1 }

    /**
     * The one span of queue positions that covers every item whose id is in [patched], or null when
     * none is: what a batch of resolved titles replaces in a single call instead of one per item.
     */
    fun patchSpan(
        ids: List<String>,
        patched: Set<String>,
    ): IntRange? {
        val first = ids.indexOfFirst { it in patched }
        if (first < 0) return null
        return first..ids.indexOfLast { it in patched }
    }
}
