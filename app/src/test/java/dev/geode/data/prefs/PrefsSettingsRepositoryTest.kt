package dev.geode.data.prefs

import android.content.SharedPreferences
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsCodec
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

/** The one settings repository (#1009, PR 3): load, atomic updates, the order writes reach the file. */
@OptIn(ExperimentalCoroutinesApi::class)
class PrefsSettingsRepositoryTest {
    private data class Two(
        val a: Int = 1,
        val b: Int = 2,
    )

    /** Two ints in a file, `a` held to 0..[MAX]. */
    private object TwoCodec : PrefsCodec<Two> {
        const val MAX = 1_000

        override val defaults = Two()

        override val keys = prefKeys("test") { portable(PrefType.INT, "a", "b") }

        override fun read(prefs: SharedPreferences) = Two(prefs.getInt("a", defaults.a), prefs.getInt("b", defaults.b))

        override fun write(
            editor: SharedPreferences.Editor,
            value: Two,
        ) {
            editor.putInt("a", value.a).putInt("b", value.b)
        }

        override fun normalize(value: Two) = value.copy(a = value.a.coerceIn(0, MAX))
    }

    private val file = RecordingPrefs()

    private fun TestScope.repository() = PrefsSettingsRepository(file.prefs, TwoCodec, this, StandardTestDispatcher(testScheduler))

    @Test
    fun valueIsTheDefaultsUntilTheFileIsReadThenTheSavedValue() =
        runTest {
            file.values += mapOf("a" to 5_000, "b" to 7)
            val repo = repository()
            assertEquals(Two(), repo.value.value)
            // Held to its range on the way in, as every later update is.
            assertEquals(Two(a = TwoCodec.MAX, b = 7), repo.loaded())
            assertEquals(Two(a = TwoCodec.MAX, b = 7), repo.value.value)
        }

    @Test
    fun anUpdateBeforeTheLoadWaitsForItInsteadOfWritingOverTheSavedValue() =
        runTest {
            file.values += mapOf("a" to 3, "b" to 7)
            val repo = repository()
            repo.update { it.copy(a = 4) }
            assertEquals(Two(a = 4, b = 7), repo.value.value)
            assertEquals(mapOf("a" to 4, "b" to 7), file.values.toMap())
        }

    @Test
    fun concurrentUpdatesOfDifferentFieldsEachSeeTheOthersResult() =
        runTest {
            val repo = repository()
            repeat(TIMES) { launch { repo.update { it.copy(a = it.a + 1) } } }
            repeat(TIMES) { launch { repo.update { it.copy(b = it.b + 1) } } }
            advanceUntilIdle()
            val expected = Two(a = 1 + TIMES, b = 2 + TIMES)
            assertEquals(expected, repo.value.value)
            assertEquals(mapOf("a" to expected.a, "b" to expected.b), file.values.toMap())
        }

    @Test
    fun writesReachTheFileInTheOrderTheUpdatesRan() =
        runTest {
            val repo = repository()
            repo.loaded()
            val seen = mutableListOf<Int>()
            for (i in 1..TIMES) {
                launch {
                    repo.update { it.copy(a = i) }
                    seen += file.values["a"] as Int
                }
            }
            advanceUntilIdle()
            assertEquals((1..TIMES).toList(), seen)
            assertEquals(TIMES, file.values["a"])
        }

    @Test
    fun anUpdateIsNormalizedBeforeItIsPublishedOrWritten() =
        runTest {
            val repo = repository()
            repo.update { it.copy(a = -5) }
            assertEquals(0, repo.value.value.a)
            assertEquals(0, file.values["a"])
        }

    @Test
    fun thePlayerPrefsRoundTripAndDropTheRetiredEngineKeys() {
        file.values += mapOf("native_engine" to true, "crossfade_ms" to 400, "gapless" to true)
        val prefs = PlayerPrefs(shuffle = true, repeatMode = 2, speed = 1.5f, fadeMs = 3_000, crossfadeMs = 4_000, nativeOutput = true)
        file.prefs
            .edit()
            .also { PlayerPrefsCodec.write(it, prefs) }
            .apply()
        assertEquals(prefs, PlayerPrefsCodec.read(file.prefs))
        for (retired in listOf("native_engine", "crossfade_ms", "gapless")) assertFalse(retired, retired in file.values)
        assertEquals(PlayerPrefs().copy(speed = PlayerPrefs.MAX_SPEED), PlayerPrefsCodec.normalize(PlayerPrefs(speed = 9f)))
    }

    private companion object {
        const val TIMES = 20
    }
}
