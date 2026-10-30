package dev.geode.ui.theme

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.InsertDriveFile
import androidx.compose.material.icons.automirrored.rounded.Notes
import androidx.compose.material.icons.automirrored.rounded.QueueMusic
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.AutoAwesome
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Fullscreen
import androidx.compose.material.icons.rounded.LibraryMusic
import androidx.compose.material.icons.rounded.Mic
import androidx.compose.material.icons.rounded.Movie
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Repeat
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material.icons.rounded.Shuffle
import androidx.compose.material.icons.rounded.SkipNext
import androidx.compose.material.icons.rounded.SkipPrevious
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector

/** The app's icon set: names the screens use, mapped onto Material Rounded symbols. */
enum class StoneIcon(
    val vector: ImageVector,
) {
    ADD(Icons.Rounded.Add),
    CHECK(Icons.Rounded.Check),
    CLOSE(Icons.Rounded.Close),
    DELETE(Icons.Rounded.Delete),
    EDIT(Icons.Rounded.Edit),
    FAVORITE(Icons.Rounded.Favorite),
    FILE(Icons.AutoMirrored.Rounded.InsertDriveFile),
    FOLDER(Icons.Rounded.Folder),
    FULLSCREEN(Icons.Rounded.Fullscreen),
    LIBRARY(Icons.Rounded.LibraryMusic),
    LYRICS(Icons.AutoMirrored.Rounded.Notes),
    MICROPHONE(Icons.Rounded.Mic),
    NEXT(Icons.Rounded.SkipNext),
    PAUSE(Icons.Rounded.Pause),
    PLAY(Icons.Rounded.PlayArrow),
    PREVIOUS(Icons.Rounded.SkipPrevious),
    QUEUE(Icons.AutoMirrored.Rounded.QueueMusic),
    REPEAT(Icons.Rounded.Repeat),
    SEARCH(Icons.Rounded.Search),
    SETTINGS(Icons.Rounded.Settings),
    SHARE(Icons.Rounded.Share),
    SHUFFLE(Icons.Rounded.Shuffle),
    STUDIO(Icons.Rounded.Movie),
    VISUALIZER(Icons.Rounded.AutoAwesome),
}

@Composable
fun StoneIconArt(
    icon: StoneIcon,
    contentDescription: String?,
    modifier: Modifier = Modifier,
    tint: Color = LocalContentColor.current,
) {
    Icon(icon.vector, contentDescription, modifier, tint = tint)
}
