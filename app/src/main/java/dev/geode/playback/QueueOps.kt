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
}
