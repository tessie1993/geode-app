package dev.geode.playback

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioMixerAttributes
import android.os.Build
import androidx.annotation.RequiresApi

/**
 * Asks Android to route media to a USB DAC without the mixer touching it. Only a device that
 * advertises a bit-perfect mixer path takes the request; anything else is left as it was.
 */
object BitPerfectOutput {
    val supported: Boolean get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE

    fun apply(
        context: Context,
        enabled: Boolean,
    ): Boolean = if (supported) applyOn34(context, enabled) else false

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun applyOn34(
        context: Context,
        enabled: Boolean,
    ): Boolean {
        val manager = context.getSystemService(AudioManager::class.java) ?: return false
        val attributes =
            AudioAttributes
                .Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()
        val usb =
            manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).filter {
                it.type == AudioDeviceInfo.TYPE_USB_DEVICE || it.type == AudioDeviceInfo.TYPE_USB_HEADSET
            }
        if (!enabled) {
            usb.forEach { manager.clearPreferredMixerAttributes(attributes, it) }
            return true
        }
        return usb.any { device ->
            val bitPerfect =
                manager.getSupportedMixerAttributes(device).firstOrNull {
                    it.mixerBehavior == AudioMixerAttributes.MIXER_BEHAVIOR_BIT_PERFECT
                } ?: return@any false
            manager.setPreferredMixerAttributes(attributes, device, bitPerfect)
        }
    }
}
