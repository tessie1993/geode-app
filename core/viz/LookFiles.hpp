#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "viz/Params.hpp"

// The files the shell keeps a user's looks in: saved palettes (files/palettes/<id>.json), presets
// (files/presets/**/<name>.json, also a geode://preset link's body and the saved live look) and the template
// library's names. The shell keeps only bytes and folders: it reads a file, hands the text here and writes back
// what comes out. Every rule a file's name, its JSON, its defaults and its migration follow is here, in the text
// org.json wrote before (old files read as they always did). Pure; host-tested in core/tests/look_files_test.cpp.
namespace geode::viz::files {

// ---- Names ---------------------------------------------------------------------------------------------------

// Kotlin's String.trim() and isBlank(): Char.isWhitespace at the ends (ASCII controls 9..13 and 28..31, the space
// separators, U+2028 and U+2029), on UTF-8 text.
std::string trimmed(std::string_view text);
bool blank(std::string_view text);

// Every code point but A-Z a-z 0-9 '-' '_' ' ' as '_': a preset folder's name.
std::string safeChars(std::string_view name);

// A name as a file stem: every code point but A-Z a-z 0-9 '-' '_' ' ' becomes '_'. A name that changed, or a blank
// one, also gets '-' and the first four bytes of the SHA-256 of its UTF-8 in hex, so two names that sanitize alike
// stay two files and a blank name never resolves to the folder itself (deleting it once removed every project).
std::string safeFileName(std::string_view name);

// The indices of `names` in list order: by name, caseless (ASCII letters folded) or by code point, ties in their
// given order.
std::vector<int> order(const std::vector<std::string>& names, bool caseless);

// ---- Saved palettes ------------------------------------------------------------------------------------------

// Your colours (packed 0xRRGGBB, at most five) or, for one saved before the colour picker, a hue gradient.
struct SavedPalette {
    std::string id;
    std::string name;
    float baseHue = 0.0f;
    float hueSpan = 1.0f;
    std::vector<int> stops;
};

// A palette's id, and its file's stem: safeFileName(name), trimmed, "palette" when nothing is left.
std::string paletteIdFor(std::string_view name);

// The id from the id (or the name when the id is blank), the name trimmed ("Palette" when empty), the hues held
// to 0..1 (NaN as 0) and only real colours kept, five at most.
SavedPalette sanitized(SavedPalette p);

// A palette file as org.json read it: "name" required (any JSON value as its text), "id" optional (blank: the
// name's id), baseHue 0 and hueSpan 1 when missing or unreadable (NaN too), held to 0..1, "stops" an array of
// ints whose real colours are kept, five at most. nullopt when the text is no object or has no name.
std::optional<SavedPalette> readPalette(std::string_view text);

// The file text, as org.json's toString(2) wrote it: id, name, baseHue, hueSpan, stops.
std::string writePalette(const SavedPalette& p);

// The palette made the look's own: its colours when it has any, else its gradient (viz/ParamsPalette).
SceneParams applyPalette(const SceneParams& params, const SavedPalette& p);

// The saved palette id a look keeps once palette `deletedId` is deleted: "" if it was that one.
std::string keptPaletteId(std::string_view lookId, std::string_view deletedId);

// A palette file migrated to its current id rule: the text it should hold, under the stem paletteIdFor(name).
std::optional<std::string> migratedPalette(std::string_view text);

// ---- Presets -------------------------------------------------------------------------------------------------

// The envelope ranges the Customize sliders offer (attack 5 ms to 1 s, decay 10 ms to 1.5 s). Every preset the app
// saves sits inside them; a shared link can say anything.
inline constexpr float kAttackMin = 0.005f;
inline constexpr float kAttackMax = 1.0f;
inline constexpr float kDecayMin = 0.01f;
inline constexpr float kDecayMax = 1.5f;

// `raw` narrowed to a float as Java does, or `fallback` when that is not finite (NaN, an infinity, or 1e39, a finite
// double that overflows a float): a link anything can send must not put one into the renderer or the writer.
float finiteOr(double raw, float fallback);
// finiteOr with the range's low end as the fallback, held to lo..hi.
float finiteIn(double raw, float lo, float hi);

// A preset (files/presets/**/<name>.json), a geode://preset link's body, or the saved live look.
struct Preset {
    std::string name;
    std::string sceneId;
    float attack = 0.0f;
    float decay = 0.0f;
    std::optional<std::string> customShader;
    std::optional<std::string> milkPreset;
    SceneParams params;
    std::string customPaletteId;
};

// As org.json read it: "name", "sceneId", "attack" and "decay" required (any value as its text or number; a retired
// style id opens on the style that replaced it), the envelope through finiteIn, "customShader" and "milkPreset" when
// present, and the params object's keys beside them (viz/ParamsJson, which reads old looks as they were). nullopt
// when the text is no object or misses a required key.
std::optional<Preset> readPreset(std::string_view text);

// The file text, as org.json's toString(2) wrote it: name, sceneId, attack, decay, the params object's keys, then
// customShader and milkPreset when set.
std::string writePreset(const Preset& p);

// The MilkDrop file a preset mirrors its .milk source to: its name's stem (a ".milk" suffix dropped) plus ".milk".
std::string milkFileName(std::string_view presetName);

}  // namespace geode::viz::files
