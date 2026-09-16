package dev.geode.ui.glass

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.BookmarkBorder
import androidx.compose.material.icons.outlined.ChatBubbleOutline
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.Cloud
import androidx.compose.material.icons.outlined.Email
import androidx.compose.material.icons.outlined.FavoriteBorder
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.List
import androidx.compose.material.icons.outlined.Lock
import androidx.compose.material.icons.outlined.Mic
import androidx.compose.material.icons.outlined.MoreVert
import androidx.compose.material.icons.outlined.MusicNote
import androidx.compose.material.icons.outlined.Pause
import androidx.compose.material.icons.outlined.Person
import androidx.compose.material.icons.outlined.PhotoCamera
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Share
import androidx.compose.material.icons.outlined.Shield
import androidx.compose.material.icons.outlined.SkipNext
import androidx.compose.material.icons.outlined.SkipPrevious
import androidx.compose.material.icons.outlined.StarBorder
import androidx.compose.material.icons.outlined.VpnKey
import androidx.compose.ui.graphics.vector.ImageVector

/**
 * The outlined glyph set the reference catalogue (ref-01..03) shows. Every one of these already
 * has a matching stock `Icons.Outlined.*` glyph, so this object aliases them rather than drawing
 * new [ImageVector]s: play, pause, previous (skip-previous), next (skip-next), home, search,
 * settings (gear), list, music note, profile (person), close, more (kebab / more-vert), heart
 * (favourite-border), share, bookmark (bookmark-border), mic, camera (photo-camera), cloud, mail
 * (email), lock, key (vpn-key), shield, star (star-border), chat (chat-bubble-outline).
 */
object GlassIcons {
    val Play: ImageVector = Icons.Outlined.PlayArrow
    val Pause: ImageVector = Icons.Outlined.Pause
    val Previous: ImageVector = Icons.Outlined.SkipPrevious
    val Next: ImageVector = Icons.Outlined.SkipNext
    val Home: ImageVector = Icons.Outlined.Home
    val Search: ImageVector = Icons.Outlined.Search
    val Settings: ImageVector = Icons.Outlined.Settings
    val ListIcon: ImageVector = Icons.Outlined.List
    val MusicNote: ImageVector = Icons.Outlined.MusicNote
    val Profile: ImageVector = Icons.Outlined.Person
    val Close: ImageVector = Icons.Outlined.Close
    val More: ImageVector = Icons.Outlined.MoreVert
    val Heart: ImageVector = Icons.Outlined.FavoriteBorder
    val Share: ImageVector = Icons.Outlined.Share
    val Bookmark: ImageVector = Icons.Outlined.BookmarkBorder
    val Mic: ImageVector = Icons.Outlined.Mic
    val Camera: ImageVector = Icons.Outlined.PhotoCamera
    val Cloud: ImageVector = Icons.Outlined.Cloud
    val Mail: ImageVector = Icons.Outlined.Email
    val Lock: ImageVector = Icons.Outlined.Lock
    val Key: ImageVector = Icons.Outlined.VpnKey
    val Shield: ImageVector = Icons.Outlined.Shield
    val Star: ImageVector = Icons.Outlined.StarBorder
    val Chat: ImageVector = Icons.Outlined.ChatBubbleOutline
}
