package dev.geode.di

import android.app.Application
import dev.geode.ui.PlayerSession
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Holds the one [PlayerSession] every screen shares.
 *
 * The session is deliberately NOT a Hilt-managed singleton binding. Its teardown flushes pending
 * store writes, releases the playback engine's UI hold, stops the foreground service and cancels
 * the scope running the playback poll loop — all of which must happen when the UI goes away, not
 * when the process dies.
 *
 * Exactly one ViewModel owns that moment. `PlayerViewModel` is created unconditionally by
 * `AppRoot`, and every ViewModel here lives in the Activity's store (this app navigates by state,
 * not by `NavHost`, so there are no per-destination stores), which means all of them are cleared
 * together when the Activity finishes for real. So `PlayerViewModel.onCleared` calls [shutdown]
 * and the rest simply stop referring to the session — no holder counting, and no screen able to
 * tear down a session another screen is still reading.
 */
@Singleton
class PlayerSessionProvider
    @Inject
    constructor(
        private val application: Application,
    ) {
        private var instance: PlayerSession? = null

        /** The shared session, built on first use. Safe for any ViewModel to call. */
        @Synchronized
        fun get(): PlayerSession = instance ?: PlayerSession(application).also { instance = it }

        /**
         * Tears the session down and forgets it. Only the owning ViewModel may call this; a later
         * [get] builds a fresh session, which is what a new Activity should get.
         */
        @Synchronized
        fun shutdown() {
            instance?.shutdown()
            instance = null
        }
    }
