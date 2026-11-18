package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class UnfinishedOutputsTest {
    @get:Rule val tmp = TemporaryFolder()

    private val file: File get() = File(tmp.root, "unfinished-outputs")

    @Test
    fun aWriteThatEndsLeavesNoNote() {
        val outputs = UnfinishedOutputs(file)
        outputs.begin("content://docs/document/a")
        outputs.end("content://docs/document/a")
        assertTrue(outputs.leftOver().isEmpty())
        assertFalse(file.exists())
    }

    @Test
    fun aWriteTheProcessDidNotOutliveIsWhatTheNextProcessFinds() {
        UnfinishedOutputs(file).apply {
            begin("content://docs/document/killed")
            begin("content://docs/document/done")
            end("content://docs/document/done")
        }
        assertEquals(setOf("content://docs/document/killed"), UnfinishedOutputs(file).leftOver())
    }
}
