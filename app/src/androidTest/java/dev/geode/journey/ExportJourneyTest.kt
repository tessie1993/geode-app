package dev.geode.journey

import android.content.ContentUris
import android.content.Context
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.MediaStore
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.media3.session.MediaBrowser
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import com.google.common.truth.Truth.assertThat
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * QA #18: on a phone the old Export dialog's options pushed
 * its Start button below the dialog's edge, so a render could not be started. Studio › Render is
 * now one page whose options scroll above a pinned RenderBar; this checks the bar's Render button
 * is on screen. [aShortRenderIsSaved] then renders a short track end to end.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class ExportJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private val context = ApplicationProvider.getApplicationContext<Context>()
    private var browser: MediaBrowser? = null
    private val saved = mutableListOf<Uri>()

    // The bar's button, not the "Render" tab above the page (a Tab has Role.Tab).
    private val renderButton = hasText("Render") and SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button)

    @After
    fun cleanUp() {
        browser?.stopAndRelease()
        // MediaStore rows outlive Orchestrator's clearPackageData; the app may delete its own.
        saved.forEach { context.contentResolver.delete(it, null, null) }
    }

    @Test
    fun renderButtonIsReachable() {
        compose.awaitEmptyPlayer()
        compose.tap("Studio")
        compose.tap("Render")

        compose.awaitNode(renderButton)
        compose.onAllNodes(renderButton).onFirst().assertIsDisplayed()
    }

    /**
     * A free user's render with the page's defaults: the whole (short) track, into the Videos
     * library. It runs the native encoder, the export foreground service and the MediaStore
     * insert, and the result must be a finished (not pending) video with both tracks. The
     * notification rationale comes first on a fresh install; "Not now" is the answer that leaves
     * the render to run without it.
     */
    @Test
    fun aShortRenderIsSaved() {
        compose.awaitEmptyPlayer()
        val b = connectBrowser().also { browser = it }
        onMain {
            b.setMediaItem(TestTrack.mediaItem(TestTrack.write()))
            b.prepare()
        }
        compose.tap("Studio")
        compose.tap("Render")
        compose.tap(renderButton)
        compose.tap("Not now")

        val result = Regex("Saved (.+) to Movies/BeatRealms\\.")
        compose.awaitNode(hasTextMatching(result), RENDER_TIMEOUT_MS)
        val name =
            compose
                .onAllNodes(hasTextMatching(result))
                .onFirst()
                .fetchSemanticsNode()
                .config[SemanticsProperties.Text]
                .firstNotNullOf { result.matchEntire(it.text)?.groupValues?.get(1) }

        val video = finishedVideo(name).also { saved += it }
        MediaMetadataRetriever().use { meta ->
            meta.setDataSource(context, video)
            assertThat(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO)).isEqualTo("yes")
            assertThat(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO)).isEqualTo("yes")
            assertThat(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLong()).isGreaterThan(0L)
        }
    }

    /** The app's video named [name] in the Videos library; a row still pending is a render cut short. */
    private fun finishedVideo(name: String): Uri {
        val collection = MediaStore.Video.Media.EXTERNAL_CONTENT_URI
        val where =
            "${MediaStore.MediaColumns.DISPLAY_NAME} = ? AND ${MediaStore.MediaColumns.OWNER_PACKAGE_NAME} = ? AND " +
                "${MediaStore.MediaColumns.IS_PENDING} = 0"
        val ids =
            context.contentResolver
                .query(collection, arrayOf(MediaStore.MediaColumns._ID), where, arrayOf(name, context.packageName), null)
                ?.use { c -> buildList { while (c.moveToNext()) add(c.getLong(0)) } }
                .orEmpty()
        assertThat(ids).hasSize(1)
        return ContentUris.withAppendedId(collection, ids.single())
    }

    private companion object {
        /**
         * A ceiling, not a wait: the check returns as soon as the result shows. On the CI emulator
         * every frame is drawn and encoded in software.
         */
        const val RENDER_TIMEOUT_MS = 10 * JOURNEY_TIMEOUT_MS
    }
}
