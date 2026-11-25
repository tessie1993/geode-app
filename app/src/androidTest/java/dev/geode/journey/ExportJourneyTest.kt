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
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import com.google.common.truth.Truth.assertThat
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportFps
import dev.geode.data.ExportPrefsStore
import dev.geode.export.ExportQuality
import dev.geode.geodeContainer
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
     * A free user's render, end to end: the native encoder, the export foreground service and the
     * MediaStore insert. The result must be a finished (not pending) video with both tracks, and the
     * page must name it. A fresh install asks two things first: whether to add the render to My
     * videos ("Just the video" keeps it to the file this test checks), then the notification
     * rationale ("Not now" lets the render run without the permission).
     *
     * The page's lightest options and the shortest track a render takes: on the CI emulator every
     * frame is drawn and encoded in software, and at the defaults (1080p, a 3 s track) each frame
     * waited seconds on its GL fence, so the render outlasted the test (android.yml run 45541194990).
     * The wait is on MediaStore, not on Compose, which cannot go idle while the render bar moves.
     */
    @Test
    fun aShortRenderIsSaved() {
        compose.awaitEmptyPlayer()
        // The same store the Render page reads when it opens (StudioRender's ExportPrefsStore).
        ExportPrefsStore(context.geodeContainer.prefsFiles.general).save(
            ExportDefaults(
                quality = ExportQuality.entries.minBy { it.shortSide },
                fps = ExportFps.options.first(),
            ),
        )
        val b = connectBrowser().also { browser = it }
        onMain {
            b.setMediaItem(TestTrack.mediaItem(TestTrack.write(seconds = SHORTEST_RENDER_S)))
            b.prepare()
        }
        // Render stays disabled until the player has the track; a disabled button still takes the tap.
        awaitMain("the track to load") { b.playbackState == Player.STATE_READY }
        compose.tap("Studio")
        compose.tap("Render")
        val since = System.currentTimeMillis() / MS_PER_S
        compose.tap(renderButton)
        compose.tap("Just the video")
        compose.tap("Not now")

        awaitMain("the render to be saved", RENDER_TIMEOUT_MS) { finishedVideos(since).isNotEmpty() }
        val (video, name) = finishedVideos(since).also { found -> saved += found.map { it.first } }.single()
        compose.awaitNode(hasText("Saved $name to Movies/BeatRealms."))

        MediaMetadataRetriever().use { meta ->
            meta.setDataSource(context, video)
            assertThat(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO)).isEqualTo("yes")
            assertThat(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO)).isEqualTo("yes")
            assertThat(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLong()).isGreaterThan(0L)
        }
    }

    /**
     * This app's finished videos added since [sinceS] (seconds, MediaStore's DATE_ADDED unit), with
     * their display names. A row still pending is a render under way or cut short.
     */
    private fun finishedVideos(sinceS: Long): List<Pair<Uri, String>> {
        val collection = MediaStore.Video.Media.EXTERNAL_CONTENT_URI
        val where =
            "${MediaStore.MediaColumns.OWNER_PACKAGE_NAME} = ? AND ${MediaStore.MediaColumns.IS_PENDING} = 0 AND " +
                "${MediaStore.MediaColumns.DATE_ADDED} >= ?"
        val columns = arrayOf(MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME)
        return context.contentResolver
            .query(collection, columns, where, arrayOf(context.packageName, sinceS.toString()), null)
            ?.use { c ->
                buildList { while (c.moveToNext()) add(ContentUris.withAppendedId(collection, c.getLong(0)) to c.getString(1)) }
            }.orEmpty()
    }

    private companion object {
        /** core/output/ExportRange.hpp kMinRangeMs: no render is shorter. */
        const val SHORTEST_RENDER_S = 1.0

        const val MS_PER_S = 1_000L

        /**
         * A ceiling, not a wait: the check returns as soon as the file is saved. Not yet measured on
         * the CI emulator at these settings; set from the first run that finishes.
         */
        const val RENDER_TIMEOUT_MS = 10 * JOURNEY_TIMEOUT_MS
    }
}
