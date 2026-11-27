package dev.geode.diag

import android.app.ActivityManager
import android.app.ActivityManager.RunningAppProcessInfo
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import androidx.annotation.WorkerThread
import dev.geode.BuildConfig
import dev.geode.RingLog
import dev.geode.data.AtomicWrite
import dev.geode.engine.bridge.VizNative
import java.io.File
import java.io.InputStream
import java.time.Instant
import java.util.Locale

/** A process death worth telling the user about, as the crash dialog words it. */
enum class ExitKind(
    val slug: String,
) {
    DRIVER_CRASH("driver-crash"),
    NATIVE_CRASH("native-crash"),
    NOT_RESPONDING("not-responding"),
    LOW_MEMORY("low-memory"),
    SYSTEM_KILL("system-kill"),
    ;

    companion object {
        /** The kind a crash report names on its `Exit:` line; null for the uncaught-exception handler's report. */
        fun of(report: String): ExitKind? {
            val slug =
                report
                    .lineSequence()
                    .take(HEADER_LINES)
                    .firstOrNull { it.startsWith(EXIT_PREFIX) }
                    ?.removePrefix(EXIT_PREFIX)
                    ?.trim()
            return entries.firstOrNull { it.slug == slug }
        }
    }
}

internal const val EXIT_PREFIX = "Exit: "
private const val HEADER_LINES = 4

/** What [ExitReasons] reads from an [ApplicationExitInfo]; the trace is read only for a crash or an ANR. */
class LastExit(
    val timestamp: Long,
    val pid: Int,
    val reason: Int,
    val importance: Int,
    val status: Int,
    val description: String?,
    val summary: ByteArray?,
    val trace: () -> InputStream?,
) {
    val key: String get() = "$timestamp:$pid"
}

/**
 * Explains, on the next start and once, the last process death the uncaught-exception handler cannot
 * see: a native or graphics driver crash, an ANR, or the system (low memory, an OEM battery manager)
 * killing the app while the user had it in front of them or a render ran. Android keeps the reason
 * ([ApplicationExitInfo]); this writes it into the crash file ([CRASH_FILE]) the crash dialog
 * already shows and copies. The newest exit handled is kept in `no_backup` so it is told once.
 */
class ExitReasons(
    filesDir: File,
    noBackupDir: File,
    private val lastUpdateTime: () -> Long,
    private val exitOf: (pid: Int) -> LastExit?,
) {
    constructor(context: Context) : this(
        context.filesDir,
        context.noBackupFilesDir,
        { context.packageManager.getPackageInfo(context.packageName, 0).lastUpdateTime },
        { pid -> exitOf(context, pid) },
    )

    private val seenFile = File(noBackupDir, SEEN_FILE)
    private val crashFile = File(filesDir, CRASH_FILE)

    /**
     * Writes the last exit's report when it needs one; the kind written, or null. A native crash whose tombstone
     * is not there yet is tried again on the next few starts.
     */
    @WorkerThread
    fun record(): ExitKind? {
        val seen = Seen.read(seenFile)
        val retry = seen.pending?.let { key -> exitOf(key.substringAfter(':').toIntOrNull() ?: 0)?.takeIf { it.key == key } }
        val newest = exitOf(0)
        if (retry == null && newest == null) return null
        var written: ExitKind? = null
        var pending: String? = null

        fun handle(
            exit: LastExit,
            tries: Int,
        ) {
            when (val outcome = outcome(exit, tries)) {
                Outcome.Retry -> pending = "${exit.key}:${tries + 1}"
                is Outcome.Done -> written = outcome.kind ?: written
            }
        }
        retry?.let { handle(it, seen.tries) }
        if (newest != null && !seen.covers(newest) && newest.key != retry?.key) handle(newest, 0)
        AtomicWrite.text(seenFile, listOfNotNull(newest?.key ?: seen.line, pending).joinToString("\n"))
        return written
    }

    private fun outcome(
        last: LastExit,
        tries: Int,
    ): Outcome {
        var tombstone: Tombstone? = null
        var anrLines = emptyList<String>()
        when (last.reason) {
            ApplicationExitInfo.REASON_CRASH_NATIVE -> {
                val trace = open(last)
                if (trace == null && tries + 1 < TRACE_TRIES) return Outcome.Retry
                tombstone = trace?.use { read(it, Tombstone::parse) }
            }
            ApplicationExitInfo.REASON_ANR ->
                anrLines = open(last)?.use { read(it) { s -> mainThreadOf(s.readCapped(TRACE_MAX_BYTES)) } }.orEmpty()
        }
        val kind = kindOf(last.reason, last.importance, tombstone?.frames.orEmpty())
        if (kind == null || !replaces(kind)) return Outcome.Done(null)
        AtomicWrite.text(crashFile, report(kind, last, tombstone, anrLines))
        return Outcome.Done(kind)
    }

    private fun open(last: LastExit): InputStream? =
        runCatching { last.trace() }
            .onFailure { RingLog.note(TAG, "exit trace unopenable", it) }
            .getOrNull()

    private fun <T> read(
        trace: InputStream,
        parse: (InputStream) -> T,
    ): T? =
        runCatching { parse(trace) }
            .onFailure { RingLog.note(TAG, "exit trace unreadable", it) }
            .getOrNull()

    /** An unseen report is replaced only by one at least as serious: a kill never hides a crash or an ANR. */
    private fun replaces(kind: ExitKind): Boolean {
        if (!crashFile.exists()) return true
        val head = runCatching { crashFile.useLines { it.take(HEADER_LINES).joinToString("\n") } }.getOrNull() ?: return false
        return rank(kind) >= rank(ExitKind.of(head))
    }

    private fun versionLine(last: LastExit): String {
        val died = last.summary?.let { String(it, Charsets.UTF_8).trim() }.takeUnless { it.isNullOrEmpty() }
        return when {
            died != null -> "BeatRealms $died"
            last.timestamp < runCatching(lastUpdateTime).getOrDefault(0L) ->
                "BeatRealms older than ${BuildConfig.VERSION_NAME} (before an update)"
            else -> "BeatRealms ${BuildConfig.VERSION_NAME}"
        }
    }

    private fun report(
        kind: ExitKind,
        last: LastExit,
        tombstone: Tombstone?,
        anrLines: List<String>,
    ): String =
        buildString {
            appendLine(versionLine(last))
            appendLine("$EXIT_PREFIX${kind.slug}")
            appendLine("When: ${Instant.ofEpochMilli(last.timestamp)}")
            appendLine("Reason: ${reasonName(last.reason)}, status ${last.status}, importance ${last.importance}")
            last.description?.takeIf { it.isNotBlank() }?.let { appendLine("Description: $it") }
            appendLine("Device: ${Build.MANUFACTURER} ${Build.MODEL}, ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}")
            appendLine("Android: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
            tombstone?.signal?.let { appendLine("Signal: $it") }
            tombstone?.abort?.let { appendLine("Abort: $it") }
            tombstone?.thread?.let { appendLine("Thread: $it") }
            tombstone?.frames?.forEachIndexed { i, f ->
                appendLine("  #%02d %s%s".format(Locale.ROOT, i, f.file, if (f.function.isEmpty()) "" else " (${f.function})"))
            }
            anrLines.forEach { appendLine(it) }
        }

    companion object {
        /** The crash report the uncaught-exception handler writes and the crash dialog reads. */
        const val CRASH_FILE = "crash-latest.txt"

        private const val SEEN_FILE = "exit-reason-seen"
        private const val TAG = "ExitReasons"
        private const val TRACE_MAX_BYTES = 1 shl 20
        private const val TRACE_TRIES = 3
        private const val SUMMARY_MAX_BYTES = 128
        private val KILLS = setOf(ExitKind.LOW_MEMORY, ExitKind.SYSTEM_KILL)
        private const val ANR_LINES = 40

        /** A driver that crashes usually does so a few frames below its own entry, under libc's abort at most. */
        private const val DRIVER_FRAMES = 8

        /** The vendor GLES, EGL and Vulkan libraries and the system loaders in front of them, by file name. */
        private val GRAPHICS_DRIVER =
            Regex(
                """^(?:libGLES|libEGL|libvulkan|vulkan\.|libmali|libIMGegl|libsrv_um|libpvr|libgsl|libllvm-glnext|libadreno)""" +
                    """|^eglSubDriver|_(?:adreno|mali)\.so$""",
                RegexOption.IGNORE_CASE,
            )

        private fun rank(kind: ExitKind?): Int = if (kind in KILLS) 0 else 1

        /** The version of this process, for [ApplicationExitInfo.getProcessStateSummary] after it dies; 128 bytes at most. */
        internal fun versionSummary(): ByteArray =
            "${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})".toByteArray(Charsets.UTF_8).let {
                if (it.size > SUMMARY_MAX_BYTES) it.copyOf(SUMMARY_MAX_BYTES) else it
            }

        fun stampVersion(context: Context) {
            context.getSystemService(ActivityManager::class.java)?.setProcessStateSummary(versionSummary())
        }

        private var quarantineTold = false

        /**
         * Tells native's style quarantine (core/viz/gl/StyleQuarantine) how the last process died: once, before the
         * first renderer, so every renderer's creator calls this first.
         */
        @Synchronized
        fun tellStyleQuarantine(context: Context) {
            if (quarantineTold) return
            quarantineTold = true
            val reason =
                runCatching { exitOf(context, 0)?.reason }
                    .onFailure { RingLog.note(TAG, "exit reason unreadable for the style quarantine", it) }
                    .getOrNull()
            VizNative.styleQuarantineLastDeath(lastDeathOf(reason))
        }

        /** Only a native crash or an ANR may be a style's doing; a kill, a JVM crash or low memory is not. Null: unknown. */
        internal fun lastDeathOf(reason: Int?): Int =
            when (reason) {
                null -> VizNative.LAST_DEATH_UNKNOWN
                ApplicationExitInfo.REASON_CRASH_NATIVE, ApplicationExitInfo.REASON_ANR -> VizNative.LAST_DEATH_CRASH
                else -> VizNative.LAST_DEATH_OTHER
            }

        /** The newest exit of this app's process, as Android recorded it. */
        private fun exitOf(
            context: Context,
            pid: Int,
        ): LastExit? {
            val am = context.getSystemService(ActivityManager::class.java) ?: return null
            val info = am.getHistoricalProcessExitReasons(context.packageName, pid, 1).firstOrNull() ?: return null
            return LastExit(
                info.timestamp,
                info.pid,
                info.reason,
                info.importance,
                info.status,
                info.description,
                info.processStateSummary,
            ) { info.traceInputStream }
        }

        /**
         * Null for an exit that needs no report: the uncaught-exception handler already wrote a JVM crash, the user
         * or an update ended the process, or the system reclaimed it while it sat cached in the background.
         */
        internal fun kindOf(
            reason: Int,
            importance: Int,
            frames: List<Tombstone.Frame>,
        ): ExitKind? {
            val inUse = importance <= RunningAppProcessInfo.IMPORTANCE_PERCEPTIBLE
            return when (reason) {
                ApplicationExitInfo.REASON_CRASH_NATIVE -> {
                    val inDriver =
                        frames.take(DRIVER_FRAMES).any {
                            GRAPHICS_DRIVER.containsMatchIn(it.file.substringAfterLast('/').substringAfterLast('!'))
                        }
                    if (inDriver) ExitKind.DRIVER_CRASH else ExitKind.NATIVE_CRASH
                }
                ApplicationExitInfo.REASON_ANR -> ExitKind.NOT_RESPONDING
                ApplicationExitInfo.REASON_LOW_MEMORY -> ExitKind.LOW_MEMORY.takeIf { inUse }
                ApplicationExitInfo.REASON_SIGNALED,
                ApplicationExitInfo.REASON_OTHER,
                ApplicationExitInfo.REASON_FREEZER,
                ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE,
                -> ExitKind.SYSTEM_KILL.takeIf { inUse }
                else -> null
            }
        }

        /** An ANR trace is text with every thread; the main thread's block is the one that says why. */
        internal fun mainThreadOf(trace: ByteArray): List<String> {
            val lines = String(trace, Charsets.UTF_8).lines()
            val start = lines.indexOfFirst { it.startsWith("\"main\"") }
            if (start < 0) return lines.take(ANR_LINES)
            return lines.drop(start).takeWhile { it.isNotBlank() }.take(ANR_LINES)
        }

        private fun reasonName(reason: Int): String =
            when (reason) {
                ApplicationExitInfo.REASON_CRASH_NATIVE -> "crash-native"
                ApplicationExitInfo.REASON_ANR -> "anr"
                ApplicationExitInfo.REASON_LOW_MEMORY -> "low-memory"
                ApplicationExitInfo.REASON_SIGNALED -> "signaled"
                ApplicationExitInfo.REASON_OTHER -> "other"
                ApplicationExitInfo.REASON_FREEZER -> "freezer"
                ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "excessive-resource-usage"
                else -> "reason-$reason"
            } + " ($reason)"

        private fun InputStream.readCapped(cap: Int): ByteArray {
            val buf = ByteArray(cap)
            var read = 0
            while (read < cap) {
                val n = read(buf, read, cap - read)
                if (n < 0) break
                read += n
            }
            return buf.copyOf(read)
        }
    }
}

private sealed interface Outcome {
    data object Retry : Outcome

    class Done(
        val kind: ExitKind?,
    ) : Outcome
}

/** `exit-reason-seen`: the newest exit handled (`timestamp:pid`, or a bare timestamp from before), then a pending retry. */
private class Seen(
    val line: String,
    val pending: String?,
    val tries: Int,
) {
    fun covers(exit: LastExit): Boolean = line == exit.key || line.toLongOrNull()?.let { exit.timestamp <= it } == true

    companion object {
        fun read(file: File): Seen {
            val lines = runCatching { file.readLines() }.getOrDefault(emptyList())
            val retry = lines.getOrNull(1)?.trim()
            val tries = retry?.substringAfterLast(':')?.toIntOrNull()
            return Seen(
                lines.firstOrNull()?.trim().orEmpty(),
                retry?.substringBeforeLast(':')?.takeIf { tries != null },
                tries ?: 0,
            )
        }
    }
}
