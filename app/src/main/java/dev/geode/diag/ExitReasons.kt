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
import java.io.File
import java.io.InputStream
import java.time.Instant

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
    val reason: Int,
    val importance: Int,
    val status: Int,
    val description: String?,
    val trace: () -> InputStream?,
)

/**
 * Explains, on the next start and once, the last process death the uncaught-exception handler cannot
 * see: a native or graphics driver crash, an ANR, or the system (low memory, an OEM battery manager)
 * killing the app while the user had it in front of them or a render ran. Android keeps the reason
 * ([ApplicationExitInfo]); this writes it into the crash file ([CRASH_FILE]) the crash dialog
 * already shows and copies. The newest exit handled is kept in `no_backup` so it is told once.
 */
class ExitReasons(
    private val filesDir: File,
    noBackupDir: File,
    private val lastExit: () -> LastExit?,
) {
    constructor(context: Context) : this(context.filesDir, context.noBackupFilesDir, { lastExitOf(context) })

    private val seenFile = File(noBackupDir, SEEN_FILE)

    /** Writes the last exit's report when it needs one; the kind written, or null. */
    @WorkerThread
    fun record(): ExitKind? {
        val last = lastExit() ?: return null
        val seen = runCatching { seenFile.readText().trim().toLong() }.getOrDefault(0L)
        if (last.timestamp <= seen) return null
        val native = last.reason == ApplicationExitInfo.REASON_CRASH_NATIVE
        val anr = last.reason == ApplicationExitInfo.REASON_ANR
        val trace = if (native || anr) traceOf(last) else null
        val tombstone = trace?.takeIf { native }?.let(Tombstone::parse)
        val kind = kindOf(last.reason, last.importance, tombstone?.frames.orEmpty())
        if (kind != null) {
            val anrLines = trace?.takeIf { anr }?.let(::mainThreadOf).orEmpty()
            AtomicWrite.text(File(filesDir, CRASH_FILE), report(kind, last, tombstone, anrLines))
        }
        AtomicWrite.text(seenFile, last.timestamp.toString())
        return kind
    }

    private fun traceOf(last: LastExit): ByteArray? =
        runCatching { last.trace()?.use { it.readCapped(TRACE_MAX_BYTES) } }
            .onFailure { RingLog.note(TAG, "exit trace unreadable", it) }
            .getOrNull()

    private fun report(
        kind: ExitKind,
        last: LastExit,
        tombstone: Tombstone?,
        anrLines: List<String>,
    ): String =
        buildString {
            appendLine("BeatRealms ${BuildConfig.VERSION_NAME}")
            appendLine("$EXIT_PREFIX${kind.slug}")
            appendLine("When: ${Instant.ofEpochMilli(last.timestamp)}")
            appendLine("Reason: ${reasonName(last.reason)}, status ${last.status}, importance ${last.importance}")
            last.description?.takeIf { it.isNotBlank() }?.let { appendLine("Description: $it") }
            appendLine("Device: ${Build.MANUFACTURER} ${Build.MODEL}, ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}")
            appendLine("Android: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
            tombstone?.signal?.let { appendLine("Signal: $it") }
            tombstone?.abort?.let { appendLine("Abort: $it") }
            tombstone?.thread?.let { appendLine("Thread: $it") }
            tombstone?.frames?.take(REPORT_FRAMES)?.forEachIndexed { i, f ->
                appendLine("  #%02d %s%s".format(i, f.file, if (f.function.isEmpty()) "" else " (${f.function})"))
            }
            anrLines.forEach { appendLine(it) }
        }

    companion object {
        /** The crash report the uncaught-exception handler writes and the crash dialog reads. */
        const val CRASH_FILE = "crash-latest.txt"

        private const val SEEN_FILE = "exit-reason-seen"
        private const val TAG = "ExitReasons"
        private const val TRACE_MAX_BYTES = 1 shl 20
        private const val REPORT_FRAMES = 24
        private const val ANR_LINES = 40

        /** A driver that crashes usually does so a few frames below its own entry, under libc's abort at most. */
        private const val DRIVER_FRAMES = 8

        /** The vendor GLES, EGL and Vulkan libraries and the system loaders in front of them. */
        private val GRAPHICS_DRIVER =
            Regex(
                """/egl/|/hw/vulkan|libGLES|libEGL|libvulkan|adreno|libgsl|libllvm-glnext|mali|libpvr|libIMG|libsrv_um""",
                RegexOption.IGNORE_CASE,
            )

        /** The newest exit of this app's process, as Android recorded it. */
        private fun lastExitOf(context: Context): LastExit? {
            val am = context.getSystemService(ActivityManager::class.java) ?: return null
            val info = am.getHistoricalProcessExitReasons(context.packageName, 0, 1).firstOrNull() ?: return null
            return LastExit(info.timestamp, info.reason, info.importance, info.status, info.description) { info.traceInputStream }
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
                    val inDriver = frames.take(DRIVER_FRAMES).any { GRAPHICS_DRIVER.containsMatchIn(it.file) }
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
