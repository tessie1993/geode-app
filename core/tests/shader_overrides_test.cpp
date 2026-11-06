// Host-side checks for the process-wide shader overrides (core/viz/ShaderOverrides, #364). Built and run
// by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: one table every renderer reads (live, export, wallpaper, dream), so an edit set once is
// what each of them builds; an empty source clears; the generation moves on every change and only then,
// so a renderer re-applies overrides to its resident scenes exactly when they changed; and a snapshot is a
// copy, so a renderer working through one never sees a half-applied later edit.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "viz/ShaderOverrides.hpp"

namespace so = geode::viz::shader_overrides;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void unknownIdIsEmpty() {
    so::clearAll();
    check(so::get("neon_chrome_sea").empty(), "an id never set draws its built-in shader");
    check(so::get("").empty(), "the empty id is empty");
}

void setReplaceClear() {
    so::clearAll();
    so::set("glow_vortex", "void main(){}");
    check(so::get("glow_vortex") == "void main(){}", "set is what get returns");
    check(so::get("black_hole").empty(), "setting one style leaves the others stock");
    so::set("glow_vortex", "void main(){ /* v2 */ }");
    check(so::get("glow_vortex") == "void main(){ /* v2 */ }", "a second set replaces the first");
    so::set("glow_vortex", "");
    check(so::get("glow_vortex").empty(), "an empty source clears");
    so::set("glow_vortex", "a");
    so::clear("glow_vortex");
    check(so::get("glow_vortex").empty(), "clear clears");
    so::set("", "ignored");
    check(so::snapshot().empty(), "an empty style id is never stored");
}

void generationMovesOnChangeOnly() {
    so::clearAll();
    const uint64_t g0 = so::generation();
    so::set("hex_hive", "x");
    const uint64_t g1 = so::generation();
    check(g1 > g0, "a set moves the generation");
    so::set("hex_hive", "x");
    check(so::generation() == g1, "setting the same source again does not");
    so::set("hex_hive", "y");
    const uint64_t g2 = so::generation();
    check(g2 > g1, "a replace moves the generation");
    so::clear("hex_hive");
    const uint64_t g3 = so::generation();
    check(g3 > g2, "a clear moves the generation");
    so::clear("hex_hive");
    check(so::generation() == g3, "clearing what is not set does not");
    so::set("never_set", "");
    check(so::generation() == g3, "an empty set of an unknown id does not");
}

void snapshotIsACopy() {
    so::clearAll();
    so::set("mandelbox_cathedral", "one");
    const auto before = so::snapshot();
    so::set("mandelbox_cathedral", "two");
    so::set("glow_vortex", "three");
    check(before.size() == 1, "a copy taken earlier gains no later entries");
    check(before.at("mandelbox_cathedral") == "one", "a copy taken earlier keeps its source");
    const std::string held = so::get("mandelbox_cathedral");
    so::clear("mandelbox_cathedral");
    check(held == "two", "a source read earlier is unchanged by a later clear");
    check(so::snapshot().size() == 1, "the live table has the later edits");
}

// Kills: the table or the generation counter made per thread (`static thread_local`), i.e. per renderer
// again. The app's main thread writes; the live, export, wallpaper and dream renderers read on their own
// GL threads, and one may be created long after the edit.
void everyRendererThreadReadsTheOneTable() {
    so::clearAll();
    uint64_t generationAfterWrite = 0;
    std::thread writer([&] {
        so::set("neon_chrome_sea", "void main(){ /* mine */ }");
        generationAfterWrite = so::generation();
    });
    writer.join();
    check(so::get("neon_chrome_sea") == "void main(){ /* mine */ }",
          "an edit written on another thread is what this thread reads");
    std::string live;
    std::string wallpaper;
    uint64_t liveGeneration = 0;
    uint64_t wallpaperGeneration = 0;
    std::thread liveGl([&] {
        live = so::get("neon_chrome_sea");
        liveGeneration = so::generation();
    });
    std::thread wallpaperGl([&] {
        wallpaper = so::get("neon_chrome_sea");
        wallpaperGeneration = so::generation();
    });
    liveGl.join();
    wallpaperGl.join();
    check(live == "void main(){ /* mine */ }", "the live renderer's thread reads the edit");
    check(wallpaper == live, "the wallpaper's thread reads the same edit as the live one");
    check(liveGeneration == generationAfterWrite && wallpaperGeneration == generationAfterWrite,
          "every thread sees the one generation, so each renderer notices the same change");
    // A renderer created after a later replace (the dream starting) builds the replaced source.
    so::set("neon_chrome_sea", "void main(){ /* v2 */ }");
    std::string dream;
    std::thread dreamGl([&] { dream = so::get("neon_chrome_sea"); });
    dreamGl.join();
    check(dream == "void main(){ /* v2 */ }", "a renderer started after a replace reads the replace");
}

// Kills: lookup by lower_bound / upper_bound or a prefix match (an override for glass_10 drawn by glass_1),
// a case-folding or trimming key compare. Each style's edit is its own.
void neighbouringStyleIdsNeverLeak() {
    so::clearAll();
    so::set("glass_10", "ten");
    check(so::get("glass_1").empty(), "glass_10's edit is not glass_1's");
    check(so::get("glass_").empty(), "nor a shorter prefix's");
    check(so::get("glass_100").empty(), "nor a longer id's");
    check(so::get("Glass_10").empty(), "ids are case-sensitive");
    check(so::get("glass_10 ").empty(), "ids are not trimmed");
    so::set("glass_1", "one");
    so::clear("glass_10");
    check(so::get("glass_1") == "one", "clearing glass_10 leaves glass_1's edit");
    check(so::get("glass_10").empty(), "and clears glass_10 alone");
}

// Kills: a set or clear of an id outside the catalogue that touches another style's entry. The table does
// not know the catalogue (a renderer only applies it to scenes it builds), so a stray id must stay inert.
void anUnknownStyleIdTouchesNoRealStyle() {
    so::clearAll();
    so::set("black_hole", "bh");
    so::set("no_such_style", "stray");
    check(so::get("black_hole") == "bh", "an edit for an unknown id leaves a real style's edit");
    so::clear("no_such_style");
    check(so::get("black_hole") == "bh", "clearing an unknown id leaves a real style's edit");
    check(so::get("no_such_style").empty(), "and the unknown id draws nothing of its own");
}

// Kills: a source cut at the first NUL or newline (c_str copies, line reads), a size cap. The PR promises
// the table holds what the user submitted even when it fails to compile, so Revert and preset save show
// the user's exact text; falling back to the built-in shader on a compile error is the GL scene's job.
void aMalformedSourceIsHeldVerbatim() {
    so::clearAll();
    std::string broken = "#version 300 es\n\tvoid main( {  // unbalanced, \xC3\xA9\n";
    broken.push_back('\0');
    broken += "after the nul";
    broken += std::string(100000, 'x');
    so::set("hex_hive", broken);
    const std::string back = so::get("hex_hive");
    check(back.size() == broken.size(), "a malformed source comes back at its full length");
    check(back == broken, "a malformed source comes back byte for byte");
}

// Kills: generation stepping by 0 or toggling (fetch_xor), so two changes land back on a value a renderer
// already applied (ABA) and it misses the second; a bump on a no-op; the lookup and erase mutants above.
// A seeded random edit history (raw mt19937 output, the same on every standard library) against a
// reference model of the header's contract: an empty id is ignored, an empty source clears, else replace.
void randomEditHistoryMatchesTheContract() {
    so::clearAll();
    const std::vector<std::string> ids{"glass_1",    "glass_10",        "glass_1 ",      "Glass_1", "dive",
                                       "black_hole", "neon_chrome_sea", "no_such_style", "dive_1",  ""};
    const std::vector<std::string> sources{"", "", "a", "b", "void main(){}", "#version 300 es\n???"};
    std::map<std::string, std::string> model;
    std::set<uint64_t> seen{so::generation()};
    std::mt19937 rng(364u);
    bool reads = true;
    bool moves = true;
    bool fresh = true;
    bool nonzero = true;
    for (int step = 0; step < 4000; ++step) {
        const std::string& id = ids[rng() % ids.size()];
        const std::string& source = sources[rng() % sources.size()];
        const bool viaClear = rng() % 4 == 0;
        const auto before = model;
        const uint64_t generationBefore = so::generation();
        if (viaClear) {
            so::clear(id);
        } else {
            so::set(id, source);
        }
        if (!id.empty()) {
            if (viaClear || source.empty()) {
                model.erase(id);
            } else {
                model[id] = source;
            }
        }
        const uint64_t generationAfter = so::generation();
        const bool changed = model != before;
        if (changed != (generationAfter != generationBefore)) moves = false;
        if (changed && !seen.insert(generationAfter).second) fresh = false;
        if (!model.empty() && generationAfter == 0) nonzero = false;
        if (so::snapshot() != model) reads = false;
        for (const auto& probe : ids) {
            const auto found = model.find(probe);
            if (so::get(probe) != (found == model.end() ? std::string() : found->second)) reads = false;
        }
    }
    check(reads, "over a random edit history every style reads exactly its own latest edit");
    check(moves, "the generation moves on exactly the edits that change the table");
    check(fresh, "the generation never returns to a value a renderer already applied");
    check(nonzero, "with any override set the generation is never 0, where a new renderer starts");
}

// Kills: removing the table lock (a torn or crashed read while the main thread edits), or bumping the
// generation before the table is written, so a renderer that sees the new generation snapshots the old
// table and then never re-syncs. Race-dependent: under a mutant these fail often, not on every run.
void readersOnOtherThreadsNeverSeeAHalfEdit() {
    so::clearAll();
    const std::string a(2000, 'a');
    const std::string b(2000, 'b');
    std::atomic<bool> go{false};
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    auto reader = [&] {
        while (!go.load()) {
        }
        while (!done.load()) {
            const std::string s = so::get("lucid_1");
            if (!(s.empty() || s == a || s == b)) torn.fetch_add(1);
            const auto copy = so::snapshot();
            const auto it = copy.find("lucid_1");
            if (it != copy.end() && it->second != a && it->second != b) torn.fetch_add(1);
        }
    };
    std::thread live(reader);
    std::thread wallpaper(reader);
    go.store(true);
    for (int i = 0; i < 20000; ++i) so::set("lucid_1", i % 3 == 0 ? a : i % 3 == 1 ? b : std::string());
    done.store(true);
    live.join();
    wallpaper.join();
    check(torn.load() == 0, "a renderer reading while the main thread edits sees a whole source or none");

    // Every edit below changes the value, so it moves the generation exactly once: the k-th edit is
    // generation g0 + k. A reader that loads generation g and then reads must see edit g - g0 or later.
    so::clearAll();
    const uint64_t g0 = so::generation();
    std::atomic<int> stale{0};
    go.store(false);
    done.store(false);
    auto syncer = [&] {
        while (!go.load()) {
        }
        while (!done.load()) {
            const uint64_t g = so::generation();
            const std::string s = so::get("lucid_2");
            const uint64_t k = s.empty() ? 0 : std::stoull(s);
            if (k < g - g0) stale.fetch_add(1);
        }
    };
    std::thread first(syncer);
    std::thread second(syncer);
    go.store(true);
    for (int k = 1; k <= 20000; ++k) so::set("lucid_2", std::to_string(k));
    done.store(true);
    first.join();
    second.join();
    check(stale.load() == 0, "a renderer that sees a new generation reads that edit or a later one");
}

// Kills: clearAll that forgets to move the generation (a renderer would keep drawing a forgotten edit).
void forgettingEveryOverrideMovesTheGeneration() {
    so::clearAll();
    so::set("dive_1", "d");
    const uint64_t before = so::generation();
    so::clearAll();
    check(so::generation() != before, "forgetting every override moves the generation");
    check(so::get("dive_1").empty(), "and every style draws its built-in shader again");
}

}  // namespace

int main() {
    unknownIdIsEmpty();
    setReplaceClear();
    generationMovesOnChangeOnly();
    snapshotIsACopy();
    everyRendererThreadReadsTheOneTable();
    neighbouringStyleIdsNeverLeak();
    anUnknownStyleIdTouchesNoRealStyle();
    aMalformedSourceIsHeldVerbatim();
    randomEditHistoryMatchesTheContract();
    readersOnOtherThreadsNeverSeeAHalfEdit();
    forgettingEveryOverrideMovesTheGeneration();
    if (failures) {
        std::printf("%d shader override check(s) failed\n", failures);
        return 1;
    }
    std::printf("shader_overrides_test: all checks passed\n");
    return 0;
}
