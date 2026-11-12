package dev.geode.ui

/**
 * Every row Settings draws, by name: one setting, or a block of settings that always travel
 * together (the ten gestures' map, the sound engine's tuning, Look's text colour and size). Each
 * is placed exactly once, in one tab's [SettingsGroupKey.basics] or one [SettingsSectionKey.rows],
 * and `SettingsMapTest` holds that, so a regroup can never drop a row or show one twice.
 *
 * A row names where it is drawn, never what it stores: its pref key stays with its store
 * (`PlayerPrefs`, `BehaviourPrefs`, `GeneralSettings` and the rest), so moving a row between tabs changes
 * no stored value and no backup.
 */
enum class SettingsRowKey {
    // Playback
    CROSSFADE,
    FADE,
    AUTO_RESUME,
    SLEEP_TIMER,
    QUEUE_END,
    REMEMBER_SHUFFLE,
    SPEED_PITCH,
    REPLAYGAIN,
    SKIP_SILENCE,
    RESUME_LONG_TRACKS,

    // Sound & equalizer
    EQUALIZER,

    // Output & devices
    PAUSE_UNPLUGGED,
    HEADPHONES_AUTOPLAY,
    PICTURE_DELAY,
    NATIVE_OUTPUT,
    BIT_PERFECT,
    NOTIFICATION_BUTTONS,

    // Library: the music folders and the short-track filter are one block
    MUSIC_FOLDERS,
    LYRICS,
    PRESET_FOLDER,

    // Visual playback
    AUTO_DIRECTOR,
    STYLE_TIMER,
    PRESET_MORPH,

    // Sound reaction
    LIVE_INPUT,
    OTHER_APPS_AUDIO,
    NOTE_RESPONSE,
    SOUND_TUNING,

    // Photosensitivity & motion
    FLASH_LIMITS,
    REDUCED_MOTION,

    // Displays & casting
    KEEP_SCREEN_ON_IN_VISUALS,
    KEEP_SCREEN_ON_WITH_VISUAL,
    CONNECTED_DISPLAY,
    PIP_AUTO_ENTER,

    // Wallpaper & screen saver
    LIVE_WALLPAPER,
    DREAM_BRIGHT,
    DREAM_CLOCK,
    DREAM_NOW_PLAYING,
    DREAM_OTHER_APPS,

    // Gestures & controls
    GESTURE_MAP,
    TOUCH_SMEAR,
    TOUCH_TRANSFORM,

    // Look: the accent and interface sounds, text colour and size, and the layout are blocks
    START_SCREEN,
    LOOK_THEME,
    LOOK_TEXT,
    LOOK_LAYOUT,

    // Performance
    PERFORMANCE_MODE,
    PERFORMANCE_DETAILS,

    // Export & recording
    OPEN_STUDIO_RENDER,
    KEEP_RENDERS,
    RECORD_QUALITY,

    // Preferences: the watermark's placement and Pro's switch are one block, as are Library's
    // opening tab, order and tap, the Player's buttons and the visual's icons
    WATERMARK,
    LIBRARY_PREFS,
    PLAYER_PREFS,
    STAGE_PREFS,

    // Backup, help & about
    BACKUP,
    TUTORIALS,
    HELP_TOPICS,
    ABOUT,
}
