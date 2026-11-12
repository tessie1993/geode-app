// Host-side checks for the MilkDrop preset every surface restores (core/viz/MilkMemory) and the bundled
// starter pack it falls back on (core/viz/MilkStarterPack). No GL, no projectM, no APK: the pack is a table
// and the preset folder a temporary directory.
//
// What is pinned: a first launch starts on the bundled preset, never the idle one; only a reported load is
// saved, and it survives a rebuild and a restart; a request is used only while nothing was shown; a saved
// file that is gone is skipped; a deleted preset is forgotten; the pack is copied once per version, never
// over a file already there, and a failed copy leaves no file.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "viz/MilkMemory.hpp"
#include "viz/MilkStarterPack.hpp"

namespace fs = std::filesystem;
namespace milk = geode::viz::milk;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const std::string kBundled = "/files/milk/geode_bloom.milk";

// The preset files that exist, by path.
struct Disk {
    std::set<std::string> files{kBundled};
    milk::Memory::Usable usable() const {
        return [this](const std::string& p) { return files.count(p) > 0; };
    }
    milk::Memory::FirstRun firstRun() const {
        return [this]() { return files.count(kBundled) > 0 ? kBundled : std::string(); };
    }
};

// A restart: a new memory seeded with what the last run saved.
struct Run {
    milk::Memory memory;
    std::string* saved;
    explicit Run(std::string* store) : saved(store) { memory.seed(*store); }
    void loaded(const std::string& p) {
        if (memory.loaded(p)) *saved = p;
    }
    void forget(const std::vector<std::string>& paths) {
        if (memory.forget(paths)) saved->clear();
    }
};

void testMemory() {
    Disk disk;
    std::string saved;

    {
        Run run(&saved);
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == kBundled, "a first launch starts on the bundled preset");
    }
    {
        Disk empty;
        empty.files.clear();
        milk::Memory m;
        check(m.restorePath(empty.usable(), empty.firstRun()).empty(), "nothing at all restores nothing");
    }

    disk.files.insert("/milk/a.milk");
    disk.files.insert("/milk/b.milk");
    {
        Run run(&saved);
        run.memory.request("/milk/a.milk");
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == "/milk/a.milk", "a request not yet loaded comes back");
        check(saved.empty(), "a request that never loaded is not saved");
        run.loaded("/milk/a.milk");
        check(saved == "/milk/a.milk", "a reported load is saved");
        run.memory.request("/milk/broken.milk");
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == "/milk/a.milk",
              "a later request that never loaded does not replace the last good one");
        run.loaded("/milk/b.milk");
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == "/milk/b.milk",
              "auto-advance reports a load nobody asked for");
        check(!run.memory.loaded("/milk/b.milk"), "the same load again is nothing new to save");
    }
    {
        Run run(&saved);
        check(run.memory.lastShown(disk.usable()) == "/milk/b.milk", "the shown preset survives a restart");
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == "/milk/b.milk", "and is what a renderer restores");
        run.memory.seed("/milk/a.milk");
        check(run.memory.lastShown(disk.usable()) == "/milk/b.milk", "only the first seed counts");
        run.forget({"/milk/b.milk"});
        check(saved.empty(), "a deleted preset is forgotten on disk too");
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == kBundled, "and the bundled one comes back");
    }
    {
        saved = "/milk-builtin/old.milk";
        Run run(&saved);
        check(run.memory.lastShown(disk.usable()).empty(), "a saved preset whose file is gone is not shown");
        check(run.memory.restorePath(disk.usable(), disk.firstRun()) == kBundled, "it falls back to the bundled one");
    }
    {
        milk::Memory m;
        m.request("/milk/a.milk");
        check(!m.forget({"/milk/a.milk"}), "forgetting a request leaves nothing to drop from the save");
        check(m.restorePath(disk.usable(), disk.firstRun()) == kBundled, "a forgotten request is not restored");
        m.loaded("/milk/a.milk");
        m.seed("/milk/b.milk");
        check(m.lastShown(disk.usable()) == "/milk/a.milk", "a seed after a load changes nothing");
    }
}

// The bundled pack as a table; a name without bytes cannot be read.
struct Pack final : milk::StarterSource {
    std::vector<std::string> list;
    std::map<std::string, std::string> bytes;
    int reads = 0;
    std::vector<std::string> names() override { return list; }
    bool read(const std::string& name, std::string& out) override {
        ++reads;
        const auto it = bytes.find(name);
        if (it == bytes.end()) return false;
        out = it->second;
        return true;
    }
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

void testStarterPack(const fs::path& root) {
    const fs::path dir = root / "milk";
    Pack pack;
    pack.list = {"geode_bloom.milk", "fracture.milk", "notes.txt", "broken.milk"};
    pack.bytes = {{"geode_bloom.milk", "[preset00]\nzoom=1.01\n"}, {"fracture.milk", "[preset00]\nrot=0.2\n"}};

    check(milk::installStarterPack(pack, dir.string()) == 2, "the readable .milk files are copied");
    check(fs::is_directory(dir), "the folder is made");
    check(slurp(dir / "geode_bloom.milk") == "[preset00]\nzoom=1.01\n", "a copy is byte for byte");
    check(!fs::exists(dir / "notes.txt"), "only .milk files are copied");
    check(!fs::exists(dir / "broken.milk"), "a failed copy leaves no file");
    check(slurp(dir / milk::kStarterMarker) == "1", "the marker holds the pack version");

    pack.reads = 0;
    check(milk::installStarterPack(pack, dir.string()) == 0 && pack.reads == 0, "a marked folder is not copied again");

    fs::remove(dir / "fracture.milk");
    writeFile(dir / "geode_bloom.milk", "mine");
    writeFile(dir / milk::kStarterMarker, " 0\n");
    check(milk::installStarterPack(pack, dir.string()) == 1, "an older marker installs again");
    check(slurp(dir / "geode_bloom.milk") == "mine", "never over a file already there");
    check(fs::exists(dir / "fracture.milk"), "a missing one comes back");

    writeFile(dir / milk::kStarterMarker, "x1");
    fs::remove(dir / "fracture.milk");
    check(milk::installStarterPack(pack, dir.string()) == 1, "a marker that is not a number is no marker");

    const fs::path fresh = root / "fresh";
    Pack empty;
    check(milk::firstRunPreset(empty, fresh.string()).empty(), "no first-run preset when the pack has none");
    check(slurp(fresh / milk::kStarterMarker) == "1", "an unlisted pack still marks the folder");
    const fs::path other = root / "other";
    check(milk::firstRunPreset(pack, other.string()) == (other / milk::kFirstRunPreset).string(),
          "the first-run preset is the copied bundled one");

    const fs::path blocked = root / "blocked";
    writeFile(blocked, "a file, not a folder");
    check(milk::installStarterPack(pack, blocked.string()) == 0, "no folder, nothing copied");
    check(!milk::isFile((root / "nowhere.milk").string()) && !milk::isFile(""), "a missing path is no file");
    check(!milk::isFile(dir.string()), "a folder is no file");
}

}  // namespace

int main() {
    testMemory();
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "geode_milk_memory_test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    testStarterPack(root);
    fs::remove_all(root, ec);

    if (failures == 0) std::printf("milk_memory_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
