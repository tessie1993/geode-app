package dev.geode.stage

/**
 * A surface controller that may own a retained [Stage] for one Activity/window generation.
 *
 * The Stage keeps only a weak reference so an old Activity cannot keep its surface alive. Implementations
 * must clear that reference when they release or are replaced.
 */
internal interface StageSurfaceOwner
