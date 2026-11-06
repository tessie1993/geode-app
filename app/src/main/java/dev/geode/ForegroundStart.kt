package dev.geode

/**
 * Runs a service's `startForeground` ([start]) and says whether it took. WHY not `bestEffort`: a
 * started service that never reaches the foreground is killed by the system a few seconds later
 * with ForegroundServiceDidNotStartInTimeException, so carrying on after the failure only moves
 * the crash. On failure it is noted in [RingLog] and [stop] runs (the service's `stopSelf`, plus
 * whatever the service must undo), and the caller returns START_NOT_STICKY.
 */
inline fun startedInForeground(
    tag: String,
    start: () -> Unit,
    stop: () -> Unit,
): Boolean =
    runCatching(start)
        .onFailure {
            RingLog.note(tag, "startForeground failed: stopping", it)
            stop()
        }.isSuccess
