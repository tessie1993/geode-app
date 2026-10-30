package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class PermissionStatusTest {
    @Test
    fun refusalAndroidWillExplainCanBeAskedAgain() {
        assertEquals(
            PermissionStatus.DENIED,
            statusAfterRefusal(rationaleBefore = false, rationaleAfter = true, previous = PermissionStatus.NOT_ASKED),
        )
    }

    @Test
    fun rationaleThatWentOffMeansDontAskAgain() {
        assertEquals(
            PermissionStatus.PERMANENTLY_DENIED,
            statusAfterRefusal(rationaleBefore = true, rationaleAfter = false, previous = PermissionStatus.DENIED),
        )
    }

    @Test
    fun aDismissedFirstDialogIsNotFinal() {
        assertEquals(
            PermissionStatus.DENIED,
            statusAfterRefusal(rationaleBefore = false, rationaleAfter = false, previous = PermissionStatus.NOT_ASKED),
        )
    }

    @Test
    fun aSecondSilentRefusalIsFinal() {
        // First tap after a restart for a permission refused for good earlier: no dialog, no
        // rationale. The first tap explains; the second one sends the person to Settings.
        assertEquals(
            PermissionStatus.PERMANENTLY_DENIED,
            statusAfterRefusal(rationaleBefore = false, rationaleAfter = false, previous = PermissionStatus.DENIED),
        )
    }
}
