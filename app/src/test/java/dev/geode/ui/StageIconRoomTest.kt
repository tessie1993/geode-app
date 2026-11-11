package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The stage icons and the options: an icon is drawn only where the sheet or the panel leaves room
 * for all of it (owner: the icons overlapped the tall sheet's tabs). Sizes are a 412 × 904 dp
 * phone at density 1: a 40 px top inset, 48 px icons, a 12 px margin, 8 px around the arrows, and
 * the sheet's heights as `sheetAnchors` gives them over a 136 px dock (728 px of room).
 */
class StageIconRoomTest {
    private fun portrait(sheetPx: Float) = room(PHONE_W, PHONE_H, sheetPx = sheetPx, panelPx = 0f)

    private fun room(
        widthPx: Float,
        heightPx: Float,
        sheetPx: Float,
        panelPx: Float,
    ) = stageIconRoom(
        stageWidthPx = widthPx,
        stageHeightPx = heightPx,
        sheetPx = sheetPx,
        panelPx = panelPx,
        topInsetPx = 40f,
        iconPx = 48f,
        marginPx = 12f,
        arrowMarginPx = 8f,
    )

    private val all = StageIconRoom(topRow = true, arrows = true, record = true)

    private val none = StageIconRoom(topRow = false, arrows = false, record = false)

    @Test
    fun aHiddenSheetLeavesEveryIcon() {
        assertEquals(all, portrait(sheetPx = DOCK))
    }

    @Test
    fun atPeekEveryIconStays() {
        assertEquals(all, portrait(sheetPx = 72f + DOCK))
    }

    @Test
    fun atHalfEveryIconStays() {
        assertEquals(all, portrait(sheetPx = 364f + DOCK))
    }

    @Test
    fun theTallSheetCoversTheIconsSoNoneIsDrawn() {
        assertEquals(none, portrait(sheetPx = 728f + DOCK))
    }

    @Test
    fun midDragTheArrowsGoBeforeTheRow() {
        // 200 px in view: the row (to 100 px) and Record fit, centred arrows would reach the row.
        assertEquals(StageIconRoom(topRow = true, arrows = false, record = true), portrait(sheetPx = PHONE_H - 200f))
    }

    @Test
    fun theRowGoesTheMomentTheSheetsTopReachesIt() {
        assertEquals(true, portrait(sheetPx = PHONE_H - 100f).topRow)
        assertEquals(false, portrait(sheetPx = PHONE_H - 99f).topRow)
    }

    @Test
    fun aNarrowLandscapePanelLeavesEveryIcon() {
        assertEquals(all, room(PHONE_H, PHONE_W, sheetPx = 0f, panelPx = 360f + 12f))
    }

    @Test
    fun aWideLandscapePanelLeavesEveryIconBesideIt() {
        assertEquals(all, room(PHONE_H, PHONE_W, sheetPx = 0f, panelPx = 452f + 12f))
    }

    @Test
    fun aPanelThatLeavesLessThanTheRowHidesTheRowAndArrows() {
        assertEquals(
            StageIconRoom(topRow = false, arrows = false, record = true),
            room(600f, 400f, sheetPx = 0f, panelPx = 500f),
        )
    }

    private companion object {
        const val PHONE_W = 412f
        const val PHONE_H = 904f
        const val DOCK = 136f
    }
}
