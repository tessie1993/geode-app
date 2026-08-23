package dev.geode.engine.runtime

enum class LifetimeId {
    PROCESS,

    PLAYBACK_SESSION,

    VISUAL_SESSION,

    GL_CONTEXT,

    OUTPUT,

    EXPORT,
}

enum class LifetimePhase { IDLE, RUNNING, CLOSED }

interface EngineLifetime {
    val id: LifetimeId
    val phase: LifetimePhase

    fun start()

    fun reset()

    fun close()
}

abstract class ManagedLifetime(
    final override val id: LifetimeId,
) : EngineLifetime {
    final override var phase: LifetimePhase = LifetimePhase.IDLE
        private set

    final override fun start() {
        check(phase != LifetimePhase.CLOSED) { "$id was closed and cannot be started again" }
        if (phase == LifetimePhase.RUNNING) return
        try {
            onStart()
        } catch (t: Throwable) {
            phase = LifetimePhase.CLOSED
            throw t
        }
        phase = LifetimePhase.RUNNING
    }

    final override fun reset() {
        check(phase != LifetimePhase.CLOSED) { "$id was closed and cannot be reset" }
        if (phase == LifetimePhase.RUNNING) onReset()
    }

    final override fun close() {
        if (phase == LifetimePhase.CLOSED) return
        val acquired = phase == LifetimePhase.RUNNING
        phase = LifetimePhase.CLOSED
        if (acquired) onClose()
    }

    protected open fun onStart() = Unit

    protected open fun onReset() = Unit

    protected open fun onClose() = Unit
}
