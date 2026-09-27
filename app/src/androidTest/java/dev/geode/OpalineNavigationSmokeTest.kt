package dev.geode

import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.nav.Gate
import dev.geode.nav.Overlay
import dev.geode.nav.Section
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises the real Activity, shell controls, Navigator, and back dispatcher. */
@RunWith(AndroidJUnit4::class)
class OpalineNavigationSmokeTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    @Test fun sectionsAndSearchBack() {
        val activity = compose.activity
        // Let the asynchronous first-run preference load choose its gate. Advance gates only
        // through their real buttons; this test never edits stored preferences directly.
        compose.waitForIdle()
        Thread.sleep(1800)
        repeat(5) {
            val gate = activity.navigator.state.value.gate ?: return@repeat
            if (gate == Gate.BOOT) {
                compose.waitUntil(10_000) { activity.navigator.state.value.gate != Gate.BOOT }
            } else {
                val action =
                    when (gate) {
                        Gate.SAFETY -> R.string.opaline_continue
                        Gate.SETUP -> R.string.opaline_enter
                        Gate.TUTORIAL -> R.string.opaline_start_listening
                        Gate.BOOT -> return@repeat
                    }
                compose.onNodeWithText(activity.getString(action)).performClick()
                compose.waitUntil(10_000) { activity.navigator.state.value.gate != gate }
            }
        }
        compose.waitUntil(10_000) { activity.navigator.state.value.gate == null }

        val tabs =
            listOf(
                Section.PLAYER to R.string.opaline_listen,
                Section.LIBRARY to R.string.nav_library,
                Section.VISUALS to R.string.nav_visuals,
                Section.STUDIO to R.string.nav_studio,
                Section.SETTINGS to R.string.nav_settings,
                Section.PLAYER to R.string.opaline_listen,
            )
        tabs.forEach { (section, label) ->
            compose
                .onNode(
                    SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Tab) and hasText(activity.getString(label)),
                ).performClick()
            compose.waitUntil(10_000) { activity.navigator.state.value.section == section }
            assertEquals(section.root, activity.navigator.state.value.current)
        }

        compose.onNodeWithContentDescription(activity.getString(R.string.action_search)).performClick()
        compose.waitUntil(10_000) { activity.navigator.state.value.overlay == Overlay.Search }
        compose.onNodeWithText(activity.getString(R.string.opaline_search_hint)).assertExists()
        compose.runOnUiThread { activity.onBackPressedDispatcher.onBackPressed() }
        compose.waitUntil(10_000) { activity.navigator.state.value.overlay == null }
        assertNull(activity.navigator.state.value.gate)
        assertEquals(Section.PLAYER, activity.navigator.state.value.section)
    }
}
