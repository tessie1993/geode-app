package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

class SoundTuningTest {
    private fun header(): String {
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, "core/sound/SoundFrame.h").isFile) dir = dir.parentFile
        return File(requireNotNull(dir) { "core/sound/SoundFrame.h not found" }, "core/sound/SoundFrame.h").readText()
    }

    @Test
    fun floatsFollowTheHeadersFieldOrder() {
        val body =
            header()
                .substringAfter("typedef struct GeodeSoundTuning {")
                .substringBefore("} GeodeSoundTuning;")
                .replace(Regex("/\\*.*?\\*/", RegexOption.DOT_MATCHES_ALL), "")
        val fields = Regex("float\\s+(\\w+)(\\[3])?;").findAll(body).map { it.groupValues[1] to it.groupValues[2].isNotEmpty() }.toList()
        assertEquals(
            listOf(
                "gain" to true,
                "threshold" to true,
                "offset" to true,
                "attack" to true,
                "release" to true,
                "drum" to true,
                "drumDecay" to true,
                "reactivity" to false,
                "speed" to false,
                "onsetThreshold" to false,
                "hardCutLevel" to false,
                "hardCutHalfLife" to false,
                "pitchThreshold" to false,
                "noteHold" to false,
                "silenceDb" to false,
            ),
            fields,
        )
        assertEquals(SoundFrameLayout.TUNING_FLOATS, SoundTuning().toFloats().size)
    }

    @Test
    fun theWireFormPlacesEachField() {
        val f = SoundTuning(reactivity = 2f, drum = listOf(0.7f, 0.8f, 0.9f), silenceDb = -50f).toFloats()
        assertEquals(0.7f, f[15])
        assertEquals(0.9f, f[17])
        assertEquals(2f, f[21])
        assertEquals(-50f, f[28])
    }

    @Test
    fun roundTripsThroughFloats() {
        val t = SoundTuning(reactivity = 1.5f, speed = 0.5f, gain = listOf(2f, 1f, 0.5f), silenceDb = -45f)
        assertEquals(t, SoundTuning.fromFloats(t.toFloats()))
        assertEquals(SoundTuning(), SoundTuning.fromFloats(FloatArray(3)))
    }

    @Test
    fun outOfRangeAndBrokenValuesAreClamped() {
        val t = SoundTuning(reactivity = 9f, speed = Float.NaN, gain = listOf(1f), attack = listOf(-1f, 0.5f, 5f)).clamped()
        assertEquals(3f, t.reactivity)
        assertEquals(1f, t.speed)
        assertEquals(SoundTuning().gain, t.gain)
        assertEquals(listOf(0f, 0.5f, 1f), t.attack)
    }
}
