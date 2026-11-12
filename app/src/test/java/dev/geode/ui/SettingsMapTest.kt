package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Settings' fifteen tabs (owner, 2026-11-11; Preferences the same day) as `AppSettingsTab` draws them: [SettingsGroupKey]'s
 * Basics and sections, each a list of [SettingsRowKey]. The regroup moved every row of the ten
 * groups before it; these hold that none was lost or shown twice, and that a page saved by an old
 * name (a process-death bundle across the update) still opens on a tab.
 */
class SettingsMapTest {
    @Test
    fun everyRowIsPlacedExactlyOnce() {
        val placed = SettingsGroupKey.entries.flatMap { it.rows }
        val twice =
            placed
                .groupingBy { it }
                .eachCount()
                .filterValues { it > 1 }
                .keys
        assertTrue("rows on more than one tab or section: $twice", twice.isEmpty())
        assertEquals("rows on no tab", emptySet<SettingsRowKey>(), SettingsRowKey.entries.toSet() - placed.toSet())
    }

    @Test
    fun everySectionBelongsToExactlyOneTab() {
        val sections = SettingsGroupKey.entries.flatMap { it.sections }
        assertEquals(sections.distinct(), sections)
        assertEquals(SettingsSectionKey.entries.toSet(), sections.toSet())
        assertTrue(SettingsSectionKey.entries.all { it.rows.isNotEmpty() })
    }

    @Test
    fun theFifteenTabsInListOrder() {
        assertEquals(
            listOf(
                "PLAYBACK",
                "SOUND_EQ",
                "OUTPUT",
                "LIBRARY",
                "VISUAL_PLAYBACK",
                "SOUND_REACTION",
                "SAFETY",
                "DISPLAYS",
                "WALLPAPER",
                "GESTURES",
                "LOOK",
                "PERFORMANCE",
                "EXPORT",
                "PREFERENCES",
                "HELP_ABOUT",
            ),
            SettingsGroupKey.entries.map { it.name },
        )
        assertTrue("every tab has Basics", SettingsGroupKey.entries.all { it.basics.isNotEmpty() })
    }

    @Test
    fun everyGroupNameFromBeforeTheRegroupOpensATab() {
        val former =
            listOf("PLAYBACK", "BEHAVIOUR", "VISUALS", "SOUND_INPUT", "LOOK", "PERFORMANCE", "LIBRARY", "EXPORT", "HELP", "BACKUP_ABOUT")
        former.forEach { assertNotNull("group $it", SettingsGroupKey.fromName(it)) }
        assertEquals(SettingsGroupKey.GESTURES, SettingsGroupKey.fromName("BEHAVIOUR"))
        assertEquals(SettingsGroupKey.VISUAL_PLAYBACK, SettingsGroupKey.fromName("VISUALS"))
        assertEquals(SettingsGroupKey.SOUND_REACTION, SettingsGroupKey.fromName("SOUND_INPUT"))
        assertEquals(SettingsGroupKey.HELP_ABOUT, SettingsGroupKey.fromName("HELP"))
        assertEquals(SettingsGroupKey.HELP_ABOUT, SettingsGroupKey.fromName("BACKUP_ABOUT"))
    }

    @Test
    fun everySectionNameFromBeforeTheRegroupMapsToATab() {
        val former =
            listOf(
                "SPEED_PITCH",
                "VOLUME_LEVELLING",
                "EQUALIZER",
                "PLAYBACK_MORE",
                "GESTURES",
                "NOTIFICATION",
                "PICTURE_IN_PICTURE",
                "TOUCH",
                "CONNECTED_DISPLAY",
                "WALLPAPER_DREAM",
                "ANALYSIS",
                "LOOK_TEXT",
                "LOOK_LAYOUT",
                "PERF_DETAILS",
                "LYRICS",
                "OTHER_FOLDERS",
                "RECORD",
                "HELP_TOPICS",
                "ABOUT",
            )
        former.forEach { assertNotNull("section $it", SettingsGroupKey.ofSection(it)) }
        assertEquals(SettingsGroupKey.SOUND_EQ, SettingsGroupKey.ofSection("EQUALIZER"))
        assertEquals(SettingsGroupKey.OUTPUT, SettingsGroupKey.ofSection("NOTIFICATION"))
        assertEquals(SettingsGroupKey.GESTURES, SettingsGroupKey.ofSection("TOUCH"))
        assertEquals(SettingsGroupKey.DISPLAYS, SettingsGroupKey.ofSection("CONNECTED_DISPLAY"))
        assertEquals(SettingsGroupKey.WALLPAPER, SettingsGroupKey.ofSection("WALLPAPER_DREAM"))
    }

    @Test
    fun todaysNamesRestoreThemselvesAndUnknownOnesNothing() {
        SettingsGroupKey.entries.forEach { assertEquals(it, SettingsGroupKey.fromName(it.name)) }
        assertNull(SettingsGroupKey.fromName(null))
        assertNull(SettingsGroupKey.fromName("AUTO"))
        assertNull(SettingsGroupKey.ofSection("TIMELINE"))
    }

    /** The rows the four new tabs took over, by the owner's decision, stay where he put them. */
    @Test
    fun theNewTabsHoldTheRowsTheyTookOver() {
        assertRowsOn(
            SettingsGroupKey.OUTPUT,
            SettingsRowKey.PAUSE_UNPLUGGED,
            SettingsRowKey.HEADPHONES_AUTOPLAY,
            SettingsRowKey.NATIVE_OUTPUT,
            SettingsRowKey.BIT_PERFECT,
        )
        assertRowsOn(SettingsGroupKey.VISUAL_PLAYBACK, SettingsRowKey.AUTO_DIRECTOR, SettingsRowKey.STYLE_TIMER)
        assertRowsOn(SettingsGroupKey.SAFETY, SettingsRowKey.FLASH_LIMITS, SettingsRowKey.REDUCED_MOTION)
        assertRowsOn(SettingsGroupKey.DISPLAYS, SettingsRowKey.CONNECTED_DISPLAY, SettingsRowKey.PIP_AUTO_ENTER)
        assertRowsOn(
            SettingsGroupKey.PREFERENCES,
            SettingsRowKey.WATERMARK,
            SettingsRowKey.LIBRARY_PREFS,
            SettingsRowKey.PLAYER_PREFS,
            SettingsRowKey.STAGE_PREFS,
        )
    }

    private fun assertRowsOn(
        tab: SettingsGroupKey,
        vararg rows: SettingsRowKey,
    ) {
        rows.forEach { assertTrue("$it on ${tab.name}", it in tab.rows) }
    }
}
