package dev.geode.ui.opaline

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.unit.dp
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File

@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], application = android.app.Application::class, qualifiers = "w400dp-h800dp-xxhdpi")
class OpalineRenderProbe {
    @get:Rule val compose = createComposeRule()

    @Test fun bodies() = renderBodies("bodies", OpalineRenderer.shared)

    @Test fun fallback() = renderBodies("fallback", OpalineRenderer(allowShader = false))

    private fun renderBodies(
        name: String,
        chosen: OpalineRenderer,
    ) {
        compose.setContent {
            CompositionLocalProvider(LocalOpalineRenderer provides chosen) { OpalineTheme {
                val palette = Opaline.palette
                val renderer = Opaline.renderer
                Box(Modifier.fillMaxSize()) {
                    Image(
                        OpalineTextures.backdrop(palette, 480, 960).asImageBitmap(),
                        null,
                        Modifier.fillMaxSize(),
                        contentScale = ContentScale.FillBounds,
                    )
                    Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(28.dp)) {
                        Text("usesShader=${renderer.usesShader}")
                        Row(horizontalArrangement = Arrangement.spacedBy(24.dp), verticalAlignment = Alignment.CenterVertically) {
                            Box(Modifier.width(150.dp).height(56.dp).opalineBody(OpalineBody.Action, palette, renderer), Alignment.Center) {
                                Text("Play all", color = palette.onGel)
                            }
                            Box(Modifier.size(56.dp).opalineBody(OpalineBody.Lens, palette, renderer))
                            Box(Modifier.size(88.dp).opalineBody(OpalineBody.Puck, palette, renderer))
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(24.dp), verticalAlignment = Alignment.CenterVertically) {
                            Box(
                                Modifier.width(150.dp).height(56.dp).opalineBody(
                                    OpalineBody.Action,
                                    palette,
                                    renderer,
                                    contact = pressed(Offset(120f, 70f)),
                                ),
                            )
                            Box(Modifier.width(150.dp).height(56.dp).opalineBody(OpalineBody.Bead, palette, renderer))
                        }
                        SlabRow("Track title", selected = false)
                        SlabRow("Now playing", selected = true)
                        Box(Modifier.fillMaxWidth().height(52.dp).opalineBody(OpalineBody.Well, palette, renderer))
                        Box(Modifier.fillMaxWidth().height(18.dp).opalineBody(OpalineBody.Channel, palette, renderer))
                        Box(Modifier.fillMaxWidth().height(120.dp).opalineBody(OpalineBody.Panel, palette, renderer))
                    }
                }
            } }
        }
        compose.waitForIdle()
        save(name)
    }

    @Test fun themes() {
        compose.setContent {
            Column {
                OpalineThemeId.entries.forEach { theme ->
                    OpalineTheme(theme) {
                        val palette = Opaline.palette
                        val renderer = Opaline.renderer
                        Box(Modifier.fillMaxWidth().height(130.dp)) {
                            Image(
                                OpalineTextures.backdrop(palette, 120, 40).asImageBitmap(),
                                null,
                                Modifier.fillMaxSize(),
                                contentScale = ContentScale.FillBounds,
                            )
                            Row(
                                Modifier.padding(20.dp),
                                horizontalArrangement = Arrangement.spacedBy(16.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Box(Modifier.width(120.dp).height(52.dp).opalineBody(OpalineBody.Action, palette, renderer), Alignment.Center) {
                                    Text(theme.name, color = palette.onGel)
                                }
                                Box(Modifier.width(180.dp).height(64.dp).opalineBody(OpalineBody.Slab, palette, renderer)) {
                                    Text("Slab text", Modifier.padding(18.dp), color = palette.text)
                                }
                            }
                        }
                    }
                }
            }
        }
        compose.waitForIdle()
        save("themes")
    }

    @Composable
    private fun SlabRow(
        text: String,
        selected: Boolean,
    ) {
        val palette = Opaline.palette
        Box(
            Modifier.fillMaxWidth().height(72.dp).opalineBody(
                OpalineBody.Slab,
                palette,
                Opaline.renderer,
                selected = { if (selected) 1f else 0f },
            ),
        ) {
            Text(text, Modifier.padding(20.dp), color = palette.text)
        }
    }

    private fun pressed(point: Offset) =
        OpalineContact(OpalineSpringState(1f, OpalineMotion.PRESS_FREQUENCY, OpalineMotion.PRESS_DAMPING), mutableStateOf(true)).apply {
            this.point = point
        }

    private fun save(name: String) {
        val bitmap = compose.onRoot().captureToImage().asAndroidBitmap()
        val dir = File("build/opaline-renders").apply { mkdirs() }
        File(dir, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
}
