package dev.geode.pro

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The one Pro style a free user keeps for sharing the app (the owner's rule: [FreeTier.freeStyleIds],
 * and one more for a share). Once per install: the first style granted stays, later grants are refused.
 * Android cannot tell whether the share was posted, so opening the share sheet is what earns it.
 */
interface ShareBonusRepository {
    /** The kept style, or null while the bonus is unused. */
    fun getStyleStream(): Flow<String?>

    /** Keeps [styleId] when no style was kept yet; false when one already was. */
    fun grant(styleId: String): Boolean
}

/** Kept in memory: tests, and nothing else. */
class InMemoryShareBonusRepository(
    initial: String? = null,
) : ShareBonusRepository {
    private val style = MutableStateFlow(initial)

    override fun getStyleStream(): Flow<String?> = style.asStateFlow()

    @Synchronized
    override fun grant(styleId: String): Boolean {
        if (style.value != null) return false
        style.value = styleId
        return true
    }
}
