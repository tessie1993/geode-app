#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// MilkDrop textures: which picked files the texture store takes and the names it keeps them under, the preset
// that shows one, what a preset's samplers ask for and which stored texture answers each name. The shell reads
// documents, writes, deletes and links files; every rule is here.
//
// Names are UTF-8. Case and letters follow what the Kotlin shell did with java.lang.String (lowercase(),
// equals(ignoreCase), isLetterOrDigit, Android's \w): exact for ASCII, a table for the common scripts beyond it.
namespace geode::viz::milk {

// Comfortably above any real MilkDrop texture, far below what exhausts the heap.
inline constexpr int64_t kTextureMaxBytes = 16LL * 1024 * 1024;
// The engine decodes a texture whole, to RGBA, on the GL thread: 4096 px a side is 64 MB decoded.
inline constexpr int kTextureMaxSide = 4096;

// After the last '.', lowercased; "" when there is none.
std::string extensionOf(const std::string& name);
// Before the last '.'; the whole name when there is none.
std::string stemOf(const std::string& name);

// The texture store's extensions: png, jpg, jpeg, bmp, tga, dds, dib.
bool isStoreTexture(const std::string& name);
// Worth reading: a store extension and no more than kTextureMaxBytes declared (0 when the provider did not say).
bool admitsTexture(const std::string& name, int64_t declaredBytes);
bool fitsTexture(int64_t width, int64_t height);
// The bytes are the image the extension claims: a DDS or TGA header that fits, else the width and height the
// shell's bounds decode read (-1 when it could not).
bool isTextureImage(const std::string& name, const uint8_t* bytes, size_t length, int width, int height);
// The name a texture is stored under: non-alphanumerics become '_', a leading digit gets a 't', and a changed
// stem carries '_' plus eight hex characters of the original stem's SHA-256 so two names never collide.
std::string storedTextureName(const std::string& name);
// The stored name for a picked file, or "" when it is refused.
std::string acceptedTextureName(const std::string& name, const uint8_t* bytes, size_t length, int width, int height);

// The generated preset that shows a texture, its file name and its source.
std::string showPresetName(const std::string& texture);
std::string showPresetText(const std::string& texture);
// The generated presets to delete once texture is removed, given the textures that remain: its own, then the
// older stem-named one while no remaining texture shares that stem.
std::vector<std::string> removedPresetNames(const std::string& texture, const std::vector<std::string>& remaining);

// The texture names a preset's samplers reference (`sampler_foo`, `sampler_fw_foo`), first spelling of each,
// without the built-in samplers and `randNN`.
std::vector<std::string> samplerReferences(const std::string& presetText);
// Lowercased, letters and digits only.
std::string normalized(const std::string& name);
// The stem without storedTextureName's `_` + eight hex suffix.
std::string withoutRenameSuffix(const std::string& stem);
// Names in the store's order: by their lowercase, ties as given.
std::vector<std::string> textureOrder(std::vector<std::string> names);

// The order matters: the shell's MilkTextureLinkKind has the same.
enum class LinkKind { Matched = 0, Renamed = 1, Overridden = 2, Substituted = 3, Missing = 4 };

struct Link {
    std::string expected;
    std::string texture;  // "" when Missing
    LinkKind kind = LinkKind::Missing;
    // The file names in the preset's link folder that point at texture: as expected, and lowercased.
    std::vector<std::string> linkNames;
};

// overrides: lowercased expected name -> stored file name the person chose.
using Overrides = std::map<std::string, std::string>;

// One expected name against the stored textures (in textureOrder): a live override, an exact name, the name
// with the store's rename undone, else the closest-named stand-in.
Link resolve(const std::string& expected, const std::vector<std::string>& stored, const Overrides& overrides);
std::vector<Link> resolveAll(const std::string& presetText, const std::vector<std::string>& stored, const Overrides& overrides);
// A stand-in or a missing texture: what the person has to look at.
bool needsYou(const std::vector<Link>& links);

// Whether a sampler of the preset names no entry of the texture folder (entries by file name).
bool missesTexture(const std::string& presetText, const std::vector<std::string>& entries);

// A file from an imported folder: a preset, a texture, or neither.
enum class PackEntry { None = 0, Preset = 1, Texture = 2 };
PackEntry packEntryKind(const std::string& name);
// A pack texture keeps its file name.
std::string packTextureName(const std::string& name);

// The name a single imported preset is filed from: the display name, "preset" when it is blank.
std::string importDisplayName(const std::string& display);
// The file an import writes, from its sanitised .milk name: that name, or with keepExisting the first of
// "stem.milk", "stem 2.milk", ... that is not taken.
std::string importFileName(const std::string& milkFileName, bool keepExisting,
                           const std::function<bool(const std::string&)>& taken);

// --- reads of the app's own folders -------------------------------------------------------------------------

// The stored textures: regular files with a store extension, in textureOrder.
std::vector<std::string> listStoreTextures(const std::string& dir);
// Every entry's name; empty when the folder cannot be listed.
std::vector<std::string> listEntries(const std::string& dir);
// A regular file's bytes; "" when it cannot be read.
std::string readFile(const std::string& path);
// The entries of milkDir whose extension is exactly "milk": the presets the links are kept for.
std::vector<std::string> linkedPresets(const std::string& milkDir);
// Link folders under milkDir/textures/.links that belong to no preset.
std::vector<std::string> staleLinkDirs(const std::string& milkDir);

}  // namespace geode::viz::milk
