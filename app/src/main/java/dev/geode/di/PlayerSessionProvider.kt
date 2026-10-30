package dev.geode.di

import android.app.Application
import dev.geode.pro.FeatureGate
import dev.geode.pro.FreeTier
import dev.geode.pro.GetFeatureGateUseCase
import dev.geode.pro.PaywallNavigator
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.PlayerSession
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
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
        getFeatureGate: GetFeatureGateUseCase,
        private val paywall: PaywallNavigator,
    ) {
        private var instance: PlayerSession? = null

        /**
         * The latest entitlement, kept current for the session's synchronous style checks. Starts
         * closed: the first emission (the cached entitlement) lands within a frame, and a Pro user
         * cannot pick a style before the picker has even drawn.
         */
        @Volatile
        private var gate: FeatureGate = FeatureGate.FREE

        /** False until the first real entitlement arrives; nothing is downgraded before that. */
        @Volatile
        private var gateKnown = false

        init {
            CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate).launch {
                getFeatureGate().collect {
                    gate = it
                    gateKnown = true
                    instance?.let(::leaveLockedStyle)
                }
            }
        }

        /**
         * A style the user may no longer keep (the trial ran out, a refund) gives way to the first
         * free one, rather than staying on screen as a free premium style.
         */
        private fun leaveLockedStyle(session: PlayerSession) {
            if (!gateKnown) return
            val current = session.visualizerRepository.viz.value.sceneId
            if (!gate.allowsStyle(current)) session.selectScene(FreeTier.FALLBACK_STYLE_ID)
        }

        /** The shared session, built on first use. Safe for any ViewModel to call. */
        @Synchronized
        fun get(): PlayerSession =
            instance ?: PlayerSession(application).also { session ->
                session.allowsStyle = { gate.allowsStyle(it) }
                session.onStyleLocked = { paywall.open(PaywallTrigger.Feature(ProFeature.PREMIUM_STYLES)) }
                instance = session
                leaveLockedStyle(session)
            }

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
