package dev.geode.playback

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.media3.common.Player
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles

/**
 * Settings › Behaviour › Start on headphones (default off). When wired headphones, a USB headset or
 * a Bluetooth audio device connects while a track is loaded and paused, play it.
 *
 * Android delivers no headset broadcast to a manifest receiver any more, so this only listens while
 * a [PlaybackSession] is alive, which is whenever the app or the playback service holds one. That is
 * also the only time there is a track to resume. The switch is read on every connect rather than
 * cached, so flipping it in Settings takes effect without a restart.
 */
internal class HeadphoneAutoplay(
    context: Context,
    private val player: Player,
) {
    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val behaviour = BehaviourStore(GeodePrefsFiles(context).general)
    private val main = Handler(Looper.getMainLooper())

    /** The first callback lists the devices already connected at registration; only later ones are news. */
    private var primed = false

    private val callback =
        object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
                if (!primed) {
                    primed = true
                    return
                }
                if (addedDevices.none { it.isSink && it.type in HEADPHONE_TYPES }) return
                if (!behaviour.load().startOnHeadphones) return
                if (player.currentMediaItem == null || player.isPlaying) return
                if (player.playbackState == Player.STATE_IDLE) player.prepare()
                player.play()
            }
        }

    init {
        audioManager?.registerAudioDeviceCallback(callback, main)
    }

    fun release() {
        audioManager?.unregisterAudioDeviceCallback(callback)
    }

    private companion object {
        val HEADPHONE_TYPES: Set<Int> =
            buildSet {
                add(AudioDeviceInfo.TYPE_WIRED_HEADSET)
                add(AudioDeviceInfo.TYPE_WIRED_HEADPHONES)
                add(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP)
                add(AudioDeviceInfo.TYPE_USB_HEADSET)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    add(AudioDeviceInfo.TYPE_BLE_HEADSET)
                }
            }
    }
}
