package dev.geode.ui

import dev.geode.engine.bridge.GeodeNative

/**
 * The search sheet's rows as the native text filter reads them (`core/library/Search`, `filterRows`,
 * through `GeodeNative.libraryFilter`). Only marshalling: the folding, the matching, the ranking, the dedupe and
 * the cap are native. Fields go in rank order: a track's title, artist, album, then the rest.
 *
 * The rows are encoded to UTF-8 once, here, so a 15k-track library is not re-encoded on every keystroke:
 * build one table per list (remembered on the list) and call [filter] with each query.
 *
 * [keyOf] names the rows that are one thing (a track's uri; null = the row stands alone); among them the
 * first [preferredOf] match is kept, else the first match. Every row has [fieldsPerRow] fields.
 */
class SearchFilter<T>(
    private val items: List<T>,
    fieldsPerRow: Int,
    keyOf: (T) -> String? = { null },
    preferredOf: ((T) -> Boolean)? = null,
    fieldsOf: (T) -> List<String>,
) {
    private val width = fieldsPerRow + 1
    private val cells = arrayOfNulls<ByteArray>(items.size * width)
    private val preferred = preferredOf?.let { p -> BooleanArray(items.size) { p(items[it]) } }

    init {
        items.forEachIndexed { i, item ->
            cells[i * width] = keyOf(item)?.encodeToByteArray()
            val fields = fieldsOf(item)
            for (f in 0 until fieldsPerRow) cells[i * width + 1 + f] = fields.getOrElse(f) { "" }.encodeToByteArray()
        }
    }

    /**
     * The rows every word of [query] is in, title hits first, then artist, album and the rest (the fields'
     * order), list order inside each, at most [max]; none for a blank query. [options] are
     * [GeodeNative.FILTER_TYPOS] and [GeodeNative.FILTER_BLANK_ALL] bits.
     */
    fun filter(
        query: String,
        max: Int = items.size,
        options: Int = 0,
    ): List<T> {
        if (items.isEmpty() || max <= 0) return emptyList()
        val out = IntArray(minOf(max, items.size))
        val count =
            GeodeNative
                .libraryFilter(cells, width - 1, preferred, query.encodeToByteArray(), options, out)
                .coerceIn(0, out.size)
        return List(count) { items[out[it]] }
    }

    companion object {
        /** The most tracks the search sheet lists. */
        const val TRACK_LIMIT = 30
    }
}
