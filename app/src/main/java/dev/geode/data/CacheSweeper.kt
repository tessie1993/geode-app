package dev.geode.data

import java.io.File

/**
 * Which files under the app's cache a startup sweep may delete. Pure file rules, so a JVM test can
 * drive them in a temp folder; the caller deletes what these name.
 */
object CacheSweeper {
    /** The regular files directly in [dir] whose name starts with one of [prefixes]. */
    fun withPrefix(
        dir: File,
        prefixes: List<String>,
    ): List<File> = dir.listFiles().orEmpty().filter { file -> file.isFile && prefixes.any { file.name.startsWith(it) } }

    /** The regular files anywhere under [dir] last written before [cutoffMs]. */
    fun olderThan(
        dir: File,
        cutoffMs: Long,
    ): List<File> = dir.walkTopDown().filter { it.isFile && it.lastModified() < cutoffMs }.toList()

    /** The regular files anywhere under [dir] except the [keep] most recently written. */
    fun beyondNewest(
        dir: File,
        keep: Int,
    ): List<File> =
        dir
            .walkTopDown()
            .filter { it.isFile }
            .sortedByDescending { it.lastModified() }
            .drop(keep.coerceAtLeast(0))
            .toList()
}
