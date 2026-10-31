package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

class SoundFrameLayoutTest {
    // Gradle runs module tests from the module directory; walk up to the repo root.
    private fun repoFile(path: String): String {
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, path).isFile) dir = dir.parentFile
        return File(requireNotNull(dir) { "$path not found" }, path).readText()
    }

    private fun header(): String = repoFile("core/sound/SoundFrame.h")

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

    @Test
    fun shaderLibraryNamesEveryScalarInItsSlot() {
        val lib = repoFile("app/src/main/assets/shaders/lib_sound.glsl")
        val slots = Regex("#define (SND_\\w+)\\s+uSound\\[(\\d+)]\\.([xyzw])").findAll(lib).toList()
        val expected =
            SoundFrameLayout.SCALAR_NAMES.mapIndexed { i, name ->
                Triple("SND_" + name.replace(Regex("([a-z0-9])([A-Z])"), "$1_$2").uppercase(), i / 4, "xyzw"[i % 4].toString())
            }
        assertEquals(expected, slots.map { Triple(it.groupValues[1], it.groupValues[2].toInt(), it.groupValues[3]) })
        val vec4s = requireNotNull(Regex("uniform vec4 uSound\\[(\\d+)]").find(lib)).groupValues[1].toInt()
        assertEquals((SoundFrameLayout.SCALARS + 3) / 4, vec4s)
    }
}
