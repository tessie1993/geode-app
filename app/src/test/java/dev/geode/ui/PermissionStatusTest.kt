package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class PermissionStatusTest {
    @Test
    fun refusalAndroidWillExplainCanBeAskedAgain() {
        assertEquals(PermissionStatus.DENIED, statusAfterRefusal(shouldShowRationale = true))
    }

    @Test
    fun refusalAndroidWillNotExplainIsFinalFromInsideTheApp() {
        assertEquals(PermissionStatus.PERMANENTLY_DENIED, statusAfterRefusal(shouldShowRationale = false))
    }
}
