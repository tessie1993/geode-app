package dev.geode.playback

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioMixerAttributes
import android.os.Build
import androidx.annotation.ChecksSdkIntAtLeast
import androidx.annotation.OptIn
import androidx.annotation.RequiresApi
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import dev.geode.util.bestEffort

/**
 * Settings › Playback › Bit-perfect USB output, done through ExoPlayer's own AudioTrack.
 *
 * Android 14 (API 34) lets a media app ask for a USB device's mixer port to be opened in
 * [AudioMixerAttributes.MIXER_BEHAVIOR_BIT_PERFECT] mode (`AudioManager.setPreferredMixerAttributes`).
 * Media3 has no API for it, so this does the platform half and ExoPlayer does the rest:
 *
 * - The platform only treats a track as bit-perfect when its rate, encoding and channel mask equal
 *   the mixer attributes exactly, and it decides that when the AudioTrack is created. So the choice
 *   follows the stream: [onOutput] is called by `audio/TapRenderersFactory`'s output provider on the
 *   playback thread just before each AudioTrack is built, with that track's own rate, mask and
 *   encoding, and picks the DAC's bit-perfect entry that equals it. A DAC that offers no such entry
 *   gets the preference cleared, and plays mixed. (It used to hear the format from the tap's format
 *   callback, which Media3 runs only after it has opened the first track, so a cold start and every
 *   gapless item after it played mixed: issue #353.)
 * - ExoPlayer is pointed at the DAC with `setPreferredAudioDevice`, so the AudioTrack is routed to
 *   the port that was just configured.
 * - [onActive] tells the native DSP whether the platform took the track bit-perfect. Native then
 *   passes every sample through untouched (`core/audio/dsp/DspProcessor`): no ReplayGain, fades,
 *   crossfade, equalizer, crossfeed or limiter, since any of them would change the samples the DAC
 *   is meant to get as they are in the file. Native latches it at the pipeline flush that follows
 *   each new AudioTrack, so the chain and the platform switch together.
 *
 * While the DAC's port is bit-perfect the platform mutes system sounds on it, and another app's
 * media mutes this one; that is how Android defines the mode, not something to work around.
 */
@OptIn(UnstableApi::class)
class BitPerfectOutput(
    context: Context,
    private val onActive: (Boolean) -> Unit,
) {
    private val manager: AudioManager? = context.applicationContext.getSystemService(AudioManager::class.java)

    @Volatile
    private var enabled = false

    @Volatile
    private var lastRateHz = 0

    @Volatile
    private var lastChannelMask = 0

    @Volatile
    private var lastEncoding = 0

    /**
     * Turns the mode on or off. The AudioTrack already open was built under the old choice, so a
     * playing item is seeked to where it stands: Media3 rebuilds the track there, and the new choice
     * applies from now rather than from the next track (#353's fix direction).
     */
    fun setEnabled(
        on: Boolean,
        player: ExoPlayer,
    ) {
        if (!supported) return
        if (on == enabled) return
        enabled = on
        bestEffort(TAG, "route to USB") {
            player.setPreferredAudioDevice(if (on) usbDevice() else null)
        }
        if (on) {
            if (lastRateHz > 0) onOutput(lastRateHz, lastChannelMask, lastEncoding)
        } else {
            onActive(false)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                bestEffort(TAG, "clear mixer attributes") { clearAll() }
            }
        }
        if (player.playbackState != Player.STATE_IDLE && player.currentMediaItem != null) {
            bestEffort(TAG, "reopen the AudioTrack") { player.seekTo(player.currentPosition) }
        }
    }

    /** The AudioTrack about to be built; called on the playback thread before it exists. */
    fun onOutput(
        sampleRateHz: Int,
        channelMask: Int,
        encoding: Int,
    ) {
        lastRateHz = sampleRateHz
        lastChannelMask = channelMask
        lastEncoding = encoding
        var granted = false
        if (enabled && Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            bestEffort(TAG, "set mixer attributes") { granted = applyOn34(sampleRateHz, channelMask, encoding) }
        }
        onActive(granted)
    }

    /** True when the platform took the DAC's bit-perfect entry for exactly this track's format. */
    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun applyOn34(
        sampleRateHz: Int,
        channelMask: Int,
        encoding: Int,
    ): Boolean {
        val audio = manager ?: return false
        val device = usbDevice() ?: return false
        val match =
            audio.getSupportedMixerAttributes(device).firstOrNull {
                it.mixerBehavior == AudioMixerAttributes.MIXER_BEHAVIOR_BIT_PERFECT &&
                    it.format.sampleRate == sampleRateHz &&
                    it.format.encoding == encoding &&
                    it.format.channelMask == channelMask
            }
        if (match == null) {
            audio.clearPreferredMixerAttributes(MEDIA, device)
            return false
        }
        return audio.setPreferredMixerAttributes(MEDIA, device, match)
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun clearAll() {
        val audio = manager ?: return
        usbDevices().forEach { audio.clearPreferredMixerAttributes(MEDIA, it) }
    }

    private fun usbDevices(): List<AudioDeviceInfo> =
        manager
            ?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            ?.filter { it.type == AudioDeviceInfo.TYPE_USB_DEVICE || it.type == AudioDeviceInfo.TYPE_USB_HEADSET }
            .orEmpty()

    private fun usbDevice(): AudioDeviceInfo? = usbDevices().firstOrNull()

    companion object {
        private const val TAG = "BitPerfectOutput"

        @get:ChecksSdkIntAtLeast(api = Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
        val supported: Boolean get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE

        private val MEDIA: AudioAttributes =
            AudioAttributes
                .Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()
    }
}
