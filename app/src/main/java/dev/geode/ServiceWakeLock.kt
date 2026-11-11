package dev.geode

import android.content.Context
import android.os.PowerManager

/**
 * Keeps the CPU awake while a foreground service does work that must not pause: a foreground
 * service keeps the process alive, not the processor, so with the screen off a render or a cast
 * with nothing else playing could be suspended mid-frame. Always held with a time limit, so a
 * service that dies without [release] cannot hold the phone awake for good.
 */
class ServiceWakeLock(
    context: Context,
    tag: String,
) {
    private val lock =
        context.getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "geode:$tag").apply {
            setReferenceCounted(false)
        }

    fun acquire(limitMs: Long) = lock.acquire(limitMs)

    fun release() {
        if (lock.isHeld) lock.release()
    }
}

/** As long as Android lets a media-processing service run (six hours), which bounds a render too. */
const val LONG_WORK_WAKE_MS = 6L * 60 * 60 * 1000
