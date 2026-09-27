package dev.geode.nav

/**
 * What stands between a person and the shell. At most one is up at a time; the sections and
 * their stacks are untouched underneath, and only a [dismissible] gate answers to back.
 */
enum class Gate(
    val dismissible: Boolean,
) {
    /** The boot animation; ends on its own. */
    BOOT(dismissible = false),

    /** The photosensitivity notice; must be acknowledged. */
    SAFETY(dismissible = false),

    /** First-run setup; ends when it is done. */
    SETUP(dismissible = false),

    /** The guided tour; may be skipped. */
    TUTORIAL(dismissible = true),
}
