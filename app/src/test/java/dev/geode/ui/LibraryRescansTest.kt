package dev.geode.ui

import dev.geode.data.libraryRescans
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

/** A MediaStore scan notifies once per file; the library rescans once per burst, not per file. */
@OptIn(ExperimentalCoroutinesApi::class)
class LibraryRescansTest {
    @Test
    fun aBurstOfChangesIsOneRescan() =
        runTest {
            val changes =
                flow {
                    repeat(50) {
                        emit(Unit)
                        delay(10)
                    }
                }
            assertEquals(1, changes.libraryRescans(quietMs = 1_000).toList().size)
        }

    @Test
    fun changesAQuietSpellApartAreTwoRescans() =
        runTest {
            val changes =
                flow {
                    emit(Unit)
                    delay(2_000)
                    emit(Unit)
                }
            assertEquals(2, changes.libraryRescans(quietMs = 1_000).toList().size)
        }
}
