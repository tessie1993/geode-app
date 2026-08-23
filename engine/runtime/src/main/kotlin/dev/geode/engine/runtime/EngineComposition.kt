package dev.geode.engine.runtime

class EngineComposition {
    private val owned = mutableListOf<EngineLifetime>()
    private var closed = false

    fun <T : EngineLifetime> own(lifetime: T): T {
        check(!closed) { "the composition root is closed; ${lifetime.id} would never be released" }
        owned += lifetime
        return lifetime
    }

    fun closeAll(): List<Pair<LifetimeId, Throwable>> {
        closed = true
        val failures = mutableListOf<Pair<LifetimeId, Throwable>>()
        owned.asReversed().forEach { lifetime ->
            runCatching { lifetime.close() }
                .onFailure { failures += lifetime.id to it }
        }
        owned.clear()
        return failures
    }
}
