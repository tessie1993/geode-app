package dev.geode.cast

import android.content.Context
import android.os.Handler
import android.os.Looper
import com.google.android.gms.cast.CastMediaControlIntent
import com.google.android.gms.cast.framework.CastOptions
import com.google.android.gms.cast.framework.OptionsProvider
import com.google.android.gms.cast.framework.SessionProvider
import com.google.android.gms.cast.framework.media.CastMediaOptions

/**
 * Cast's options: Google's Default Media Receiver, remote-to-local transfers on (the system's
 * output switcher), and the Cast SDK's own media notification and media session off. The app
 * already has both: [CastService]'s "Casting to your TV" notification, and the music's own
 * session, which stays on the phone. The SDK's copies would add a second notification (and a
 * second foreground service) and a second session fighting the first for the media buttons.
 *
 * It also makes [CastController] exist the moment Cast starts in this process. Cast starts either
 * from the Cast button or when the output switcher hands the app a session through
 * `MediaTransferReceiver` (owner: a cast started there must work); in the second case no screen
 * has built the controller, and without it nobody would check Pro or start the stream. The
 * controller is built on the next main-thread turn, because building it touches Cast, which is
 * still starting up here.
 */
class CastOptionsProvider : OptionsProvider {
    override fun getCastOptions(context: Context): CastOptions {
        val app = context.applicationContext
        Handler(Looper.getMainLooper()).post { CastController.get(app) }
        val media =
            CastMediaOptions
                .Builder()
                .setNotificationOptions(null)
                .setMediaSessionEnabled(false)
                .build()
        return CastOptions
            .Builder()
            .setReceiverApplicationId(CastMediaControlIntent.DEFAULT_MEDIA_RECEIVER_APPLICATION_ID)
            .setRemoteToLocalEnabled(true)
            .setCastMediaOptions(media)
            .build()
    }

    override fun getAdditionalSessionProviders(context: Context): List<SessionProvider>? = null
}
