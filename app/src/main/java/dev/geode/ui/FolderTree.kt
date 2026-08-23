package dev.geode.ui

object FolderTree {
    fun <T> rows(folders: Map<String, List<T>>): Map<String, List<T>> {
        val segments = folders.keys.associateWith { path -> path.split('/').filter(String::isNotEmpty) }
        val depths = folders.keys.associateWithTo(HashMap()) { 1 }
        var widening = true
        while (widening) widening = widenCollisions(segments, depths)
        val labels =
            folders.keys.associateWith { path ->
                label(path, segments.getValue(path), depths.getValue(path))
            }
        val resolved = fallBackToWholePath(labels)
        return folders.entries
            .sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { resolved.getValue(it.key) })
            .associate { (path, items) -> resolved.getValue(path) to items }
    }

    private fun widenCollisions(
        segments: Map<String, List<String>>,
        depths: MutableMap<String, Int>,
    ): Boolean {
        var widened = false
        val sharing = depths.keys.groupBy { label(it, segments.getValue(it), depths.getValue(it)) }
        for ((_, paths) in sharing) {
            if (paths.size < 2) continue
            for (path in paths) {
                if (depths.getValue(path) >= segments.getValue(path).size) continue
                depths[path] = depths.getValue(path) + 1
                widened = true
            }
        }
        return widened
    }

    private fun fallBackToWholePath(labels: Map<String, String>): Map<String, String> {
        val exhausted =
            labels.entries
                .groupBy { it.value }
                .filterValues { it.size > 1 }
                .values
                .flatten()
                .map { it.key }
                .toSet()
        return labels.mapValues { (path, label) -> if (path in exhausted) path else label }
    }

    private fun label(
        path: String,
        segments: List<String>,
        depth: Int,
    ): String = if (segments.isEmpty()) path else segments.takeLast(depth).joinToString("/")
}
