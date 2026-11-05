package dev.geode.ui

import android.app.SearchManager
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R

/**
 * Settings › Lyrics: the playing song's lyrics (timed ones follow the music, a tap on a line
 * seeks there), a web search for them, and a way to load an .lrc file for this song. They used to
 * be a panel of the fullscreen live view, which is now for watching.
 *
 * The search hands the query to the browser; the app itself has no network access and stays
 * that way. What the user saves from it comes back through "Load .lrc", which keeps a copy for
 * this track ([LyricsLoader.attach]).
 */
@Composable
internal fun LyricsSection(viewModel: PlayerViewModel) {
    val context = LocalContext.current
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    val lyrics by viewModel.lyrics.collectAsStateWithLifecycle()
    // Not delegated: only the lyrics panel reads .value, to follow the sung line.
    val position = viewModel.positionMs.collectAsStateWithLifecycle()
    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) viewModel.attachLyrics(uri)
        }
    val song = listOfNotNull(state.title, state.artist?.takeIf { it.isNotBlank() }).joinToString(" ")
    val query = stringResource(R.string.lyrics_search_query, song)
    val noBrowser = stringResource(R.string.lyrics_no_browser)
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            if (!state.hasMedia) {
                Text(
                    stringResource(R.string.lyrics_nothing_playing),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                return@Column
            }
            Text(song, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.onSurface)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CrystalButton(onClick = { if (!context.searchWeb(query)) Toast.makeText(context, noBrowser, Toast.LENGTH_SHORT).show() }) {
                    Text(stringResource(R.string.lyrics_search_web))
                }
                // */* rather than text/plain: most file managers type .lrc as nothing at all.
                CrystalButton(filled = false, onClick = { picker.launch(arrayOf("*/*")) }) {
                    Text(stringResource(R.string.lyrics_load_lrc))
                }
            }
            Text(
                stringResource(R.string.lyrics_search_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (state.hasMedia) {
            LyricsPanel(
                lyrics = lyrics,
                positionMs = { position.value },
                onSeek = viewModel::seekToMs,
                // A fixed height: the panel is a lazy list, and the Settings page around it scrolls.
                modifier = Modifier.height(LYRICS_PANEL_HEIGHT).fillMaxWidth(),
            )
        }
    }
}

private val LYRICS_PANEL_HEIGHT = 360.dp

/** A web search in whatever handles one, else the browser; false when nothing can. */
private fun Context.searchWeb(query: String): Boolean {
    val search = Intent(Intent.ACTION_WEB_SEARCH).putExtra(SearchManager.QUERY, query)
    val browse = Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://www.google.com/search?q=" + android.net.Uri.encode(query)))
    return listOf(search, browse).any { intent ->
        try {
            startActivity(intent)
            true
        } catch (_: ActivityNotFoundException) {
            false
        }
    }
}
