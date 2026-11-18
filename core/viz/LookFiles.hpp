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
// The byte length of the code point at `i` when it is Char.isWhitespace, else 0.
size_t whitespaceAt(std::string_view text, size_t i);

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
// The saved palette id a look keeps on a frame native wrote (a roll, a reset): `lookId` while the frame still
// draws its own palette, "" once it does not, because a saved palette is its overrides.
std::string framePaletteId(std::string_view lookId, const SceneParams& frame);

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

// ---- Preset names ----------------------------------------------------------------------------------------------

// What a built-in preset's name holds between its look and its style ("Glow · Cymatics"); a user's never does.
inline constexpr std::string_view kBuiltInSeparator = " \xc2\xb7 ";
bool isBuiltInPreset(std::string_view name);

// Two names alike but for case, as Kotlin's equals(ignoreCase = true) compares them: each code point as the lower
// case of its upper case (Latin, Greek, Cyrillic, Armenian, full-width Latin; elsewhere exact).
bool sameNameCaseless(std::string_view a, std::string_view b);

// A name as the store keeps it: the separator as " - ", trimmed; `fallback` when nothing is left.
std::string cleanPresetName(std::string_view raw, std::string_view fallback = {});
// A folder's name as the store keeps it: trimmed.
std::string cleanFolderName(std::string_view raw);

// Why a name cannot be used; the shell says it in its own words.
enum class NameProblem : int { None = 0, Blank = 1, Taken = 2 };

// `raw` as the new name of preset `current`: Blank when nothing is left once cleaned, Taken when another name in
// `taken` (built-ins included: a user preset would hide behind one) is the same but for case, since on some file
// systems those are one file. Its own name, any case, is fine.
NameProblem presetRenameProblem(std::string_view raw, std::string_view current, const std::vector<std::string>& taken);
// `raw` as a folder's name, renaming `current` (nullopt for a new folder): Blank, or Taken by another, any case.
NameProblem folderNameProblem(std::string_view raw, const std::optional<std::string>& current,
                              const std::vector<std::string>& folders);

// `base`, or "<base> 2", "<base> 3"... : the first that `taken` does not hold, case ignored.
std::string uniquePresetName(std::string_view base, const std::vector<std::string>& taken);
// A duplicate's name: uniquePresetName of "<name> copy".
std::string presetCopyName(std::string_view name, const std::vector<std::string>& taken);
// An imported preset's name: cleaned (`fallback` when blank), then " 2", " 3"... while `taken` holds it exactly.
std::string importedPresetName(std::string_view raw, std::string_view fallback, const std::vector<std::string>& taken);

// The user preset in `names` a save under `raw` overwrites (same name once cleaned, exactly), or nullopt.
std::optional<std::string> presetReplaceTarget(std::string_view raw, const std::vector<std::string>& names);
// A save under `raw` names a built-in in `names`, case ignored: refused, as the copy would pass for the built-in.
bool builtInNameClash(std::string_view raw, const std::vector<std::string>& names);
// A built-in preset of style `presetSceneId` is made for `activeSceneId`: the same style, or the shelf the catalogue
// files its looks under (every Fluid or Cymatics look shares them).
bool presetMatchesScene(const std::string& presetSceneId, const std::string& activeSceneId);

// ---- Share links -----------------------------------------------------------------------------------------------

// A preset whole in one link: this prefix, then its JSON gzipped and in URL-safe base64 (the shell's step). Lengths
// count UTF-16 units, as the shell's text does.
inline constexpr std::string_view kPresetLinkPrefix = "geode://preset/";
// No link longer is minted (a file is shared instead) or accepted, so a hostile one cannot spend the inflate budget.
inline constexpr int kMaxLinkLength = 8000;
inline constexpr int kMaxLinkPayload = 1024 * 1024;
// The most bytes a payload may inflate to.
inline constexpr int kMaxLinkInflated = 4 * 1024 * 1024;

std::string presetLink(std::string_view payload);
bool presetLinkFits(std::string_view link);
// `text`, trimmed, starts with the prefix, case ignored.
bool isPresetLink(std::string_view text);
// The payload of the link `text` is (trimmed; a '#' fragment or '?' query cut off), or nullopt when it is no link or
// too long.
std::optional<std::string> presetLinkPayload(std::string_view text);
// The first link inside pasted text, up to the first whitespace, or nullopt.
std::optional<std::string> findPresetLink(std::string_view text);
// An inflated payload is a preset's text only when not blank.
bool presetLinkBodyAccepted(std::string_view body);

// ---- The template library ------------------------------------------------------------------------------------

// A template's file in the library: its name's stem (at most 60 bytes, trimmed, "Template" when blank), "--", its
// id's tag (A-Z a-z 0-9 '_' '-' kept, anything else '_', at most 40 bytes) and `suffix` (the template format's).
// The id is in the name so a template is found without reading every file.
std::string templateFileName(std::string_view name, std::string_view id, std::string_view suffix);
// What every file of template `id` ends with: "--", its id's tag and `suffix`.
std::string templateFileTail(std::string_view id, std::string_view suffix);

// The name a newly added template takes: `base` trimmed ("Template" when blank), or, when a template in `taken`
// already has it (ASCII letters caseless), the first free "<name> 2", "<name> 3"... A replaced template keeps its own.
std::string uniqueTemplateName(std::string_view base, const std::vector<std::string>& taken);

// The bundled starter set, each a whole template file in the template format (written once here, so it always
// reads and matches the current model). Using one copies it into the library under a fresh id, so editing a copy
// never changes what a starter means. starter(i) is "" past the end.
int starterCount();
std::string_view starter(int index);
bool isStarter(std::string_view id);

}  // namespace geode::viz::files
