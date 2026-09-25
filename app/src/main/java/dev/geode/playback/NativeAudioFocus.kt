package dev.geode.playback

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Handler
import androidx.core.content.ContextCompat

/** Main-thread Android adapter. A generation rejects callbacks from an abandoned request. */
internal class NativeAudioFocus(
    context: Context,
    private val main: Handler,
    private val onChange: (PlaybackFocusState.State) -> Unit,
    private val onNoisy: () -> Unit,
) {
    private val context = context.applicationContext
    private val manager = context.getSystemService(AudioManager::class.java)
    private val policy = PlaybackFocusState()
    private var request: AudioFocusRequest? = null
    private var generation = 0L
    private var registered = false
    private var noisyEnabled = true
    private val receiver =
        object : BroadcastReceiver() {
            override fun onReceive(
                context: Context?,
                intent: Intent?,
            ) {
                if (intent?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY && policy.state.playRequested) onNoisy()
            }
        }

    fun requestPlay(): PlaybackFocusState.State {
        if (request != null && policy.state.playRequested) return policy.state
        val epoch = ++generation
        val focusRequest =
            AudioFocusRequest
                .Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(
                    AudioAttributes
                        .Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build(),
                ).setAcceptsDelayedFocusGain(true)
                .setOnAudioFocusChangeListener({ change ->
                    if (epoch == generation) {
                        val event =
                            when (change) {
                                AudioManager.AUDIOFOCUS_GAIN -> PlaybackFocusState.Change.GAIN
                                AudioManager.AUDIOFOCUS_LOSS -> PlaybackFocusState.Change.LOSS
                                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> PlaybackFocusState.Change.TRANSIENT_LOSS
                                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> PlaybackFocusState.Change.DUCK
                                else -> null
                            }
                        if (event != null) {
                            val state = policy.change(event)
                            if (!state.playRequested) abandon()
                            onChange(state)
                        }
                    }
                }, main)
                .build()
        request = focusRequest
        val result =
            when (manager.requestAudioFocus(focusRequest)) {
                AudioManager.AUDIOFOCUS_REQUEST_GRANTED -> PlaybackFocusState.RequestResult.GRANTED
                AudioManager.AUDIOFOCUS_REQUEST_DELAYED -> PlaybackFocusState.RequestResult.DELAYED
                else -> PlaybackFocusState.RequestResult.DENIED
            }
        val state = policy.request(result)
        if (!state.playRequested) abandon() else updateReceiver()
        return state
    }

    fun setPauseOnNoisy(enabled: Boolean) {
        noisyEnabled = enabled
        updateReceiver()
    }

    fun abandon() {
        generation++
        policy.pause()
        request?.let { manager.abandonAudioFocusRequest(it) }
        request = null
        updateReceiver()
    }

    private fun updateReceiver() {
        val shouldRegister = noisyEnabled && policy.state.playRequested
        if (shouldRegister && !registered) {
            ContextCompat.registerReceiver(
                context,
                receiver,
                IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY),
                ContextCompat.RECEIVER_NOT_EXPORTED,
            )
            registered = true
        } else if (!shouldRegister && registered) {
            context.unregisterReceiver(receiver)
            registered = false
        }
    }
}
