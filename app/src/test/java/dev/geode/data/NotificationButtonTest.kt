package dev.geode.data

import dev.geode.data.NotificationButton.FAVOURITE
import dev.geode.data.NotificationButton.REPEAT
import dev.geode.data.NotificationButton.SHUFFLE
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Android's media controls draw two custom buttons, in the order the session lists them. The
 * defaults are shuffle and repeat, so a heart listed third never reached the notification.
 */
class NotificationButtonTest {
    @Test
    fun aChosenFavouriteIsAmongTheButtonsTheNotificationHasRoomFor() {
        val shown = NotificationButton.ordered(setOf(SHUFFLE, REPEAT, FAVOURITE)).take(NotificationButton.ROOM)
        assertTrue(FAVOURITE in shown)
    }

    @Test
    fun theDefaultsKeepTheirOrder() {
        assertEquals(listOf(SHUFFLE, REPEAT), NotificationButton.ordered(setOf(REPEAT, SHUFFLE)))
    }

    @Test
    fun switchingAThirdOnKeepsItAndDropsOneOther() {
        assertEquals(setOf(FAVOURITE, SHUFFLE), NotificationButton.toggled(setOf(SHUFFLE, REPEAT), FAVOURITE, on = true))
        assertEquals(setOf(REPEAT, FAVOURITE), NotificationButton.toggled(setOf(FAVOURITE, SHUFFLE), REPEAT, on = true))
    }

    @Test
    fun switchingOffOnlyRemovesThatButton() {
        assertEquals(setOf(REPEAT), NotificationButton.toggled(setOf(SHUFFLE, REPEAT), SHUFFLE, on = false))
        assertEquals(emptySet<NotificationButton>(), NotificationButton.toggled(setOf(FAVOURITE), FAVOURITE, on = false))
    }
}
