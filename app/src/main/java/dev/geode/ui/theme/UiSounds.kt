package dev.geode.ui.theme

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import android.provider.Settings
import androidx.annotation.RawRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalContext
import dev.geode.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * The interface sounds: a soft glass tap, a two-note confirm and a short air swoop for
 * changing tabs. Synthesised for this theme (see `tools/ui_sounds.py`), not sampled.
 */
enum class UiSound(
    @param:RawRes val res: Int,
) {
    TAP(R.raw.ui_tap),
    CONFIRM(R.raw.ui_confirm),
    SWOOP(R.raw.ui_swoop),
}

fun interface UiSoundPlayer {
    fun play(sound: UiSound)
}

val LocalUiSounds = staticCompositionLocalOf { UiSoundPlayer { } }

/**
 * Plays [UiSound]s through a [SoundPool] at a level well under the music.
 *
 * Silent unless the user leaves the app's own "Interface sounds" setting on AND the system's
 * touch-sounds setting is on, the same rule the platform applies to its own clicks, so a phone
 * set to silent taps stays silent here too.
 */
private class SoundPoolUiSounds(
    context: Context,
) : UiSoundPlayer {
    private val pool =
        SoundPool
            .Builder()
            .setMaxStreams(MAX_STREAMS)
            .setAudioAttributes(
                AudioAttributes
                    .Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build(),
            ).build()
    private val ids = UiSound.entries.associateWith { pool.load(context, it.res, 1) }

    @Volatile var systemTouchSounds = false

    override fun play(sound: UiSound) {
        if (!systemTouchSounds) return
        val id = ids[sound] ?: return
        pool.play(id, VOLUME, VOLUME, 0, 0, 1f)
    }

    fun release() = pool.release()
}

@Composable
fun rememberUiSounds(enabled: Boolean): UiSoundPlayer {
    if (!enabled) return remember { UiSoundPlayer { } }
    val context = LocalContext.current.applicationContext
    val player = remember(context) { SoundPoolUiSounds(context) }
    LaunchedEffect(player) {
        player.systemTouchSounds =
            withContext(Dispatchers.IO) {
                Settings.System.getInt(context.contentResolver, Settings.System.SOUND_EFFECTS_ENABLED, 1) != 0
            }
    }
    DisposableEffect(player) { onDispose { player.release() } }
    return player
}

private const val MAX_STREAMS = 2
private const val VOLUME = 0.35f
