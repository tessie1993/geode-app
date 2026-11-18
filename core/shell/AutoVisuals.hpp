#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Auto's remembered shape, ported from Kotlin data/AutoVisualsPrefsStore.kt: the timed style change (what it
// picks from and its interval) and the visual playlist, read back from what the prefs file holds. The shell
// reads the raw values out of SharedPreferences (an absent key stays nullopt) and writes them back; what they
// mean is decided here: the playlist's JSON, each scene id resolved to today's style id, the playlist off
// when it has no entries, and the intervals held to native's range.
//
// The interval range is geode::viz::auto_plan::clampInterval (core/viz/AutoPlan.hpp, kIntervalMinS ..
// kIntervalMaxS), used here as it stands. Native AutoPlan already owns when the playlist steps, the timed
// draw and the pool; what this module still decides is the reading of the saved values. It should merge into
// AutoPlan's file as `auto_plan::readSaved` (a pure function over these same structs) the next time core/viz
// is open for edits; nothing here depends on that. Host-tested in core/tests/shell_prefs_test.cpp.
namespace geode::shell {

// One visual playlist entry (VizPlaylistEntry).
struct PlaylistEntry {
    std::string sceneId;
    std::optional<std::string> presetName;
    std::optional<std::string> milkPath;
    std::string label;
};

// The fields of the viz state Auto remembers (the rest of VizUiState is the shell's).
struct AutoVisualsState {
    bool randomEnabled = false;
    int randomIntervalSec = 0;
    bool randomIncludeStyles = false;
    bool randomIncludePresets = false;
    bool randomIncludeMilk = false;
    std::vector<PlaylistEntry> playlist;
    bool playlistEnabled = false;
    int playlistIntervalSec = 0;
};

// What the prefs file holds, each nullopt where its key is absent.
struct AutoVisualsStored {
    std::optional<int> randomIntervalSec;
    std::optional<bool> randomIncludeStyles;
    std::optional<bool> randomIncludePresets;
    std::optional<bool> randomIncludeMilk;
    std::optional<bool> playlistEnabled;
    std::optional<int> playlistIntervalSec;
    std::optional<std::string> entriesJson;
};

// A scene id's current style id (the catalogue's resolve: geode_catalogue_resolve).
using StyleResolver = std::function<std::string(const std::string&)>;

// The state with the stored values read over it (AutoVisualsPrefsStore.applyTo): the timer always starts off
// (a timer is something the listener starts on purpose, for this session); an absent value keeps the state's;
// a stored playlist text replaces the playlist (unreadable text is an empty playlist); the playlist is on only
// when it is stored on (or was on) and has entries; both intervals are held to native's range.
AutoVisualsState autoVisualsApply(const AutoVisualsState& state, const AutoVisualsStored& stored, const StyleResolver& resolve);

// The playlist as the prefs keep it: a JSON array of {sceneId, label[, presetName][, milkPath]}, as org.json
// wrote it (AutoVisualsPrefsStore.entriesToJson).
std::string autoVisualsEntriesToJson(const std::vector<PlaylistEntry>& entries);

// The entries a saved text holds (AutoVisualsPrefsStore.entriesFromJson): read as org.json reads, the first
// array in the text; an element that is not an object, or has no scene id, is skipped; the scene id is
// resolved, an empty preset name or milk path is none, an empty label is the raw scene id. Text that is not
// an array is no entries.
std::vector<PlaylistEntry> autoVisualsEntriesFromJson(const std::string& json, const StyleResolver& resolve);

}  // namespace geode::shell
