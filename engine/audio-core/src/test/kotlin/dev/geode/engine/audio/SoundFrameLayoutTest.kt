package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

class SoundFrameLayoutTest {
    private fun header(): String {
        // Gradle runs module tests from the module directory; walk up to the repo root.
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, "core/sound/SoundFrame.h").isFile) dir = dir.parentFile
        return File(requireNotNull(dir) { "core/sound/SoundFrame.h not found" }, "core/sound/SoundFrame.h").readText()
    }

    private fun define(
        text: String,
        name: String,
    ): Int = requireNotNull(Regex("#define $name (\\d+)").find(text)) { "$name missing" }.groupValues[1].toInt()

    @Test
    fun scalarsMatchTheHeaderInOrder() {
        val text = header()
        val body =
            text
                .substringAfter("typedef struct GeodeSoundFrame {")
                .substringBefore("} GeodeSoundFrame;")
                .replace(Regex("/\\*.*?\\*/", RegexOption.DOT_MATCHES_ALL), "")
        val scalars = Regex("float\\s+(\\w+);").findAll(body).map { it.groupValues[1] }.toList()
        assertEquals(scalars, SoundFrameLayout.SCALAR_NAMES)
        assertEquals(define(text, "GEODE_SOUND_SCALARS"), SoundFrameLayout.SCALARS)
    }

    @Test
    fun arraysAndTuningMatchTheHeader() {
        val text = header()
        assertEquals(define(text, "GEODE_SOUND_EQ_BANDS"), SoundFrameLayout.EQ_BANDS)
        assertEquals(define(text, "GEODE_SOUND_SPECTRUM_POINTS"), SoundFrameLayout.SPECTRUM_POINTS)
        assertEquals(define(text, "GEODE_SOUND_WAVEFORM_POINTS"), SoundFrameLayout.WAVEFORM_POINTS)
        assertEquals(define(text, "GEODE_SOUND_TUNING_FLOATS"), SoundFrameLayout.TUNING_FLOATS)
    }
}
