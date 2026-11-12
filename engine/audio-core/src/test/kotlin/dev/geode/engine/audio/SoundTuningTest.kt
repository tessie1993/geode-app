package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

/**
 * The slot layout only: defaults, ranges, the clamp and how an older save reads are native (`core/sound/Tuning`)
 * and tested in `core/tests/sound_test.cpp`, since this module's tests have no native library. Nothing here reads a value.
 */
class SoundTuningTest {
    private fun header(): String {
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, "core/sound/SoundFrame.h").isFile) dir = dir.parentFile
        return File(requireNotNull(dir) { "core/sound/SoundFrame.h not found" }, "core/sound/SoundFrame.h").readText()
    }

    @Test
    fun slotsFollowTheHeadersFieldOrder() {
        val body =
            header()
                .substringAfter("typedef struct GeodeSoundTuning {")
                .substringBefore("} GeodeSoundTuning;")
                .replace(Regex("/\\*.*?\\*/", RegexOption.DOT_MATCHES_ALL), "")
        var at = 0
        val slots =
            Regex("float\\s+(\\w+)(\\[3])?;").findAll(body).associate { m ->
                (m.groupValues[1] to at).also { at += if (m.groupValues[2].isEmpty()) 1 else 3 }
            }
        val named =
            mapOf(
                "gain" to SoundTuning.GAIN,
                "threshold" to SoundTuning.THRESHOLD,
                "attack" to SoundTuning.ATTACK,
                "release" to SoundTuning.RELEASE,
                "drum" to SoundTuning.DRUM,
                "drumDecay" to SoundTuning.DRUM_DECAY,
                "reactivity" to SoundTuning.REACTIVITY,
                "speed" to SoundTuning.SPEED,
                "onsetThreshold" to SoundTuning.ONSET_THRESHOLD,
                "hardCutLevel" to SoundTuning.HARD_CUT_LEVEL,
                "hardCutHalfLife" to SoundTuning.HARD_CUT_HALF_LIFE,
                "pitchThreshold" to SoundTuning.PITCH_THRESHOLD,
                "noteHold" to SoundTuning.NOTE_HOLD,
                "silenceDb" to SoundTuning.SILENCE_DB,
                "inputTrimDb" to SoundTuning.INPUT_TRIM_DB,
            )
        assertEquals(named, slots.filterKeys(named::containsKey))
    }

    @Test
    fun savedFloatsStayAsStoredWithoutNative() {
        val f = FloatArray(29) { it.toFloat() }
        assertEquals(f.toList(), SoundTuning.fromFloats(f).savedFloats()?.toList())
        assertEquals(SoundTuning.fromFloats(f), SoundTuning.fromFloats(f.copyOf()))
        assertNull(SoundTuning().savedFloats())
    }
}
