package dev.geode.data

/**
 * Behaviour's start-screen rules for JVM tests, which cannot load libgeode: "last used" opens the last screen,
 * and as a last screen reads as the player. Native's own rules, the notification buttons' too, are
 * core/tests/shell_prefs_test.cpp's to prove; a test that reaches a button rule here fails.
 */
object TestBehaviourRules : BehaviourRules.Reads {
    fun install() = BehaviourRules.install(this)

    override fun buttonsOrdered(chosen: Int): IntArray = throw UnsupportedOperationException("native's (shell_prefs_test)")

    override fun buttonsToggled(
        chosen: Int,
        button: Int,
        on: Boolean,
    ): Int = throw UnsupportedOperationException("native's (shell_prefs_test)")

    override fun lastScreen(last: Int): Int = if (last == StartScreen.LAST_USED.ordinal) StartScreen.PLAYER.ordinal else last

    override fun opensOn(
        start: Int,
        last: Int,
    ): Int = if (start == StartScreen.LAST_USED.ordinal) last else start
}
