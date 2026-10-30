package dev.geode.journey

import android.content.ComponentName
import android.content.Context
import android.net.Uri
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.media3.session.SessionToken
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import dev.geode.playback.PlaybackService
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.PI
import kotlin.math.sin

/**
 * A short sine tone written as a 16-bit PCM WAV into the app's cache, so playback journeys need
 * neither a bundled song nor storage permission. The app ships no demo track (res/raw holds only
 * interface sounds), and anything from MediaStore would make the test depend on the device.
 */
object TestTrack {
    private const val RATE = 44_100
    private const val TONE_HZ = 440.0
    private const val HEADER_BYTES = 44

    fun write(seconds: Double = 3.0): Uri {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val frames = (RATE * seconds).toInt()
        val data = frames * 2
        val buf = ByteBuffer.allocate(HEADER_BYTES + data).order(ByteOrder.LITTLE_ENDIAN)
        buf.put("RIFF".toByteArray()).putInt(36 + data).put("WAVE".toByteArray())
        buf.put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(1)
        buf.putInt(RATE).putInt(RATE * 2).putShort(2).putShort(16)
        buf.put("data".toByteArray()).putInt(data)
        repeat(frames) { i -> buf.putShort((sin(2 * PI * TONE_HZ * i / RATE) * 8_000).toInt().toShort()) }
        val file = File(context.cacheDir, "journey-tone.wav")
        file.writeBytes(buf.array())
        return Uri.fromFile(file)
    }

    fun mediaItem(uri: Uri): MediaItem =
        MediaItem
            .Builder()
            .setMediaId(uri.toString())
            .setUri(uri)
            .setMediaMetadata(MediaMetadata.Builder().setTitle("Journey tone").build())
            .build()
}

/** Runs [block] on the main thread and returns its value; MediaController is main-looper bound. */
fun <T> onMain(block: () -> T): T {
    var result: Result<T>? = null
    InstrumentationRegistry.getInstrumentation().runOnMainSync { result = runCatching(block) }
    return result!!.getOrThrow()
}

/** Polls [condition] on the main thread until it holds, failing after [timeoutMs]. */
fun awaitMain(
    what: String,
    timeoutMs: Long = JOURNEY_TIMEOUT_MS,
    condition: () -> Boolean,
) {
    val deadline = System.currentTimeMillis() + timeoutMs
    while (!onMain(condition)) {
        check(System.currentTimeMillis() < deadline) { "Timed out waiting for $what" }
        Thread.sleep(POLL_MS)
    }
}

private const val POLL_MS = 50L

/** Connects a MediaBrowser to [PlaybackService], as Android Auto or a Bluetooth head unit would. */
fun connectBrowser(): MediaBrowser {
    val context = ApplicationProvider.getApplicationContext<Context>()
    val token = SessionToken(context, ComponentName(context, PlaybackService::class.java))
    val future = onMain { MediaBrowser.Builder(context, token).buildAsync() }
    return future.get(JOURNEY_TIMEOUT_MS, TimeUnit.MILLISECONDS)
}

/**
 * Latches once the player reports it is playing, from a listener rather than a poll: a poll can
 * miss a short track's whole playing window, a listener cannot. Call on the test thread.
 */
fun MediaBrowser.latchPlaying(): AtomicBoolean {
    val played = AtomicBoolean(false)
    onMain {
        addListener(
            object : Player.Listener {
                override fun onIsPlayingChanged(isPlaying: Boolean) {
                    if (isPlaying) played.set(true)
                }
            },
        )
    }
    return played
}

/**
 * Stops and empties the process-wide player before letting go of the browser, so nothing keeps
 * playing into the next test even where Orchestrator isn't in use (a run from the IDE).
 */
fun MediaBrowser.stopAndRelease() =
    onMain {
        stop()
        clearMediaItems()
        release()
    }
