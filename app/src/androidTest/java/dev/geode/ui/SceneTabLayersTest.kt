package dev.geode.ui

import android.content.Context
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
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** The layer lives in Customize › Advanced › Layers, and a search for it finds it there. */
@RunWith(AndroidJUnit4::class)
class SceneTabLayersTest {
    @get:Rule
    val compose = createComposeRule()

    @Before
    @After
    fun layerOff() {
        val looks = ApplicationProvider.getApplicationContext<Context>().geodeContainer.looks
        GeodeNative.layerEdit(GeodeNative.LAYER_ON, 0f, "").takeIf { it.isNotEmpty() }?.let(looks::saveLayer)
    }

    @Test
    fun layeringSitsOnTheLayersPageAndASearchFindsIt() {
        var query by mutableStateOf("")
        compose.setContent {
            MaterialTheme {
                CompositionLocalProvider(LocalSceneId provides "kifs", LocalParamQuery provides query) {
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        LayersPage()
                    }
                }
            }
        }
        compose.onNodeWithText("Layers enabled").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Layers enabled").performSemanticsAction(SemanticsActions.OnClick)
        compose.waitForIdle()
        assertTrue(GeodeNative.layerValues("kifs", -1)[GeodeNative.LAYER_ON] > 0f)

        query = "layer"
        compose.onNodeWithText("Layers enabled").performScrollTo().assertIsDisplayed()
    }
}
