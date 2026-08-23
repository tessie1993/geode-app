package dev.geode.util

import android.util.Log

/**
 * Runs [block] for its side effect where a failure is genuinely acceptable — teardown, cache
 * eviction, releasing a system resource that may already be gone. The failure is logged rather
 * than dropped, so a deliberate best-effort call is distinguishable from a swallowed error.
 *
 * Do not use this where the failure changes what the user sees: return the failure instead.
 */
inline fun bestEffort(
    tag: String,
    what: String,
    block: () -> Unit,
) {
    runCatching(block).onFailure { Log.w(tag, "$what failed", it) }
}
