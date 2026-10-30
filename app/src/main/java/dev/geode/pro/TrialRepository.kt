package dev.geode.pro

import kotlinx.coroutines.flow.Flow

/**
 * The one-hour Pro trial: once per install, wall clock, kept on the device.
 *
 * The app has no network, so there is no server to hold the trial; a reinstall or a cleared app
 * gets a fresh one. That is accepted — the trial is a taste of Pro, not a licence.
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
         * A clock earlier than the start time means the clock was moved back after starting;
         * that ends the trial rather than extending it.
         */
        fun at(
            startedAtMs: Long?,
            endNoticeShown: Boolean,
            nowMs: Long,
        ): TrialState {
            if (startedAtMs == null) return Available
            val endsAt = startedAtMs + FreeTier.TRIAL_DURATION_MS
            return if (nowMs in startedAtMs until endsAt) Active(endsAt) else Ended(endNoticeShown)
        }
    }
}
