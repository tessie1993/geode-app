#pragma once

#include <string>
#include <vector>

// The MilkDrop presets bundled with the app, copied into the user's preset folder so they list, edit and
// delete like the user's own. Copied once per pack version (a marker file in the folder says which), and
// never over a file already there: a preset the user deleted stays deleted, one they edited stays theirs.
namespace geode::viz::milk {

inline constexpr int kStarterPackVersion = 1;
inline constexpr const char* kStarterMarker = ".starter-pack";
// The bundled folder, in the APK's assets.
inline constexpr const char* kStarterAssetDir = "milk";
// What a first launch shows, so it never opens on projectM's idle preset.
inline constexpr const char* kFirstRunPreset = "geode_bloom.milk";

// Where the bundled presets are read from: the APK's assets on a phone, a table in a test.
class StarterSource {
public:
    virtual ~StarterSource() = default;
    // The file names in the bundled folder; empty when it cannot be listed.
    virtual std::vector<std::string> names() = 0;
    // One file's bytes; false when it cannot be read.
    virtual bool read(const std::string& name, std::string& bytes) = 0;
};

// Copies the pack's .milk files into dir unless its marker already holds this version, then writes the
// marker (also when the pack cannot be listed, as an empty pack). Returns how many files it wrote; a file
// that fails half way is removed. Not thread-safe: the caller holds one lock around every install.
int installStarterPack(StarterSource& source, const std::string& dir);

// Installs the pack into dir and returns the first-run preset's path there, or "" when it is not there.
std::string firstRunPreset(StarterSource& source, const std::string& dir);

// A regular file (following links) at path.
bool isFile(const std::string& path);

}  // namespace geode::viz::milk
