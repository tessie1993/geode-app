package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The reaction editor (Advanced › Reactions) under the Events page's recipes, driven through
 * their hoisted state exactly as CustomizePanel drives them: the pages only report edits, and
 * this test plays the ViewModel, handing each edit to native as ModulationController does.
 * The class keeps its name because CI and the device checklist refer to it.
 */
@RunWith(AndroidJUnit4::class)
class ReactivityTabTest {
    @get:Rule
    val compose = createComposeRule()

    private val sceneId = "kifs"

    private var params by mutableStateOf(SceneParams.DEFAULT)
    private var reactions by mutableStateOf<String?>(null)
    private var handMask by mutableIntStateOf(0)

    /** What ModulationController.editReactions does, without the store. */
    private fun edit(
        op: Int,
        a: Int,
        b: Int,
        value: Float,
    ) {
        val outMask = IntArray(1)
        val routes = GeodeNative.modReactionsEdit(sceneId, params.reactSeed, reactions, handMask, op, a, b, value, outMask)
        reactions = routes
        handMask = if (routes == null) 0 else outMask[0]
    }

    /** The routes native plays, one list of [GeodeNative.MOD_ROUTE_VALUES] fields each. */
    private fun playing(
        seed: Int,
        text: String? = reactions,
        mask: Int = handMask,
    ): List<List<Float>> = GeodeNative.modRoutesPlaying(sceneId, seed, text, mask).toList().chunked(GeodeNative.MOD_ROUTE_VALUES)

    /** The style's own routes for [seed]. */
    private fun dna(seed: Int) = playing(seed, null, 0)

    private fun summary(route: List<Float>): String {
        val source = ModCatalog.label("react", route[GeodeNative.MOD_ROUTE_SOURCE].toInt())
        val target = ModCatalog.label("target", route[GeodeNative.MOD_ROUTE_TARGET].toInt())
        return "$source → $target"
    }

    private fun recipeState(recipe: ModWord) = GeodeNative.modRecipeState(recipe.ordinal, sceneId, params.reactSeed, reactions, handMask)

    private fun setTab(restoration: StateRestorationTester? = null) {
        val content: @androidx.compose.runtime.Composable () -> Unit = {
            MaterialTheme {
                CompositionLocalProvider(LocalSceneId provides sceneId) {
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        EventsTab(p = params, reactions = reactions, handMask = handMask, onEdit = ::edit)
                        // Advanced › Reactions, the route list the recipes write into.
                        ReactionsSection(
                            p = params,
                            reactions = reactions,
                            handMask = handMask,
                            onEdit = ::edit,
                            // What ModulationController.rerollReactions does.
                            onReroll = {
                                edit(GeodeNative.MOD_EDIT_CLEAR, 0, 0, 0f)
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

    private fun openRoute(route: List<Float>) = clickText(summary(route), substring = true)

    private fun assertShown(routes: List<List<Float>>) {
        assertTrue("the style should play something", routes.isNotEmpty())
        routes.forEach { r ->
            assertTrue(summary(r), compose.onAllNodesWithText(summary(r), substring = true).fetchSemanticsNodes().isNotEmpty())
        }
    }

    @Test
    fun showsTheStyleDnaUntilEdited() {
        setTab()
        assertShown(dna(0))
        compose.onNodeWithText("Use the style's own reactions").assertDoesNotExist()
    }

    @Test
    fun rerollShowsTheNextDna() {
        setTab()
        clickText("Shuffle reactions")
        assertEquals(1, params.reactSeed)
        assertNull(reactions)
        assertShown(dna(1))
    }

    @Test
    fun addMakesTheRoutesCustomAndBackToDnaUndoesIt() {
        setTab()
        val routes = dna(0)
        clickText("Add")
        val added = userRoutes()
        assertEquals(routes.size + 1, added.size)
        assertEquals(routes, added.dropLast(1))
        compose.onNodeWithText("Use the style's own reactions").performScrollTo().assertIsDisplayed()

        clickText("Use the style's own reactions")
        assertNull(reactions)
        compose.onNodeWithText("Use the style's own reactions").assertDoesNotExist()
    }

    @Test
    fun editingARouteChangesOnlyThatRoute() {
        setTab()
        val routes = dna(0)
        val spin = ModCatalog.words("mode").first { it.name == "SPIN" }
        openRoute(routes.first())
        // The mode is a dropdown row now: open it, then pick from its menu.
        clickText("Mode")
        clickText(spin.label)
        val edited = userRoutes()
        assertEquals(spin.ordinal, edited.first()[GeodeNative.MOD_ROUTE_MODE].toInt())
        assertEquals(routes.drop(1), edited.drop(1))
    }

    @Test
    fun removeDropsTheRoute() {
        setTab()
        val routes = dna(0)
        openRoute(routes.first())
        clickText("Remove")
        assertEquals(routes.drop(1), userRoutes())
    }

    @Test
    fun anOpenRouteStaysOpenAcrossStateRestoration() {
        val restoration = StateRestorationTester(compose)
        setTab(restoration)
        openRoute(dna(0).first())
        compose.onNodeWithText("Remove").performScrollTo().assertIsDisplayed()

        restoration.emulateSavedInstanceStateRestore()

        compose.onNodeWithText("Remove").performScrollTo().assertIsDisplayed()
    }

    @Test
    fun aRecipeChipAddsItsRoutesAndTakesThemBackOff() {
        setTab()
        val before = dna(0)
        // Whichever recipe the style's DNA leaves room for and does not already play.
        val wanted = GeodeNative.MOD_RECIPE_OFFERED or GeodeNative.MOD_RECIPE_FITS
        val recipe =
            ModCatalog.words("recipe").first {
                recipeState(it) and (wanted or GeodeNative.MOD_RECIPE_ON) == wanted
            }
        // The recipes are one checkable menu now; it stays open while ticking.
        compose
            .onNodeWithContentDescription("Ready-made reactions")
            .performScrollTo()
            .performSemanticsAction(SemanticsActions.OnClick)
        clickText(recipe.label)
        val on = userRoutes()
        assertTrue(recipeState(recipe) and GeodeNative.MOD_RECIPE_ON != 0)
        assertTrue("the recipe should add routes", on.size > before.size)
        assertShown(on)

        clickText(recipe.label)
        assertEquals(0, recipeState(recipe) and GeodeNative.MOD_RECIPE_ON)
        assertTrue("turning it off should take routes away", userRoutes().size < on.size)
    }

    /** The routes after an edit, which must have made them the user's own. */
    private fun userRoutes(): List<List<Float>> {
        assertNotNull("an edit should make the routes the user's own", reactions)
        return playing(params.reactSeed)
    }
}
