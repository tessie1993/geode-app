package dev.geode.data.prefs

import dev.geode.audio.AudioFxController
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.billing.CachedEntitlement
import dev.geode.billing.PrefsEntitlementStore
import dev.geode.billing.PrefsTrialStore
import dev.geode.data.AutoDirectorStore
import dev.geode.data.AutoVisualsPrefsStore
import dev.geode.data.BackgroundPrefs
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.BehaviourPrefs
import dev.geode.data.BehaviourStore
import dev.geode.data.BookmarkStore
import dev.geode.data.BootAnimationStore
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination
import dev.geode.data.ExportPrefsStore
import dev.geode.data.FavouritesStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.GuiPrefs
import dev.geode.data.LfoStore
import dev.geode.data.LibraryPrefsStore
import dev.geode.data.LookStore
import dev.geode.data.MEDIA_ROOTS_PREF_KEYS
import dev.geode.data.MacroStore
import dev.geode.data.NoteResponseStore
import dev.geode.data.PerformanceStore
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsStore
import dev.geode.data.RecordPrefs
import dev.geode.data.RecordPrefsStore
import dev.geode.data.RecordSaveStore
import dev.geode.data.SaveTarget
import dev.geode.data.StudioPrefsStore
import dev.geode.data.StyleShelfStore
import dev.geode.data.ThemeStore
import dev.geode.data.VizUiState
import dev.geode.data.backup.BackupFormat
import dev.geode.data.backup.BackupSection
import dev.geode.engine.audio.SoundFrameLayout
import dev.geode.engine.audio.SoundTuning
import dev.geode.export.ExportQuality
import dev.geode.model.GestureTip
import dev.geode.model.TourProgress
import dev.geode.model.TourSpot
import dev.geode.render.MilkPresetMemory
import dev.geode.render.scene.SceneParams
import dev.geode.ui.ModulationController
import dev.geode.wallpaper.WallpaperPrefsStore
import dev.geode.wallpaper.WallpaperSettings
import dev.geode.widget.WidgetState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The net under the settings redo (#1009): every key the app keeps in SharedPreferences is listed
 * once, by the store that owns it, with a type and a backup class, and the stores, the lists and
 * the manual backup cannot drift apart unnoticed. The lists state what the app does today; where
 * that is known to be wrong, the key is in [misclassified], which later PRs shrink.
 */
class PrefsSchemaTest {
    /** One owner of keys: its source file, its list, and how to make it write every key (null: see [sourcePinned]). */
    private class Owner(
        val source: String,
        val keys: List<PrefKey>,
        val drive: ((PrefsContext) -> Unit)? = null,
    )

    private val general = GeodePrefsFiles.GENERAL
    private val player = GeodePrefsFiles.PLAYER

    private val owners =
        listOf(
            Owner("$DATA/ThemeStore.kt", ThemeStore.PREF_KEYS) {
                val tuning = SoundTuning.fromFloats(FloatArray(SoundFrameLayout.TUNING_FLOATS))
                val tour = TourProgress(resume = TourSpot.entries.first(), tips = setOf(GestureTip.SWIPE_STYLES))
                ThemeStore(it[general]).saveGui(
                    GuiPrefs(soundTuning = tuning, presetMirrorUri = "content://presets", fontColorArgb = 1, tourProgress = tour),
                )
            },
            Owner("$DATA/BehaviourPrefs.kt", BehaviourStore.PREF_KEYS) { BehaviourStore(it[general]).save(BehaviourPrefs()) },
            Owner("$DATA/ExportPrefs.kt", ExportPrefsStore.PREF_KEYS) {
                val store = ExportPrefsStore(it[general])
                store.save(ExportDefaults())
                ExportDestination.entries.forEach { destination -> store.saveFor(destination, ExportDefaults()) }
            },
            Owner("$DATA/StudioPrefsStore.kt", StudioPrefsStore.PREF_KEYS) {
                StudioPrefsStore(it[general]).saveLastTab("RENDER")
                StudioPrefsStore(it[general]).saveKeepAsProject(true)
            },
            Owner("$DATA/BootAnimationStore.kt", BootAnimationStore.PREF_KEYS) { BootAnimationStore(it[general]).save(false) },
            Owner("$DATA/MacroStore.kt", MacroStore.PREF_KEYS) { MacroStore.get(it).commit() },
            Owner("$DATA/LfoStore.kt", LfoStore.PREF_KEYS) {
                val store = LfoStore(it[general])
                store.save(store.load())
                store.saveAdsrs(store.loadAdsrs())
                store.saveReactions(emptyList())
            },
            Owner("$DATA/AutoDirectorStore.kt", AutoDirectorStore.PREF_KEYS) {
                AutoDirectorStore.get(it).setEnabled(false)
                AutoDirectorStore.get(it).setReach(AutoDirectorStore.DEFAULT_REACH)
            },
            Owner("$DATA/NoteResponseStore.kt", NoteResponseStore.PREF_KEYS) { NoteResponseStore(it[general]) {}.setEnabled(false) },
            Owner("$DATA/AutoVisualsPrefsStore.kt", AutoVisualsPrefsStore.PREF_KEYS) {
                AutoVisualsPrefsStore(it[GeodePrefsFiles.VIZ]).save(VizUiState())
            },
            Owner("$DATA/StyleShelfStore.kt", StyleShelfStore.PREF_KEYS) { StyleShelfStore.get(it).toggleFavourite("plasma") },
            Owner("engine/scenes/src/main/kotlin/dev/geode/render/MilkPresetMemory.kt", GeodePrefsFiles.MILK_PRESET_PREF_KEYS) {
                MilkPresetMemory.shared(it).loaded("/milk/a.milk")
            },
            Owner("$DATA/PlayerPrefs.kt", PlayerPrefsStore.PREF_KEYS + BookmarkStore.PREF_KEYS) {
                PlayerPrefsStore(it[player]).save(PlayerPrefs())
                BookmarkStore(it[player]).note("content://track", BookmarkStore.LONG_TRACK_MS, BookmarkStore.LONG_TRACK_MS * 2)
            },
            Owner("$APP/audio/AudioFxController.kt", AudioFxController.PREF_KEYS) {
                val fx = AudioFxController(it[GeodePrefsFiles.AUDIO_FX], emptyList(), NativeDspProcessor())
                fx.setEnabled(true)
                fx.setBandLevel(0, 0)
                fx.setBassBoost(1)
                fx.setLoudness(1)
            },
            Owner("$DATA/FavouritesStore.kt", FavouritesStore.PREF_KEYS) {
                FavouritesStore(it[GeodePrefsFiles.FAVOURITES]).toggle("content://track")
            },
            Owner("$DATA/LibraryPrefsStore.kt", LibraryPrefsStore.PREF_KEYS) {
                LibraryPrefsStore(it[GeodePrefsFiles.LIBRARY]).setMinTrackSeconds(10)
            },
            Owner("$DATA/BackgroundPrefsStore.kt", BackgroundPrefsStore.PREF_KEYS) {
                BackgroundPrefsStore(it[GeodePrefsFiles.BACKGROUND]).save(BackgroundPrefs(uri = "content://image"))
            },
            Owner("$APP/ui/ModulationController.kt", ModulationController.PREF_KEYS) {
                val host =
                    object : ModulationController.Host {
                        override val params = SceneParams.DEFAULT
                        override val sceneId = "plasma"

                        override fun setSceneParams(params: SceneParams) = Unit
                    }
                ModulationController(LfoStore(it[general]), it[GeodePrefsFiles.MODULATION], host).toggleParamLock("speed")
            },
            Owner("$DATA/RecordPrefs.kt", RecordPrefsStore.PREF_KEYS) {
                val record = RecordPrefs(ExportQuality.entries.first(), fps = 30, maxLengthSec = 60)
                RecordPrefsStore(it[general]) { seconds -> seconds }.save(record)
            },
            Owner("$DATA/RecordSaveStore.kt", RecordSaveStore.PREF_KEYS) { RecordSaveStore(it).choose(SaveTarget.VideosLibrary) },
            Owner("$APP/wallpaper/WallpaperPrefs.kt", WallpaperPrefsStore.PREF_KEYS) { WallpaperPrefsStore(it).save(WallpaperSettings()) },
            Owner("$APP/widget/WidgetState.kt", WidgetState.PREF_KEYS) {
                WidgetState.save(it, WidgetState(artPath = "/cache/art.png", styleId = "plasma"))
            },
            Owner("$APP/billing/EntitlementStore.kt", PrefsEntitlementStore.PREF_KEYS) {
                PrefsEntitlementStore(it[PrefsEntitlementStore.PREFS_FILE]).save(CachedEntitlement.NONE)
            },
            Owner("$APP/billing/StoredTrialRepository.kt", PrefsTrialStore.PREF_KEYS) {
                PrefsTrialStore(it[PrefsEntitlementStore.PREFS_FILE]).saveStartedAt(1L)
                PrefsTrialStore(it[PrefsEntitlementStore.PREFS_FILE]).saveEndNoticeShown()
                PrefsTrialStore(it[PrefsEntitlementStore.PREFS_FILE]).saveLastSeen(1L)
            },
            Owner("$DATA/PerformanceStore.kt", PerformanceStore.PREF_KEYS),
            Owner("$DATA/LookStore.kt", LookStore.PREF_KEYS),
            Owner("$DATA/MusicLibraryRepository.kt", MEDIA_ROOTS_PREF_KEYS),
        )

    /**
     * Owners no JVM test can run, so their writes are not proven here, only that the source still
     * spells each key. Shrinks as each moves behind a codec (#1009, PRs 5 and 6).
     */
    private val sourcePinned =
        mapOf(
            "$DATA/PerformanceStore.kt" to "its first load and its fallback read the native tier table",
            "$DATA/LookStore.kt" to "VizStateStore writes these, and builds on the native bundled presets",
            "$DATA/MusicLibraryRepository.kt" to "needs an Application, MediaStore and a folder scan",
        )

    /**
     * Files that hold or open a SharedPreferences and own no key: they pass a file on, listen to
     * one, or copy whole files (the backup).
     */
    private val passThrough =
        setOf(
            "$DATA/GeodePrefsFiles.kt",
            "$DATA/VizStateStore.kt",
            "$DATA/FavouritesRepository.kt",
            "$DATA/UserDataRepository.kt",
            "$DATA/backup/BackupRepository.kt",
            "$APP/di/DataModule.kt",
            "$APP/cast/CastStream.kt",
            "$APP/playback/NotificationButtons.kt",
        )

    /**
     * Keys whose class today is known to be wrong, with the class they should have. The list
     * states the defect; fixing a key's class (#1009, PRs 7 and 8) must remove it from here.
     */
    private val misclassified: Map<Pair<String, String>, BackupClass> =
        buildMap {
            // State the app remembers, yet a restore carries it to another phone.
            put(general to "behaviour_last_screen", BackupClass.STATE)
            put(general to "studio_last_tab", BackupClass.STATE)
            put(general to "studio_keep_as_project", BackupClass.STATE)
            // Retired stand-ins for gui_setup_done and gui_safety_acknowledged, which are device-local.
            put(general to "gui_intent", BackupClass.STATE)
            put(general to "gui_safety_choice", BackupClass.STATE)
            // A picked folder's tree URI: its grant belongs to the install it was picked on.
            ExportPrefsStore.PREF_KEYS.filter { it.name.endsWith("export_save_to") }.forEach { put(general to it.name, BackupClass.DEVICE) }
            // Equalizer band levels (owner decision 9 of #1009).
            put(GeodePrefsFiles.AUDIO_FX to "band_levels", BackupClass.DEVICE)
            // The wallpaper's options are the user's choices, but no backup section holds their file.
            WallpaperPrefsStore.PREF_KEYS.forEach { put(it.file to it.name, BackupClass.PORTABLE) }
        }

    private val allKeys = owners.flatMap { it.keys }

    @Test
    fun everyStoreWritesExactlyItsDeclaredKeysWithTheirTypes() {
        for (owner in owners) {
            val drive = owner.drive ?: continue
            val context = PrefsContext().also(drive)
            val live = owner.keys.filterNot { it.retired }
            val written = context.files.flatMap { (file, prefs) -> prefs.written.map { (key, value) -> Triple(file, key, value) } }
            for ((file, key, value) in written) {
                val declared = live.firstOrNull { it.file == file && it.matches(key) }
                assertTrue("${owner.source} wrote undeclared $file/$key", declared != null)
                assertEquals("${owner.source} $file/$key", declared?.type, typeOf(value))
            }
            val unwritten = live.filter { key -> written.none { (file, name, _) -> key.file == file && key.matches(name) } }
            assertEquals("${owner.source} never wrote", emptyList<String>(), unwritten.map { it.name })
        }
    }

    @Test
    fun theOwnersThatCannotRunStillSpellTheirKeys() {
        assertEquals(sourcePinned.keys, owners.filter { it.drive == null }.map { it.source }.toSet())
        for (owner in owners.filter { it.drive == null }) {
            val text = File(root, owner.source).readText()
            for (key in owner.keys) assertTrue("${owner.source} no longer names ${key.name}", "\"${key.name}\"" in text)
        }
    }

    @Test
    fun noKeyIsDeclaredTwice() {
        val twice = allKeys.groupBy { it.file to it.name }.filterValues { it.size > 1 }.keys
        assertEquals(emptySet<Pair<String, String>>(), twice)
        for (prefix in allKeys.filter { it.prefix }) {
            val swallowed = allKeys.filter { it !== prefix && it.file == prefix.file && prefix.matches(it.name) }
            assertEquals("${prefix.name} covers other keys", emptyList<PrefKey>(), swallowed)
        }
    }

    @Test
    fun everyBackupClassIsWhatTheManualBackupDoesToday() {
        for (key in allKeys) {
            val restored = BackupFormat.sectionOfKey(key.file, key.name) != null
            assertEquals("${key.file}/${key.name} is ${key.backup}", restored, key.backup == BackupClass.PORTABLE)
        }
    }

    @Test
    fun theBackupNamesOnlyDeclaredKeysAndFiles() {
        val declared = allKeys.map { it.file to it.name }.toSet()
        val named =
            (BackupFormat.deviceLocalKeys.entries + BackupSection.entries.flatMap { it.borrowedKeys.entries })
                .flatMap { (file, keys) -> keys.map { file to it } }
        assertEquals(emptyList<Pair<String, String>>(), named.filterNot { it in declared })
        val files = allKeys.map { it.file }.toSet()
        assertEquals(emptyList<String>(), BackupSection.entries.flatMap { it.prefsFiles }.filterNot { it in files })
    }

    @Test
    fun theOlderKeySetsStillMatchTheSchema() {
        val (macroRetired, macroLive) = MacroStore.PREF_KEYS.partition { it.retired }
        assertEquals(MacroStore.KEYS, macroLive.map { it.name }.toSet())
        assertEquals(MacroStore.RETIRED_KEYS, macroRetired.map { it.name }.toSet())
        assertEquals(RecordPrefsStore.KEYS, RecordPrefsStore.PREF_KEYS.map { it.name }.toSet())
        assertEquals(LfoStore.KEYS, LfoStore.PREF_KEYS.map { it.name }.toSet())
        assertEquals(PerformanceStore.KEYS, PerformanceStore.PREF_KEYS.map { it.name }.toSet())
    }

    @Test
    fun aMisclassifiedKeyLeavesTheListWhenItsClassIsFixed() {
        for ((id, intended) in misclassified) {
            val key = allKeys.firstOrNull { it.file to it.name == id }
            assertTrue("$id is not a declared key", key != null)
            assertNotEquals("$id is fixed: remove it from the list", intended, key?.backup)
        }
    }

    @Test
    fun everyFileThatTouchesPrefsIsAnOwnerOrAPassThrough() {
        val touches = Regex("""import android\.content\.SharedPreferences|getSharedPreferences\(""")
        val found =
            listOf("app/src/main", "engine")
                .flatMap { File(root, it).walkTopDown().filter { f -> f.extension == "kt" && "/src/main/" in f.invariantSeparatorsPath } }
                .filter { touches.containsMatchIn(it.readText()) }
                .map { it.relativeTo(root).invariantSeparatorsPath }
                .toSet()
        // LookStore reaches its file through GeodePrefsFiles, so it is an owner the scan does not see.
        assertEquals(owners.map { it.source }.toSet() + passThrough - "$DATA/LookStore.kt", found)
    }

    private fun typeOf(value: Any): PrefType? =
        when (value) {
            is Boolean -> PrefType.BOOLEAN
            is Int -> PrefType.INT
            is Long -> PrefType.LONG
            is Float -> PrefType.FLOAT
            is String -> PrefType.STRING
            is Set<*> -> PrefType.STRING_SET
            else -> null
        }

    private companion object {
        const val APP = "app/src/main/java/dev/geode"
        const val DATA = "$APP/data"

        // Unit tests run with the module directory as the working directory.
        val root: File = listOf(File("."), File("..")).first { File(it, "app/src/main").isDirectory }
    }
}
