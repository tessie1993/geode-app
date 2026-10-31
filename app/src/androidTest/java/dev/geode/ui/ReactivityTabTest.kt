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
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.render.EventRecipe
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
 * The reaction editor (now on the Events tab, beside the event recipes), driven through its
 * hoisted state exactly as VisualsHub drives it: the tab only reports edits, and this test plays
 * the ViewModel. The class keeps its name because CI and the device checklist refer to it.
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
                        EventsTab(
                            p = params,
                            onChange = { params = it },
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
        // The click action itself, not a touch at the node's centre: the panel above the routes
        // (recipe chips, a slot note that comes and goes) can move a row between the scroll and
        // the tap, and a coordinate tap then lands on whatever slid under it.
        compose
            .onNodeWithText(text, substring = substring)
            .performScrollTo()
            .performSemanticsAction(SemanticsActions.OnClick)
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

    @Test
    fun aRecipeChipAddsItsRoutesAndTakesThemBackOff() {
        setTab()
        val routes = dna(sceneId, 0)
        // Whichever recipe the style's DNA leaves room for and does not already play.
        val recipe =
            EventRecipe.entries.first {
                it.routesFor(sceneId).isNotEmpty() && !it.isOn(routes, sceneId) && it.fits(routes, sceneId)
            }
        clickText(recipe.title)
        val on = assertNotNullAndGet()
        assertTrue(recipe.isOn(on, sceneId))
        recipe.routesFor(sceneId).forEach { r ->
            assertTrue(summary(r), compose.onAllNodesWithText(summary(r), substring = true).fetchSemanticsNodes().isNotEmpty())
        }

        clickText(recipe.title)
        assertEquals(recipe.toggle(on, sceneId), reactions)
        assertTrue(!recipe.isOn(reactions!!, sceneId))
    }

    private fun assertNotNullAndGet(): List<ReactionRoute> {
        assertNotNull("an edit should make the routes the user's own", reactions)
        return reactions!!
    }
}
