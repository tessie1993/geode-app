// Host-side checks for the MilkDrop texture rules (core/viz/MilkTextures), pinned to what the Kotlin shell did
// before they moved: TextureStore's header checks, caps and stored names, its generated show preset and the
// presets a removal sweeps, MilkTextureLinks' sampler scan and resolution ladder, MilkPackImporter's file kinds
// and missing-texture check, MilkImportController's collision names. No APK, no projectM: folders are a
// temporary directory.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "viz/MilkTextures.hpp"

namespace fs = std::filesystem;
namespace milk = geode::viz::milk;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

using Bytes = std::vector<uint8_t>;

void put16(Bytes& b, size_t at, uint32_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

void put32(Bytes& b, size_t at, uint32_t v) {
    put16(b, at, v & 0xFFFF);
    put16(b, at + 2, v >> 16);
}

Bytes dds(uint32_t width, uint32_t height, size_t size = 128) {
    Bytes b(size, 0);
    if (size >= 4) {
        b[0] = 'D';
        b[1] = 'D';
        b[2] = 'S';
        b[3] = ' ';
    }
    if (size >= 20) {
        put32(b, 12, height);
        put32(b, 16, width);
    }
    return b;
}

Bytes tga(uint32_t width, uint32_t height, uint8_t type = 2, uint8_t depth = 32, uint8_t colorMap = 0, size_t size = 18) {
    Bytes b(size, 0);
    if (size >= 18) {
        b[1] = colorMap;
        b[2] = type;
        put16(b, 12, width);
        put16(b, 14, height);
        b[16] = depth;
    }
    return b;
}

bool image(const std::string& name, const Bytes& b, int w = -1, int h = -1) {
    return milk::isTextureImage(name, b.data(), b.size(), w, h);
}

void testHeaders() {
    check(image("a.dds", dds(256, 128)), "a DDS with its magic and a size that fits");
    check(image("a.DDS", dds(2048, 2048)), "the extension is caseless and 2048 a side fits");
    check(!image("a.dds", dds(256, 128, 127)), "a DDS shorter than its 128-byte header is refused");
    Bytes wrong = dds(256, 128);
    wrong[3] = 'X';
    check(!image("a.dds", wrong), "a DDS without its magic is refused");
    check(!image("a.dds", dds(0, 128)), "a DDS of no width is refused");
    check(!image("a.dds", dds(2049, 128)), "a DDS past 2048 wide is refused");
    check(!image("a.dds", dds(128, 2049)), "a DDS past 2048 high is refused");
    check(!image("a.dds", dds(128, 0xFFFFFFFFu)), "a DDS height that was negative to the Kotlin reader is refused");
    check(image("a.dds", dds(256, 128), -1, -1), "a DDS is judged by its header, not the bounds decode");

    check(image("a.tga", tga(64, 64)), "a true-colour TGA that fits");
    for (const uint8_t type : {1, 2, 3, 9, 10, 11}) check(image("a.tga", tga(64, 64, type)), "TGA image type " + std::to_string(type));
    for (const uint8_t type : {0, 4, 12, 32}) check(!image("a.tga", tga(64, 64, type)), "no TGA image type " + std::to_string(type));
    for (const uint8_t depth : {8, 15, 16, 24, 32}) check(image("a.tga", tga(64, 64, 2, depth)), "TGA depth " + std::to_string(depth));
    for (const uint8_t depth : {0, 1, 12, 48}) check(!image("a.tga", tga(64, 64, 2, depth)), "no TGA depth " + std::to_string(depth));
    check(image("a.tga", tga(64, 64, 1, 8, 1)), "a colour-mapped TGA");
    check(!image("a.tga", tga(64, 64, 2, 32, 2)), "a TGA colour map type past 1 is refused");
    check(!image("a.tga", tga(5000, 64)), "a TGA past 2048 wide is refused");
    check(!image("a.tga", tga(64, 0)), "a TGA of no height is refused");
    check(!image("a.tga", tga(64, 64, 2, 32, 0, 17)), "a TGA shorter than its 18-byte header is refused");

    const Bytes any(10, 1);
    check(image("a.png", any, 512, 256), "a PNG takes the bounds decode's size");
    check(image("a.JPEG", any, 1, 1), "so does a JPEG");
    check(!image("a.png", any, -1, -1), "a picture the decode could not read is refused");
    check(!image("a.bmp", any, 2049, 16), "a picture past 2048 a side is refused");

    check(milk::admitsTexture("a.dib", 0), "an undeclared size is read");
    check(milk::admitsTexture("a.PNG", milk::kTextureMaxBytes), "exactly the cap is read");
    check(!milk::admitsTexture("a.png", milk::kTextureMaxBytes + 1), "a declared size past the cap is not");
    check(!milk::admitsTexture("a.gif", 10), "a GIF is not a texture");
    check(!milk::admitsTexture("png", 10), "a name without an extension is not");
    check(milk::kTextureMaxBytes == 16LL * 1024 * 1024, "the cap is 16 MiB");

    check(milk::acceptedTextureName("my tex.png", any.data(), any.size(), 64, 64) == "my_tex_0c0d16cd.png", "an accepted pick gets its stored name");
    check(milk::acceptedTextureName("a.png", any.data(), 0, 64, 64).empty(), "an empty file is refused");
    check(milk::acceptedTextureName("a.gif", any.data(), any.size(), 64, 64).empty(), "a GIF is refused");
    check(milk::acceptedTextureName("a.png", any.data(), any.size(), -1, -1).empty(), "an unreadable picture is refused");
}

void testNames() {
    check(milk::storedTextureName("cloud.png") == "cloud.png", "a safe name is kept");
    check(milk::storedTextureName("Cloud.PNG") == "Cloud.png", "the case of the stem is kept, the extension lowercased");
    check(milk::storedTextureName("my tex.png") == "my_tex_0c0d16cd.png", "a space becomes _ and the stem's hash follows");
    check(milk::storedTextureName("Fire-Ball.TGA") == "Fire_Ball_46776665.tga", "so does a dash");
    check(milk::storedTextureName("Fire Ball.png") == "Fire_Ball_7db97dd4.png", "a different original, a different hash");
    check(milk::storedTextureName(".png") == "tex_e3b0c442.png", "an empty stem becomes tex");
    check(milk::storedTextureName("1abc.jpg") == "t1abc_57cb980e.jpg", "a leading digit gets a t");
    check(milk::storedTextureName("2.dds") == "t2_d4735e3a.dds", "even alone");
    check(milk::storedTextureName("caf\xC3\xA9.png") == "caf__850f7dc4.png", "a letter beyond ASCII is one _, hashed as UTF-8");
    check(milk::storedTextureName("\xF0\x9F\x98\x80.png") == "__f0443a34.png", "an emoji is one _");
    check(milk::storedTextureName("archive.tar.png") == "archive_tar_26038919.png", "only the last dot is the extension's");
    check(milk::storedTextureName("noext") == "noext.", "a name without a dot keeps a bare dot");
    check(milk::storedTextureName("a b") == "a_b_c8687a08.", "and is hashed whole when it changes");

    // The strip rule undoes exactly what the store adds.
    for (const char* original : {"my tex.png", "Fire-Ball.TGA", "archive.tar.png"}) {
        const std::string stored = milk::storedTextureName(original);
        check(milk::normalized(milk::withoutRenameSuffix(milk::stemOf(stored))) == milk::normalized(milk::stemOf(original)),
              std::string("the rename of ") + original + " is undone");
    }
    check(milk::withoutRenameSuffix("abc_0123abcd") == "abc", "_ and eight lowercase hex is the suffix");
    check(milk::withoutRenameSuffix("_0123abcd") == "", "even the whole stem");
    check(milk::withoutRenameSuffix("abc_0123ABCD") == "abc_0123ABCD", "uppercase hex is not the store's");
    check(milk::withoutRenameSuffix("abc_0123abc") == "abc_0123abc", "seven is not eight");
    check(milk::withoutRenameSuffix("abc0123abcd") == "abc0123abcd", "no _, no suffix");
    check(milk::withoutRenameSuffix("abc_0123abcdx") == "abc_0123abcdx", "only at the end");

    check(milk::normalized("My-Tex 2") == "mytex2", "normalized: lowercase letters and digits");
    check(milk::normalized("a_b") == "ab", "_ goes");
    check(milk::normalized("Kelvin\xE2\x84\xAA") == "kelvink", "the Kelvin sign lowercases to k");
    check(milk::normalized("\xC4\xB0mage") == "image", "a dotted capital I lowercases to i and a dropped mark");
    check(milk::normalized("CAF\xC3\x89") == "caf\xC3\xA9", "a Latin-1 capital lowercases and stays");
    check(milk::normalized("a\xE2\x80\x93" "b") == "ab", "a dash beyond ASCII goes");
    check(milk::normalized("x\xF0\x9F\x98\x80y") == "xy", "an emoji goes");

    check(milk::extensionOf("a.PNG") == "png" && milk::extensionOf("a") == "" && milk::extensionOf("a.") == "", "extensionOf");
    check(milk::stemOf("a.b.png") == "a.b" && milk::stemOf("a") == "a" && milk::stemOf(".png") == "", "stemOf");

    const std::vector<std::string> order = milk::textureOrder({"b.png", "A.png", "a.jpg", "C.png"});
    check(order == std::vector<std::string>({"a.jpg", "A.png", "b.png", "C.png"}), "the store's order is by lowercase name");
    check(milk::textureOrder({"A.png", "a.png"}) == std::vector<std::string>({"A.png", "a.png"}), "names alike in lowercase keep their order");
    check(milk::textureOrder({"\xEF\xBC\xA1.png", "\xF0\x9F\x98\x80.png"}) ==
              std::vector<std::string>({"\xF0\x9F\x98\x80.png", "\xEF\xBC\xA1.png"}),
          "UTF-16 order: a supplementary character before U+FF21");
}

void testShowPreset() {
    check(milk::showPresetName("cloud.png") == "show_cloud_png.milk", "the show preset's name");
    check(milk::showPresetName("a.b.png") == "show_a_b_png.milk", "every dot becomes _");
    const std::string text = milk::showPresetText("cloud.png");
    check(text.rfind("MILKDROP_PRESET_VERSION=201\nPSVERSION=2\nPSVERSION_WARP=2\nPSVERSION_COMP=2\n[preset00]\n", 0) == 0,
          "the show preset's header");
    check(text.find("\ncomp_1=`sampler sampler_cloud;\n") != std::string::npos, "it declares the texture's sampler");
    check(text.find("\ncomp_5=`   float3 img = tex2D(sampler_cloud, uv2).xyz;\n") != std::string::npos, "and samples it");
    check(text.find("\nper_frame_1=wave_a = 0;\n") != std::string::npos, "the wave is hidden");
    check(text.size() >= 9 && text.substr(text.size() - 9) == "comp_8=`}", "it ends on comp_8 with no newline");
    size_t lines = 1;
    for (const char c : text) lines += c == '\n' ? 1 : 0;
    check(lines == 31, "thirty-one lines");

    check(milk::removedPresetNames("cloud.png", {"cloud.jpg"}) == std::vector<std::string>({"show_cloud_png.milk"}),
          "a removal sweeps its own show preset; the stem-named one stays while a texture shares the stem");
    check(milk::removedPresetNames("cloud.png", {"other.png"}) ==
              std::vector<std::string>({"show_cloud_png.milk", "show_cloud.milk"}),
          "and the stem-named one when none does");
    check(milk::removedPresetNames("cloud", {}) == std::vector<std::string>({"show_cloud.milk"}), "one name when they are the same");
}

void testSamplers() {
    const std::string text =
        "sampler sampler_headlights; tex2D(sampler_headlights, uv); sampler_fw_clouds sampler_pc_ sampler_main "
        "sampler_rand01 sampler_blur1 xsampler_nope _sampler_nope Sampler_nope sampler_fwfoo sampler_Headlights "
        "sampler_noise_lq sampler_fw_main sampler_a_b sampler_randy sampler__under \xC3\xA9sampler_nope sampler_caf\xC3\xA9 "
        "sampler_bad\xFFtail sampler_pw";
    const std::vector<std::string> refs = milk::samplerReferences(text);
    const std::vector<std::string> want = {"headlights", "clouds", "pc_", "fwfoo", "a_b", "_under", "caf\xC3\xA9", "bad", "pw"};
    check(refs == want, "the sampler scan: names in order, first spelling, no built-ins or randNN, word boundaries");
    check(milk::samplerReferences("").empty(), "no text, no references");
    check(milk::samplerReferences("sampler_ sampler").empty(), "no name, no reference");
}

void testLadder() {
    const std::vector<std::string> stored =
        milk::textureOrder({"Headlights.PNG", "my_tex_0c0d16cd.png", "clouds2.jpg", "zebra.png", "a.png"});
    const milk::Overrides none;

    const milk::Link pinned = milk::resolve("headlights", stored, {{"headlights", "zebra.png"}});
    check(pinned.kind == milk::LinkKind::Overridden && pinned.texture == "zebra.png", "1: a live choice wins");
    check(pinned.linkNames == std::vector<std::string>({"headlights.png"}), "one link name when both spellings agree");
    check(milk::resolve("HeadLights", stored, {{"headlights", "zebra.png"}}).kind == milk::LinkKind::Overridden,
          "choices are kept by the lowercase name");
    const milk::Link gone = milk::resolve("headlights", stored, {{"headlights", "gone.png"}});
    check(gone.kind == milk::LinkKind::Matched && gone.texture == "Headlights.PNG", "a choice whose file is gone falls through");
    check(milk::resolve("headlights", stored, {{"headlights", ""}}).kind == milk::LinkKind::Matched, "an empty choice is none");

    const milk::Link exact = milk::resolve("HEADLIGHTS", stored, none);
    check(exact.kind == milk::LinkKind::Matched && exact.texture == "Headlights.PNG", "2: the stem, caseless");
    check(exact.linkNames == std::vector<std::string>({"HEADLIGHTS.PNG", "headlights.png"}), "linked as asked and lowercased");

    const milk::Link renamed = milk::resolve("my_tex", stored, none);
    check(renamed.kind == milk::LinkKind::Renamed && renamed.texture == "my_tex_0c0d16cd.png", "3: the store's rename undone");
    check(milk::resolve("MyTex", stored, none).kind == milk::LinkKind::Renamed, "normalized on both sides");
    check(milk::resolve("zeb-ra", stored, none).kind == milk::LinkKind::Renamed, "a renamed match needs no suffix");

    const milk::Link standIn = milk::resolve("cloud", stored, none);
    check(standIn.kind == milk::LinkKind::Substituted && standIn.texture == "clouds2.jpg", "4: the closest-named stand-in");
    check(standIn.linkNames == std::vector<std::string>({"cloud.jpg"}), "linked under the expected name");
    check(milk::resolve("qqq", milk::textureOrder({"aaa.png", "b.png"}), none).texture == "b.png",
          "no prefix in common: the shortest name");
    check(milk::resolve("q", milk::textureOrder({"bb.png", "aa.png"}), none).texture == "aa.png",
          "a tie in length: the first in the store's order");

    const milk::Link missing = milk::resolve("headlights", {}, {{"headlights", "zebra.png"}});
    check(missing.kind == milk::LinkKind::Missing && missing.texture.empty() && missing.linkNames.empty(), "5: nothing stored");

    check(milk::resolve("foo", milk::textureOrder({"Foo.png", "foo.jpg"}), none).texture == "foo.jpg",
          "the first exact match in the store's order");
    check(milk::resolve("kick", {"\xE2\x84\xAAick.png"}, none).kind == milk::LinkKind::Matched, "the Kelvin sign matches k");
    check(milk::resolve("image", {"\xC4\xB1mage.png"}, none).kind == milk::LinkKind::Matched, "a dotless i matches i");
    check(milk::resolve("noext", {"NoExt"}, none).linkNames == std::vector<std::string>({"noext."}), "a texture with no extension");

    const std::vector<milk::Link> all =
        milk::resolveAll("sampler_headlights sampler_my_tex sampler_cloud sampler_main", stored, none);
    check(all.size() == 3 && all[0].kind == milk::LinkKind::Matched && all[1].kind == milk::LinkKind::Renamed &&
              all[2].kind == milk::LinkKind::Substituted,
          "a preset resolves each reference in order");
    check(milk::needsYou(all), "a stand-in needs the person");
    check(!milk::needsYou({all[0], all[1]}), "matches and renames do not");
    check(milk::needsYou({missing}), "a missing texture does");
    check(milk::resolveAll("no samplers", stored, none).empty(), "no references, no links");
}

void testPack() {
    check(milk::packEntryKind("a/b/Preset.MILK") == milk::PackEntry::Preset, "a .milk is a preset");
    check(milk::packEntryKind("x.MIL\xE2\x84\xAA") == milk::PackEntry::Preset, "with a Kelvin sign too");
    for (const char* t : {"x.png", "x.JPG", "x.jpeg", "x.bmp", "x.tga", "x.dds"}) check(milk::packEntryKind(t) == milk::PackEntry::Texture, t);
    for (const char* n : {"x.dib", "x.txt", "noext", "x.milk.bak"}) check(milk::packEntryKind(n) == milk::PackEntry::None, n);
    check(milk::packTextureName("dir/sub/x.png") == "x.png" && milk::packTextureName("x.png") == "x.png", "a pack texture keeps its file name");

    const std::string text = "sampler_clouds sampler_main sampler_rand00";
    check(!milk::missesTexture(text, {"Clouds.PNG", ".links"}), "a texture by any extension and case satisfies a sampler");
    check(milk::missesTexture(text, {".links", "other.png"}), "a sampler naming nothing misses");
    check(!milk::missesTexture("sampler_main sampler_rand00", {}), "built-ins and randNN never miss");
    check(!milk::missesTexture("", {}), "no text, nothing missing");
}

void testImportNames() {
    check(milk::importDisplayName("") == "preset", "a blank name files as preset");
    check(milk::importDisplayName(" \t\n") == "preset", "whitespace is blank");
    check(milk::importDisplayName("\xC2\xA0\xE3\x80\x80") == "preset", "so are the no-break and ideographic spaces");
    check(milk::importDisplayName(" a ") == " a ", "anything else is kept");

    const auto none = [](const std::string&) { return false; };
    const auto all = [](const std::string&) { return true; };
    const std::set<std::string> two = {"Cool.milk", "Cool 2.milk"};
    const auto some = [&two](const std::string& n) { return two.count(n) > 0; };
    check(milk::importFileName("Cool.milk", false, all) == "Cool.milk", "without keepExisting the first name, taken or not");
    check(milk::importFileName("Cool.milk", true, none) == "Cool.milk", "a free name is used");
    check(milk::importFileName("Cool.milk", true, some) == "Cool 3.milk", "else the first free numbered one");
    check(milk::importFileName("Cool", true, some) == "Cool 3.milk", "the .milk suffix is the name's");
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

void testDisk() {
    const fs::path root = fs::temp_directory_path() / "milk_textures_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path textures = root / "textures";
    fs::create_directories(textures / ".links" / "live");
    fs::create_directories(textures / ".links" / "stale");
    fs::create_directories(textures / "folder.png");
    write(textures / "b.png", "x");
    write(textures / "A.dds", "x");
    write(textures / "notes.txt", "x");
    write(root / "live.milk", "sampler_b sampler_gone");
    write(root / "other.MILK", "x");

    check(milk::listStoreTextures(textures.string()) == std::vector<std::string>({"A.dds", "b.png"}),
          "the store lists regular texture files in its order");
    check(milk::linkedPresets(root.string()) == std::vector<std::string>({"live.milk"}), "presets by the exact extension");
    check(milk::staleLinkDirs(root.string()) == std::vector<std::string>({"stale"}), "a link folder with no preset is stale");
    check(milk::readFile((root / "live.milk").string()) == "sampler_b sampler_gone", "a preset's text");
    check(milk::readFile((root / "none.milk").string()).empty() && milk::readFile(textures.string()).empty(),
          "nothing from a missing file or a folder");
    check(milk::listEntries((root / "none").string()).empty(), "nothing from a missing folder");
    check(milk::missesTexture(milk::readFile((root / "live.milk").string()), milk::listEntries(textures.string())),
          "the preset misses gone");

    fs::remove_all(root, ec);
}

}  // namespace

int main() {
    testHeaders();
    testNames();
    testShowPreset();
    testSamplers();
    testLadder();
    testPack();
    testImportNames();
    testDisk();
    if (failures == 0) std::printf("milk_textures_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
