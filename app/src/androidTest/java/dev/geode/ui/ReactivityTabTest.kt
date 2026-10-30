package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.ReactMode
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The Reactivity tab's reaction editor, driven through its hoisted state exactly as
 * VisualsHub drives it: the tab only reports edits, and this test plays the ViewModel.
 */
@RunWith(AndroidJUnit4::class)
class ReactivityTabTest {
    @get:Rule
    val compose = createComposeRule()

    private val dna = BuildStyleReactionsUseCase()
    private val sceneId = SceneIds.KIFS

    private var params by mutableStateOf(SceneParams.DEFAULT)
    private var reactions by mutableStateOf<List<ReactionRoute>?>(null)

    private fun summary(r: ReactionRoute) = "${r.source.label} → ${r.target.label}"

    private fun setTab(restoration: StateRestorationTester? = null) {
        val content: @androidx.compose.runtime.Composable () -> Unit = {
            MaterialTheme {
                CompositionLocalProvider(LocalSceneId provides sceneId) {
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        ReactivityTab(
                            p = params,
                            onChange = { params = it },
                            attack = 0.3f,
                            decay = 0.2f,
                            onReactivityChange = { _, _ -> },
                            lfos = List(LfoEngine.SLOTS) { LfoConfig() },
                            onLfoChange = { _, _ -> },
                            adsr = List(AdsrEngine.COUNT) { AdsrConfig() },
                            onAdsrChange = { _, _ -> },
                            reactions = reactions,
                            onReactionsChange = { reactions = it },
                            // What ModulationController.rerollReactions does.
                            onRerollReactions = {
                                reactions = null
                                params = params.copy(reactSeed = params.reactSeed + 1)
                            },
                        )
                    }
                }
            }
        }
        if (restoration != null) restoration.setContent(content) else compose.setContent(content)
    }

    /** Buttons and chips match their whole label; a route row matches its "source → target" prefix. */
    private fun clickText(
        text: String,
        substring: Boolean = false,
    ) {
        compose.onNodeWithText(text, substring = substring).performScrollTo().performClick()
    }

    private fun openRoute(r: ReactionRoute) = clickText(summary(r), substring = true)

    @Test
    fun showsTheStyleDnaUntilEdited() {
        setTab()
        dna(sceneId, 0).forEach { r ->
            assertTrue(summary(r), compose.onAllNodesWithText(summary(r), substring = true).fetchSemanticsNodes().isNotEmpty())
        }
        compose.onNodeWithText("Back to style DNA").assertDoesNotExist()
    }

    @Test
    fun rerollShowsTheNextDna() {
        setTab()
        clickText("Re-roll DNA")
        assertEquals(1, params.reactSeed)
        assertNull(reactions)
        dna(sceneId, 1).forEach { r ->
            assertTrue(summary(r), compose.onAllNodesWithText(summary(r), substring = true).fetchSemanticsNodes().isNotEmpty())
        }
    }

    @Test
    fun addMakesTheRoutesCustomAndBackToDnaUndoesIt() {
        setTab()
        val routes = dna(sceneId, 0)
        clickText("Add")
        assertEquals(routes.size + 1, reactions!!.size)
        assertEquals(routes, reactions!!.dropLast(1))
        compose.onNodeWithText("Back to style DNA").performScrollTo().assertIsDisplayed()

        clickText("Back to style DNA")
        assertNull(reactions)
        compose.onNodeWithText("Back to style DNA").assertDoesNotExist()
    }

    @Test
    fun editingARouteChangesOnlyThatRoute() {
        setTab()
        val routes = dna(sceneId, 0)
        val first = routes.first()
        openRoute(first)
        clickText(ReactMode.SPIN.label)
        val edited = assertNotNullAndGet()
        assertEquals(ReactMode.SPIN, edited.first().mode)
        assertEquals(routes.drop(1), edited.drop(1))
    }

    @Test
    fun removeDropsTheRoute() {
        setTab()
        val routes = dna(sceneId, 0)
        openRoute(routes.first())
        clickText("Remove")
        assertEquals(routes.drop(1), assertNotNullAndGet())
    }

    @Test
    fun anOpenRouteStaysOpenAcrossStateRestoration() {
        val restoration = StateRestorationTester(compose)
        setTab(restoration)
        openRoute(dna(sceneId, 0).first())
        compose.onNodeWithText("Remove").performScrollTo().assertIsDisplayed()

        restoration.emulateSavedInstanceStateRestore()

        compose.onNodeWithText("Remove").performScrollTo().assertIsDisplayed()
    }

    private fun assertNotNullAndGet(): List<ReactionRoute> {
        assertNotNull("an edit should make the routes the user's own", reactions)
        return reactions!!
    }
}
