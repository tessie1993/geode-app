package dev.geode.diag

import android.app.ActivityManager.RunningAppProcessInfo
import android.app.ApplicationExitInfo
import dev.geode.BuildConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.Locale

/** The last process death on the crash file, told once (U13). */
class ExitReasonsTest {
    @get:Rule
    val dirs = TemporaryFolder()

    private val filesDir by lazy { dirs.newFolder("files") }
    private val noBackupDir by lazy { dirs.newFolder("no_backup") }
    private val crashFile get() = File(filesDir, ExitReasons.CRASH_FILE)
    private val seenFile get() = File(noBackupDir, "exit-reason-seen")

    /** What Android recorded, newest first; each [record] builds a fresh [ExitReasons], as a new process does. */
    private var last: LastExit? = null
    private val byPid = HashMap<Int, LastExit>()
    private var updatedAt = 0L

    private fun record() = ExitReasons(filesDir, noBackupDir, { updatedAt }) { pid -> if (pid == 0) last else byPid[pid] }.record()

    @Test
    fun `a native crash in the GPU driver is reported as a driver crash, once`() {
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = tombstone(DRIVER_FRAMES))

        assertEquals(ExitKind.DRIVER_CRASH, record())
        val report = crashFile.readText()
        assertEquals(ExitKind.DRIVER_CRASH, ExitKind.of(report))
        assertTrue(report, report.contains("Signal: SIGSEGV (SEGV_MAPERR)"))
        assertTrue(report, report.contains("Thread: GLThread 42"))
        assertTrue(report, report.contains("#00 /vendor/lib64/egl/libGLESv2_adreno.so (glDrawArrays)"))

        crashFile.delete()
        assertNull("the same exit is told once", record())
        assertFalse(crashFile.exists())
    }

    @Test
    fun `a native crash outside the driver is a native crash with its abort message`() {
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = tombstone(ENGINE_FRAMES, abort = "check failed: frame < count"))

        assertEquals(ExitKind.NATIVE_CRASH, record())
        val report = crashFile.readText()
        assertTrue(report, report.contains("Abort: check failed: frame < count"))
        assertTrue(report, report.contains("libgeode.so (geode::Renderer::draw)"))
    }

    @Test
    fun `an unreadable tombstone still reports the native crash`() {
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = byteArrayOf(0x82.toByte(), 0x01, 0x7f, 0x13))

        assertEquals(ExitKind.NATIVE_CRASH, record())
        assertEquals(ExitKind.NATIVE_CRASH, ExitKind.of(crashFile.readText()))
    }

    @Test
    fun `an ANR carries the main thread from its trace`() {
        val trace =
            "----- pid 1 -----\n\"Signal Catcher\" daemon\n  native: foo\n\n" +
                "\"main\" prio=5 tid=1 Blocked\n  at dev.geode.Foo.bar(Foo.kt:1)\n\n\"other\"\n"
        exit(ApplicationExitInfo.REASON_ANR, trace = trace.toByteArray())

        assertEquals(ExitKind.NOT_RESPONDING, record())
        val report = crashFile.readText()
        assertTrue(report, report.contains("at dev.geode.Foo.bar(Foo.kt:1)"))
        assertFalse(report, report.contains("Signal Catcher"))
    }

    @Test
    fun `a kill while in use is explained, a kill while cached is not`() {
        exit(ApplicationExitInfo.REASON_LOW_MEMORY, importance = RunningAppProcessInfo.IMPORTANCE_FOREGROUND_SERVICE)
        assertEquals(ExitKind.LOW_MEMORY, record())

        crashFile.delete()
        exit(ApplicationExitInfo.REASON_SIGNALED, importance = RunningAppProcessInfo.IMPORTANCE_CACHED, timestamp = 2_000L)
        assertNull(record())
        assertFalse(crashFile.exists())
    }

    @Test
    fun `a JVM crash keeps the handler's report and a user stop is no report`() {
        crashFile.writeText("BeatRealms 1\nThread: main\n\njava.lang.IllegalStateException")
        exit(ApplicationExitInfo.REASON_CRASH)
        assertNull(record())
        assertEquals("BeatRealms 1\nThread: main\n\njava.lang.IllegalStateException", crashFile.readText())
        assertNull(ExitKind.of(crashFile.readText()))

        crashFile.delete()
        exit(ApplicationExitInfo.REASON_USER_REQUESTED, timestamp = 2_000L)
        assertNull(record())
        assertFalse(crashFile.exists())
    }

    @Test
    fun `the report names the version that died, not the one installed now`() {
        exit(ApplicationExitInfo.REASON_ANR, summary = "0.9.1 (901)".toByteArray())
        record()
        assertEquals("BeatRealms 0.9.1 (901)", crashFile.readLines().first())
    }

    @Test
    fun `without a summary, an exit before the last update says so`() {
        updatedAt = 5_000L
        exit(ApplicationExitInfo.REASON_ANR, timestamp = 4_000L)
        record()
        assertTrue(crashFile.readText(), crashFile.readLines().first().endsWith("(before an update)"))

        updatedAt = 1_000L
        exit(ApplicationExitInfo.REASON_ANR, timestamp = 6_000L)
        record()
        assertEquals("BeatRealms ${BuildConfig.VERSION_NAME}", crashFile.readLines().first())
    }

    @Test
    fun `the version summary fits the platform's 128 bytes`() {
        val summary = ExitReasons.versionSummary()
        assertTrue(summary.size <= 128)
        assertEquals("${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})", String(summary))
    }

    @Test
    fun `a kill does not overwrite an unseen crash report`() {
        val jvm = "BeatRealms 1\nThread: main\n\njava.lang.IllegalStateException"
        crashFile.writeText(jvm)
        exit(ApplicationExitInfo.REASON_LOW_MEMORY, pid = 1)
        assertNull(record())
        assertEquals(jvm, crashFile.readText())

        crashFile.delete()
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = tombstone(DRIVER_FRAMES), pid = 2)
        assertEquals(ExitKind.DRIVER_CRASH, record())
        exit(ApplicationExitInfo.REASON_SIGNALED, pid = 3)
        assertNull(record())
        assertEquals(ExitKind.DRIVER_CRASH, ExitKind.of(crashFile.readText()))

        exit(ApplicationExitInfo.REASON_ANR, pid = 4)
        assertEquals(ExitKind.NOT_RESPONDING, record())
        assertEquals(ExitKind.NOT_RESPONDING, ExitKind.of(crashFile.readText()))
    }

    @Test
    fun `a kill replaces an unseen kill and a crash replaces a kill`() {
        exit(ApplicationExitInfo.REASON_LOW_MEMORY, pid = 1)
        assertEquals(ExitKind.LOW_MEMORY, record())
        exit(ApplicationExitInfo.REASON_SIGNALED, pid = 2)
        assertEquals(ExitKind.SYSTEM_KILL, record())
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = tombstone(ENGINE_FRAMES), pid = 3)
        assertEquals(ExitKind.NATIVE_CRASH, record())
        assertEquals(ExitKind.NATIVE_CRASH, ExitKind.of(crashFile.readText()))
    }

    @Test
    fun `an exit with an earlier clock than the last one seen is still new`() {
        exit(ApplicationExitInfo.REASON_ANR, timestamp = 5_000L, pid = 10)
        assertEquals(ExitKind.NOT_RESPONDING, record())
        crashFile.delete()
        assertNull(record())

        exit(ApplicationExitInfo.REASON_ANR, timestamp = 1_000L, pid = 11)
        assertEquals(ExitKind.NOT_RESPONDING, record())
    }

    @Test
    fun `a timestamp-only seen marker from an older build still holds`() {
        seenFile.writeText("5000")
        exit(ApplicationExitInfo.REASON_ANR, timestamp = 5_000L, pid = 10)
        assertNull(record())
        assertFalse(crashFile.exists())
        assertEquals("5000:10", seenFile.readText().lines().first())

        exit(ApplicationExitInfo.REASON_ANR, timestamp = 6_000L, pid = 11)
        assertEquals(ExitKind.NOT_RESPONDING, record())
        assertEquals("6000:11", seenFile.readText().lines().first())
    }

    @Test
    fun `the crashing thread is found after more than a mebibyte of other threads`() {
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = tombstone(DRIVER_FRAMES, padding = (1 shl 20) + 4096))

        assertEquals(ExitKind.DRIVER_CRASH, record())
        assertTrue(crashFile.readText(), crashFile.readText().contains("Thread: GLThread 42"))
    }

    @Test
    fun `frames are collected up to the report's count`() {
        val many = List(100) { "/data/app/~~x/lib/arm64/libgeode.so" to "f$it" }
        val parsed = Tombstone.parse(tombstone(many).inputStream())
        assertEquals(Tombstone.MAX_FRAMES, parsed.frames.size)
        assertEquals("f0", parsed.frames.first().function)
    }

    @Test
    fun `a tombstone cut off inside the crashing thread keeps what came before`() {
        val full = tombstone(DRIVER_FRAMES)
        val parsed = Tombstone.parse(full.copyOf(full.size - 60).inputStream())
        assertEquals("SIGSEGV (SEGV_MAPERR)", parsed.signal)
        assertTrue(parsed.frames.isEmpty())
    }

    @Test
    fun `the driver is told by the library's name, not by a word in its path`() {
        fun kind(file: String) =
            ExitReasons.kindOf(
                ApplicationExitInfo.REASON_CRASH_NATIVE,
                RunningAppProcessInfo.IMPORTANCE_FOREGROUND,
                listOf(Tombstone.Frame(file, "")),
            )
        listOf(
            "/data/app/~~x/lib/arm64/libnormalize.so",
            "/data/app/~~x/base.apk!libimgproc.so",
            "/data/local/tmp/mali/libgeode.so",
            "/data/app/~~x/lib/arm64/libgeode_vulkan.so",
        ).forEach { assertEquals(it, ExitKind.NATIVE_CRASH, kind(it)) }
        listOf(
            "/vendor/lib64/egl/libGLES_mali.so",
            "/vendor/lib64/egl/libGLESv2_adreno.so",
            "/vendor/lib64/egl/libEGL_adreno.so",
            "/vendor/lib64/hw/vulkan.mali.so",
            "/vendor/lib64/hw/vulkan.adreno.so",
            "/system/lib64/libvulkan.so",
            "/vendor/lib64/libmali.so",
            "/vendor/lib64/libIMGegl.so",
            "/vendor/lib64/libsrv_um.so",
            "/vendor/lib64/libllvm-glnext.so",
            "/vendor/lib64/libgsl.so",
            "/vendor/lib64/libadreno_utils.so",
            "/vendor/lib64/egl/libq3dtools_adreno.so",
        ).forEach { assertEquals(it, ExitKind.DRIVER_CRASH, kind(it)) }
    }

    @Test
    fun `frame numbers are ASCII digits in any locale`() {
        val default = Locale.getDefault()
        try {
            Locale.setDefault(Locale.forLanguageTag("fa"))
            exit(ApplicationExitInfo.REASON_CRASH_NATIVE, trace = tombstone(DRIVER_FRAMES))
            record()
        } finally {
            Locale.setDefault(default)
        }
        assertTrue(crashFile.readText(), crashFile.readText().contains("  #00 /vendor/lib64/egl/libGLESv2_adreno.so"))
    }

    @Test
    fun `a native crash whose tombstone is not there yet is retried on the next start`() {
        var trace: ByteArray? = null
        last = LastExit(1_000L, 7, ApplicationExitInfo.REASON_CRASH_NATIVE, FOREGROUND, 0, null, null) { trace?.inputStream() }
        byPid[7] = last!!
        assertNull(record())
        assertFalse(crashFile.exists())

        trace = tombstone(DRIVER_FRAMES)
        exit(ApplicationExitInfo.REASON_SIGNALED, importance = RunningAppProcessInfo.IMPORTANCE_CACHED, timestamp = 2_000L, pid = 8)
        assertEquals(ExitKind.DRIVER_CRASH, record())
        assertTrue(crashFile.readText(), crashFile.readText().contains("#00 /vendor/lib64/egl/libGLESv2_adreno.so"))

        crashFile.delete()
        assertNull(record())
    }

    @Test
    fun `a tombstone that never comes is given up on after a few starts`() {
        exit(ApplicationExitInfo.REASON_CRASH_NATIVE, pid = 7)
        assertNull(record())
        assertNull(record())
        assertEquals(ExitKind.NATIVE_CRASH, record())
        assertEquals(ExitKind.NATIVE_CRASH, ExitKind.of(crashFile.readText()))

        crashFile.delete()
        assertNull(record())
    }

    private fun exit(
        reason: Int,
        importance: Int = FOREGROUND,
        trace: ByteArray? = null,
        timestamp: Long = 1_000L,
        pid: Int = 100,
        summary: ByteArray? = null,
    ) {
        last = LastExit(timestamp, pid, reason, importance, status = 0, description = null, summary = summary) { trace?.inputStream() }
        byPid[pid] = last!!
    }

    private fun tombstone(
        frames: List<Pair<String, String>>,
        abort: String? = null,
        padding: Int = 0,
    ): ByteArray {
        val signal = message { string(2, "SIGSEGV").string(4, "SEGV_MAPERR") }
        val crashing =
            message {
                varint(1, CRASH_TID).string(2, "GLThread 42")
                frames.forEach { (file, fn) -> bytes(4, message { varint(1, 0x1234).string(4, fn).string(6, file) }) }
            }
        val idle = message { varint(1, 7).string(2, "main").bytes(4, message { string(6, "/vendor/lib64/egl/libGLESv2_adreno.so") }) }
        return message {
            varint(5, CRASH_TID).varint(6, CRASH_TID).bytes(10, signal)
            if (abort != null) string(14, abort)
            bytes(16, message { varint(1, 7).bytes(2, idle) })
            if (padding > 0) {
                val big = message { varint(1, 8).string(2, "x".repeat(padding)) }
                bytes(16, message { varint(1, 8).bytes(2, big) })
            }
            bytes(16, message { varint(1, CRASH_TID).bytes(2, crashing) })
            bytes(17, message { string(2, "/apex/com.android.runtime/lib64/bionic/libc.so") })
        }
    }

    private class Writer {
        val out = ByteArrayOutputStream()

        fun varint(
            field: Int,
            value: Long,
        ) = apply {
            raw((field shl 3).toLong())
            raw(value)
        }

        fun string(
            field: Int,
            value: String,
        ) = bytes(field, value.toByteArray())

        fun bytes(
            field: Int,
            value: ByteArray,
        ) = apply {
            raw(((field shl 3) or 2).toLong())
            raw(value.size.toLong())
            out.write(value)
        }

        private fun raw(value: Long) {
            var v = value
            while (v >= 0x80) {
                out.write(((v and 0x7f) or 0x80).toInt())
                v = v ushr 7
            }
            out.write(v.toInt())
        }
    }

    private fun message(build: Writer.() -> Unit): ByteArray = Writer().apply(build).out.toByteArray()

    private companion object {
        const val FOREGROUND = RunningAppProcessInfo.IMPORTANCE_FOREGROUND
        const val CRASH_TID = 4242L
        val DRIVER_FRAMES =
            listOf(
                "/vendor/lib64/egl/libGLESv2_adreno.so" to "glDrawArrays",
                "/data/app/~~x/lib/arm64/libgeode.so" to "geode::Renderer::draw",
            )
        val ENGINE_FRAMES =
            listOf(
                "/apex/com.android.runtime/lib64/bionic/libc.so" to "abort",
                "/data/app/~~x/lib/arm64/libgeode.so" to "geode::Renderer::draw",
            )
    }
}
