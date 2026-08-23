package dev.geode

object RingLog {
    private const val CAPACITY = 500

    private val entries = ArrayDeque<String>(CAPACITY)

    @Volatile
    var echo: (tag: String, line: String) -> Unit = { _, _ -> }

    fun note(
        tag: String,
        message: String,
        error: Throwable? = null,
    ) {
        val detail = error?.let { " (${it.javaClass.simpleName}: ${it.message ?: "no message"})" } ?: ""
        val line = "${System.currentTimeMillis()} $tag: $message$detail"
        synchronized(entries) {
            if (entries.size == CAPACITY) entries.removeFirst()
            entries.addLast(line)
        }
        echo(tag, "$message$detail")
    }

    fun dump(): String = synchronized(entries) { entries.joinToString("\n") }

    fun clear() = synchronized(entries) { entries.clear() }
}
