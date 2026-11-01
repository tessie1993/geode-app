package dev.geode.playback

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioMixerAttributes
import android.os.Build
import androidx.annotation.ChecksSdkIntAtLeast
import androidx.annotation.OptIn
import androidx.annotation.RequiresApi
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
 *   follows the stream: [onFormat] is called from the tap's format callback, which Media3 runs while
 *   it configures the sink, before the new AudioTrack exists, and it picks the DAC's bit-perfect entry
 *   for that rate, 16-bit (the chain never gets float output, see `docs/AUDIO_CHAIN.md`) and that
 *   channel layout. A DAC that offers no such entry gets the preference cleared, and plays mixed.
 * - ExoPlayer is pointed at the DAC with `setPreferredAudioDevice`, so the AudioTrack is routed to
 *   the port that was just configured.
 *
 * Sonic only resamples when the pitch is changed, and ReplayGain and the equalizer change the
 * samples, not the format, so the format still matches; whether the result is "bit-perfect" with the
 * equalizer on is the user's call, as the explainer says.
 *
 * While the DAC's port is bit-perfect the platform mutes system sounds on it, and another app's
 * media mutes this one; that is how Android defines the mode, not something to work around.
 */
@OptIn(UnstableApi::class)
class BitPerfectOutput(
    context: Context,
) {
    private val manager: AudioManager? = context.applicationContext.getSystemService(AudioManager::class.java)

    @Volatile
    private var enabled = false

    @Volatile
    private var lastRateHz = 0

    @Volatile
    private var lastChannels = 0

    /** Turns the mode on or off. Applies from the next AudioTrack, so from the next track or seek. */
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
            if (lastRateHz > 0) onFormat(lastRateHz, lastChannels)
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            bestEffort(TAG, "clear mixer attributes") { clearAll() }
        }
    }

    /** The stream the sink is about to open; called on the playback thread before its AudioTrack exists. */
    fun onFormat(
        sampleRateHz: Int,
        channelCount: Int,
    ) {
        lastRateHz = sampleRateHz
        lastChannels = channelCount
        if (!enabled || Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return
        bestEffort(TAG, "set mixer attributes") { applyOn34(sampleRateHz, channelCount) }
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun applyOn34(
        sampleRateHz: Int,
        channelCount: Int,
    ) {
        val audio = manager ?: return
        val device = usbDevice() ?: return
        val mask = channelMaskFor(channelCount)
        val match =
            audio.getSupportedMixerAttributes(device).firstOrNull {
                it.mixerBehavior == AudioMixerAttributes.MIXER_BEHAVIOR_BIT_PERFECT &&
                    it.format.sampleRate == sampleRateHz &&
                    it.format.encoding == AudioFormat.ENCODING_PCM_16BIT &&
                    it.format.channelMask == mask
            }
        if (match == null) {
            audio.clearPreferredMixerAttributes(MEDIA, device)
        } else {
            audio.setPreferredMixerAttributes(MEDIA, device, match)
        }
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

        /** The output mask the sink builds for a 16-bit stream of [channelCount] channels. */
        fun channelMaskFor(channelCount: Int): Int =
            when (channelCount) {
                1 -> AudioFormat.CHANNEL_OUT_MONO
                2 -> AudioFormat.CHANNEL_OUT_STEREO
                else -> AudioFormat.CHANNEL_INVALID
            }
    }
}
