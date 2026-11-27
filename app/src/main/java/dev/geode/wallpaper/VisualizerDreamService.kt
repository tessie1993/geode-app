package dev.geode.wallpaper

/**
 * The screensaver's published component name, kept when the live wallpaper went and the dream moved to
 * `dev.geode.dream`. Android stores the user's chosen screensaver by component name, and an exported
 * service's name must not change once published (the manifest's `<service android:name>` docs), or every
 * phone that picked BeatRealms falls back to its default screensaver on update. Everything lives in
 * [dev.geode.dream.VisualizerDreamService]; this name is all that stays here.
 */
class VisualizerDreamService : dev.geode.dream.VisualizerDreamService()
