package dev.geode.diag

import android.app.ActivityManager.RunningAppProcessInfo
import android.app.ApplicationExitInfo
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File

/** The last process death on the crash file, told once (U13). */
class ExitReasonsTest {
    @get:Rule
    val dirs = TemporaryFolder()

    private val filesDir by lazy { dirs.newFolder("files") }
    private val noBackupDir by lazy { dirs.newFolder("no_backup") }
    private val crashFile get() = File(filesDir, ExitReasons.CRASH_FILE)

    /** What Android recorded for the last process; each [record] builds a fresh [ExitReasons], as a new process does. */
    private var last: LastExit? = null

    private fun record() = ExitReasons(filesDir, noBackupDir) { last }.record()

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

    private fun exit(
        reason: Int,
        importance: Int = RunningAppProcessInfo.IMPORTANCE_FOREGROUND,
        trace: ByteArray? = null,
        timestamp: Long = 1_000L,
    ) {
        last = LastExit(timestamp, reason, importance, status = 0, description = null) { trace?.inputStream() }
    }

    private fun tombstone(
        frames: List<Pair<String, String>>,
        abort: String? = null,
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
