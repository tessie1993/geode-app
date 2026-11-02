package dev.geode.cast

import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.annotation.OptIn
import androidx.media3.cast.DefaultCastOptionsProvider
import androidx.media3.common.util.UnstableApi
import com.google.android.gms.cast.framework.CastOptions
import com.google.android.gms.cast.framework.OptionsProvider
import com.google.android.gms.cast.framework.SessionProvider

/**
 * Media3's default options (Google's Default Media Receiver, remote-to-local transfers on), plus
 * one thing: the moment Cast starts up in this process, [CastController] exists.
 *
 * Cast starts up either from the Cast button or when the system's output switcher hands the app a
 * session through `MediaTransferReceiver` (owner: a cast started from the switcher must work). In
 * the second case no screen has built the controller, and without it nobody would check Pro or
 * start the stream. Cast only reads this class when it starts, so nothing about Cast runs in a
 * process that never shows the Cast button or receives a session.
 *
 * The controller is built on the next main-thread turn, not here: building it touches Cast, which
 * is still in the middle of starting up.
 */
@OptIn(UnstableApi::class)
class CastOptionsProvider : OptionsProvider {
    private val defaults = DefaultCastOptionsProvider()

    override fun getCastOptions(context: Context): CastOptions {
        val app = context.applicationContext
        Handler(Looper.getMainLooper()).post { CastController.get(app) }
        return defaults.getCastOptions(context)
    }

    override fun getAdditionalSessionProviders(context: Context): List<SessionProvider>? = defaults.getAdditionalSessionProviders(context)
}
