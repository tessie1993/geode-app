package dev.geode.pro

import kotlinx.coroutines.flow.Flow

/**
 * The one-hour Pro trial: once per install, wall clock, kept on the device.
 *
 * The app has no backend, so there is no server to hold the trial; a cleared app, or a reinstall
 * Android's backup does not restore, gets a fresh one. That is accepted — the trial is a taste of
 * Pro, not a licence. What is defended is the clock: see [TrialState.at].
 */
interface TrialRepository {
    fun getTrialStream(): Flow<TrialState>

    /** Starts the trial if it has never run. A second call does nothing. */
    suspend fun startTrial()

    /** Records that the "your trial has ended" paywall was shown, so it shows once. */
    suspend fun markEndNoticeShown()
}

sealed interface TrialState {
    /** Never started. */
    data object Available : TrialState

    data class Active(
        val endsAtMs: Long,
    ) : TrialState

    /** Used up. [endNoticeShown] is false until the trial-ended paywall has appeared once. */
    data class Ended(
        val endNoticeShown: Boolean,
    ) : TrialState

    companion object {
        /**
         * The trial's state at [nowMs] for a trial started at [startedAtMs] (null: never started).
         *
         * [lastSeenMs] is the latest wall-clock time the trial was ever read at (0: never). A clock
         * earlier than the start time, or more than [FreeTier.TRIAL_CLOCK_SLACK_MS] behind
         * [lastSeenMs], was moved back after starting; that ends the trial rather than extending
         * it. Inside the slack (a network time correction) the trial reads at [lastSeenMs], so a
         * small step back gains nothing either.
         */
        fun at(
            startedAtMs: Long?,
            endNoticeShown: Boolean,
            nowMs: Long,
            lastSeenMs: Long = 0L,
        ): TrialState {
            if (startedAtMs == null) return Available
            val endsAt = startedAtMs + FreeTier.TRIAL_DURATION_MS
            val movedBack = lastSeenMs - nowMs > FreeTier.TRIAL_CLOCK_SLACK_MS
            val now = maxOf(nowMs, lastSeenMs)
            return if (!movedBack && now in startedAtMs until endsAt) Active(endsAt) else Ended(endNoticeShown)
        }

        /**
         * What to keep as the next [at]'s `lastSeenMs` after reading [state] at [nowMs]: never
         * earlier than before, and at least the trial's end once it has ended, so no clock brings
         * an ended trial back.
         */
        fun lastSeenAfter(
            state: TrialState,
            startedAtMs: Long,
            nowMs: Long,
            lastSeenMs: Long,
        ): Long {
            val seen = maxOf(nowMs, lastSeenMs)
            return if (state is Ended) maxOf(seen, startedAtMs + FreeTier.TRIAL_DURATION_MS) else seen
        }
    }
}
