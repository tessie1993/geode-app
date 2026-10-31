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
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** The layer lives on the Scene tab, and a search for it finds it there. */
@RunWith(AndroidJUnit4::class)
class SceneTabLayersTest {
    @get:Rule
    val compose = createComposeRule()

    @Before
    fun clean() {
        LayersBus.state.value = LayersUiState()
    }

    @After
    fun restoreLayers() {
        LayersBus.state.value = LayersUiState()
    }

    @Test
    fun layeringSitsOnTheSceneTabAndASearchFindsIt() {
        var query by mutableStateOf("")
        compose.setContent {
            MaterialTheme {
                CompositionLocalProvider(LocalSceneId provides SceneIds.KIFS, LocalParamQuery provides query) {
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        SceneTab(SceneParams.DEFAULT, {}, null, 1f, {}, {})
                    }
                }
            }
        }
        compose.onNodeWithText("Layers enabled").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Layers enabled").performSemanticsAction(SemanticsActions.OnClick)
        compose.waitForIdle()
        assertTrue(LayersBus.state.value.enabled)

        query = "layer"
        compose.onNodeWithText("Layers enabled").performScrollTo().assertIsDisplayed()
    }
}
