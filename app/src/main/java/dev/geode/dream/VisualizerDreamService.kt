package dev.geode.dream

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.service.dreams.DreamService
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import androidx.core.content.ContextCompat
import dev.geode.R
import dev.geode.billing.ProSnapshot
import dev.geode.billing.WallClock
import dev.geode.data.LookStore
import dev.geode.data.PerformanceStore
import dev.geode.engine.bridge.SoundNative
import dev.geode.engine.bridge.StageNative
import dev.geode.geodeContainer
import dev.geode.pro.PaywallIntent
import dev.geode.pro.ProFeature
import dev.geode.stage.StageView
import dev.geode.util.bestEffort

/**
 * The visualizer as a screensaver (Daydream): the app's stage in a [StageView] on the dream's window,
 * with the time and what is playing over it ([DreamOverlay], each part a Settings › Screen saver toggle).
 *
 * It runs by the native surface rule ([DreamConditions], `core/viz/SurfaceRun`), made for a screen
 * that sits there for hours while nobody may be looking:
 * - covered ([DreamCover]: the proximity sensor reads near, face down or in a pocket): no frames;
 * - battery saver: a still frame;
 * - silence ([SoundSource]'s grace): no visual, faded to black under the overlay, until sound comes
 *   back (owner: no sound, no visuals; #999);
 * - reduced motion (the system's Remove animations, [SystemAnimations]): a
 * still frame for the whole dream (owner; #389). Off, the dream keeps its gentle flight.
 * It paces to its own fps choice, Behaviour › Dream ([dev.geode.data.BehaviourPrefs.dreamFps]),
 * not to Settings › Performance's cap, which is the live visual's.
 * A dream has no Settings page of its own (`res/xml/dream_info.xml`), so battery saver and silence
 * always rest it ([DreamConditions]). It hears the app's ring while BeatRealms feeds it, else, when
 * Settings › Screen saver says so ([dev.geode.data.BehaviourPrefs.dreamHearOtherApps]) and the
 * permission the output-mix capture needs is granted, any app's audio.
 */
open class VisualizerDreamService : DreamService() {
    private var visualizerView: StageView? = null
    private var overlay: DreamOverlay? = null

    private val handler = Handler(Looper.getMainLooper())
    private val powerManager by lazy { getSystemService(PowerManager::class.java) }

    /** Other apps' sound, Behaviour's choice, read at attach. */
    private var hearOtherApps = false

    private val sound = SoundSource(this) { syncRunState() }

    /** Lazy: a service has no context for its prefs until onCreate. */
    private val generalPrefs by lazy { geodeContainer.prefsFiles.general }

    /** Behaviour's dream frame rate, read at attach; [applyPerformance] restates it after the cap. */
    private var dreamFps = SoundNative.SURFACE_DEFAULT_FPS

    /**
     * Settings › Performance, followed live: the app shares this process and
     * the prefs file, so a change (a backup restored mid-dream, say) reaches the running dream.
     * A field: listeners are held weakly.
     */
    private val performanceChanged =
        SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key in PerformanceStore.KEYS) applyPerformance()
        }

    /** Lazy: it asks for the sensor service, which a service has no context for until onCreate. */
    private val cover by lazy { DreamCover(this) { syncRunState() } }

    /** Lazy for the same reason: it reads the application context. A flip restates at once. */
    private val systemAnimations by lazy { SystemAnimations(this) { syncRunState() } }

    /** Between onDreamingStarted and onDreamingStopped: the only time anyone can see it. */
    private var dreaming = false
    private var listening = false

    /** Black over the visual while the run rule says [DreamRunMode.DARK]; the overlay stays above. */
    private var darkCover: View? = null
    private var darkShown = false

    private val powerReceiver =
        object : BroadcastReceiver() {
            override fun onReceive(
                context: Context,
                intent: Intent,
            ) = syncRunState()
        }
    private var receiverRegistered = false

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        // Pro feature: without it the dream says so for a moment, then ends and the system falls back
        // to its own. A tap on the notice opens the app on the paywall.
        if (!entitled()) {
            showLockedNotice()
            return
        }
        isInteractive = false
        isFullscreen = true
        val prefsFiles = geodeContainer.prefsFiles
        // Its own switch (Settings › Screen saver), no longer Player's keep-screen-on.
        val behaviour = geodeContainer.behaviour.current()
        isScreenBright = behaviour.dreamScreenBright
        hearOtherApps = behaviour.dreamHearOtherApps
        val view = StageView(this, StageNative.STAGE_KIND_SURFACE, SoundNative.SURFACE_DEFAULT_FPS.toFloat())
        visualizerView = view
        // The app's saved look in the gentle flight (owner, wave 1): the camera keeps flying without big
        // moves, because a dream is watched from across a room.
        val looks = LookStore(prefsFiles, contentResolver)
        looks.pushProcessWide()
        view.stage.look = looks.current().gentleFlight()
        dreamFps = behaviour.dreamFps
        applyPerformance()
        view.stage.soundInput = { sound.input }
        val stage = FrameLayout(this)
        stage.addView(view, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        // Above the stage's view (a SurfaceView's hole would clear anything below it), under the overlay.
        val black = View(this).apply { setBackgroundColor(android.graphics.Color.BLACK) }
        black.alpha = 0f
        darkCover = black
        stage.addView(black, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        if (behaviour.dreamShowClock || behaviour.dreamShowNowPlaying) {
            val textScale = geodeContainer.general.current().textScale
            val dreamOverlay = DreamOverlay(this, behaviour.dreamShowClock, behaviour.dreamShowNowPlaying, textScale)
            overlay = dreamOverlay
            stage.addView(
                dreamOverlay,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.CENTER,
                ),
            )
        }
        setContentView(stage)
        ContextCompat.registerReceiver(
            this,
            powerReceiver,
            IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        receiverRegistered = true
        systemAnimations.start()
        generalPrefs.registerOnSharedPreferenceChangeListener(performanceChanged)
    }

    /**
     * Settings › Performance's quality on the dream's stage, and the dream's own frame rate, snapped
     * natively: Performance's fps cap is the live visual's, and the dream paces to its own choice.
     */
    private fun applyPerformance() {
        val stage = visualizerView?.stage ?: return
        stage.performance = PerformanceStore(generalPrefs, this).settings.value
        stage.capFps = SoundNative.surfaceFpsChoice(dreamFps).toFloat()
    }

    override fun onDreamingStarted() {
        super.onDreamingStarted()
        dreaming = true
        cover.start()
        overlay?.start()
        syncRunState()
    }

    override fun onDreamingStopped() {
        dreaming = false
        cover.stop()
        overlay?.stop()
        syncRunState()
        super.onDreamingStopped()
    }

    /** Restates when a running trial ends; the native rule says when (#376). */
    private val trialEnded = Runnable { syncRunState() }

    /** The entitlement as the native rule reads it: Pro forever, a trial its end, free 0. */
    private fun animateUntilMs(): Long = ProSnapshot.animateUntilMs(this, ProFeature.SCREENSAVER)

    private fun entitled(): Boolean = SoundNative.surfaceEntitled(animateUntilMs(), WallClock.SYSTEM.nowMs())

    /** What a free user's screensaver shows instead of the visual: why it is still, and the way to Pro. */
    private fun showLockedNotice() {
        isInteractive = true
        isFullscreen = true
        val notice =
            TextView(this).apply {
                text = getString(R.string.pro_chip_screensaver)
                setTextColor(Color.WHITE)
                setBackgroundColor(Color.BLACK)
                textSize = LOCKED_NOTICE_TEXT_SP
                gravity = Gravity.CENTER
                val padding = (LOCKED_NOTICE_PADDING_DP * resources.displayMetrics.density).toInt()
                setPadding(padding, 0, padding, 0)
                setOnClickListener {
                    PaywallIntent.forFeature(this@VisualizerDreamService, ProFeature.SCREENSAVER)?.let(::startActivity)
                    wakeUp()
                }
            }
        setContentView(notice)
        handler.postDelayed(endLockedNotice, LOCKED_NOTICE_MS)
    }

    private val endLockedNotice = Runnable { finish() }

    override fun onDetachedFromWindow() {
        handler.removeCallbacks(endLockedNotice)
        generalPrefs.unregisterOnSharedPreferenceChangeListener(performanceChanged)
        handler.removeCallbacks(trialEnded)
        sound.stop()
        cover.stop()
        systemAnimations.stop()
        overlay?.stop()
        if (receiverRegistered) {
            bestEffort(TAG, "unregister power receiver") { unregisterReceiver(powerReceiver) }
            receiverRegistered = false
        }
        // The stage outlives its view, so the dream that made it ends it.
        visualizerView?.stage?.close()
        sound.release()
        (visualizerView?.parent as? ViewGroup)?.removeView(visualizerView)
        visualizerView = null
        overlay = null
        darkCover?.animate()?.cancel()
        darkCover = null
        super.onDetachedFromWindow()
    }

    /**
     * Restates what the dream sees to its stage: on dreaming, cover,
     * battery saver, silence and Remove animations. The stage applies the native run rule (its
     * pacing and a still frame's settle) and answers the mode, for the black cover.
     */
    private fun syncRunState() {
        val view = visualizerView ?: return
        val now = WallClock.SYSTEM.nowMs()
        val conditions =
            DreamConditions(
                visible = dreaming,
                covered = cover.covered,
                batterySaver = powerManager?.isPowerSaveMode == true,
                silent = sound.silent,
                reducedMotion = systemAnimations.removed,
                animateUntilMs = animateUntilMs(),
            )
        // A trial that ends mid-dream ends the dream, the same as a free user starting one
        // (owner, #376), and Android falls back to its own.
        if (!conditions.entitled(now)) {
            finish()
            return
        }
        handler.removeCallbacks(trialEnded)
        conditions.recheckInMs(now)?.let { handler.postDelayed(trialEnded, it) }
        // The native rule says when to listen: whenever the dream would move with sound, cover
        // aside, so lifting the phone for a second does not restart the silence clock.
        syncListening(conditions.listening(now))
        val runMode = DreamRunMode.entries[view.stage.setSurface(conditions.flags, conditions.animateUntilMs, now)]
        showDark(runMode == DreamRunMode.DARK)
    }

    /**
     * Fades the black over the visual in or out on a change only. The platform's long animation time
     * is scaled by the system's animator duration scale, so Remove animations makes it a cut.
     */
    private fun showDark(dark: Boolean) {
        if (dark == darkShown) return
        darkShown = dark
        darkCover
            ?.animate()
            ?.alpha(if (dark) 1f else 0f)
            ?.setDuration(resources.getInteger(android.R.integer.config_longAnimTime).toLong())
    }

    /** A start counts as fresh sound, so [sound] restarts only on a real change. */
    private fun syncListening(listen: Boolean) {
        if (listen == listening) return
        listening = listen
        if (listen) sound.start(hearOtherApps) else sound.stop()
    }

    private companion object {
        /** How long a free user's screensaver shows [showLockedNotice] before it ends. */
        const val LOCKED_NOTICE_MS = 10_000L
        const val LOCKED_NOTICE_TEXT_SP = 20f
        const val LOCKED_NOTICE_PADDING_DP = 48

        const val TAG = "VisualizerDream"
    }
}
