#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Which built scenes stay resident on the GPU (#378). No GL here, so core/tests/scene_residency_test.cpp
// drives it on the host; Renderer owns the scenes and asks this unit which ids to release.
//
// Why: the renderer kept every style it ever built. A shader style holds a linked program, a VAO and a
// sound texture; a fluid style holds a whole FluidSim (at Ultra the dye grid alone is tens of MB, #999's
// arithmetic gave 67.5 MiB, plus velocity, pressure, multigrid, particles and bloom); a MilkDrop style holds
// a projectM instance with its own screen-size textures (the main pair, the blurs, its frame target). An hour of Auto, the wallpaper or the dream
// visits dozens of styles, so resident memory grew until the memory limiter or LMK killed the process,
// or GL allocations failed. And every scale change resized all of them in one frame.
//
// The policy, an LRU with pins and a weighted spare budget:
//  - Pinned scenes (the one drawn, the one fading out, the layer, the born style, the one just built or
//    requested) are never released. The heaviest of them is free; the others count against the spare
//    budget (#999), so a fluid fading into a fluid over a fluid layer leaves no room for spares.
//  - The rest, most recent first, keep their place while their weight fits the spare budget.
//    A fluid or MilkDrop scene weighs kHeavyWeight: one of them costs what several shader styles cost.
//  - A few spares stay, so back and forth between two looks (a beat cut, ‹ ›) stays a cache hit.
//  - Resizes are lazy: a scale change bumps the size stamp, only scenes drawn this frame are
//    resized, and a spare is resized when it is drawn again (needsResize), so a change pays one
//    resize per scene it actually shows, not one per scene it ever showed.
//
// Worked example at Full (budget 3): Neon, Fluid, Arbor, Glass were shown in that order, Glass is
// drawn. Spares by recency: Arbor (1, total 1, kept), Fluid (3, total 4 > 3, released), Neon (1,
// total 2, kept). At Minimal (budget 0) every spare goes and only Glass stays.
//
// Memory pressure (#854): Android tells the app when its UI is hidden or the process is cached
// (onTrimMemory: TRIM_MEMORY_UI_HIDDEN 20, TRIM_MEMORY_BACKGROUND 40; since Android 14 the other levels
// are no longer delivered and from 15 they are deprecated, so only these two count). Either way the
// spares go: the budget drops to 0, so each renderer keeps what it draws, the layer, the born style
// and the one requested, and nothing else. A wallpaper or dream that is still drawing keeps its look
// for that reason; a rebuilt spare costs one staged build, never a black frame. The signal is
// process-wide (one process serves the app, the wallpaper and the dream) and each renderer reads it
// on its own GL thread, so no release ever runs off the thread that owns the context.
// Only a frame reads the signal, and UI_HIDDEN arrives after the app's GL thread has paused, so a surface
// that stops drawing sheds at its pause, while its context is still current (Renderer::trimSpares,
// geode_viz_trim_spares); the signal is for the surfaces that go on drawing. What each sheds is shedFor.
namespace geode::viz {

// What the system asked for. Background is the stronger ask; scenes shed the same way for both.
enum class MemoryTrim : int { None = 0, UiHidden = 1, Background = 2 };

// What one trim releases (#999). The scene drawn is never in it: a paused GL thread has no current context
// to release it with when BACKGROUND arrives, and a rebuild on return is a staged build, not a free one.
struct TrimShed {
    bool spares = false;        // every unpinned scene, whatever the budget keeps
    bool passTargets = false;   // Liquid's sides, the dive, Trails, the UI blur, the born target
    bool sceneTargets = false;  // the renderer's own two scene targets (sceneA, sceneB)
};

class SceneResidency {
public:
    // The Android onTrimMemory level as a tier. Exactly the two levels the docs say to act on.
    static MemoryTrim trimForAndroidLevel(int level) {
        if (level == 20) return MemoryTrim::UiHidden;    // TRIM_MEMORY_UI_HIDDEN
        if (level == 40) return MemoryTrim::Background;  // TRIM_MEMORY_BACKGROUND
        return MemoryTrim::None;
    }

    // The process's one pending trim, as a generation count any renderer polls from its GL thread.
    class TrimSignal {
    public:
        void request(MemoryTrim trim) {
            if (trim != MemoryTrim::None) generation_.fetch_add(1, std::memory_order_acq_rel);
        }
        // True once per request for each caller's own `seen` counter.
        bool poll(uint32_t& seen) const {
            const uint32_t now = generation_.load(std::memory_order_acquire);
            if (now == seen) return false;
            seen = now;
            return true;
        }

    private:
        std::atomic<uint32_t> generation_{0};
    };
    static TrimSignal& trimSignal() {
        static TrimSignal signal;
        return signal;
    }

    // What one fluid-solver scene weighs against a shader style in the spare budget.
    static constexpr int kHeavyWeight = 3;

    // A scene's weight: a fluid solver or a projectM instance is heavy, a shader style is not.
    static bool heavy(bool fluid, bool milkdrop) { return fluid || milkdrop; }

    // What a trim sheds. A renderer that goes on drawing (the signal read by a frame: a visible wallpaper,
    // the dream, Cast, an export) drops its spares only: the passes' targets hold the picture's history
    // (Liquid's feedback, Trails, the dive), and freeing them popped that history on screen, on the TV and
    // into the MP4. A surface that stops drawing (its pause, an in-app hide, a stopped wallpaper) also drops
    // the passes' targets and its two scene targets, since the next frame it draws builds them again;
    // through hours of background playback they were 20 MiB at 1080p for nothing.
    static TrimShed shedFor(MemoryTrim trim, bool drawing) {
        if (trim == MemoryTrim::None) return {};
        if (drawing) return {true, false, false};
        return {true, true, true};
    }

    struct Entry {
        std::string id;
        int weight = 1;
        uint32_t sizeStamp = 0;
    };

    // The weight of unpinned scenes kept resident.
    static constexpr int kSpareBudget = 3;
    static int spareBudget() { return kSpareBudget; }

    // The spare budget under memory pressure: none at all. Without a trim it is kSpareBudget.
    static int spareBudget(MemoryTrim trim) { return trim == MemoryTrim::None ? spareBudget() : 0; }

    // A newly built scene, sized at `sizeStamp`: the most recent. Re-inserting an id replaces it.
    void insert(const std::string& id, bool heavy, uint32_t sizeStamp) {
        remove(id);
        entries_.insert(entries_.begin(), Entry{id, heavy ? kHeavyWeight : 1, sizeStamp});
    }

    // The scene was asked for again: it becomes the most recent.
    void touch(const std::string& id) {
        auto it = find(id);
        if (it != entries_.end()) std::rotate(entries_.begin(), it, it + 1);
    }

    void remove(const std::string& id) {
        auto it = find(id);
        if (it != entries_.end()) entries_.erase(it);
    }

    void clear() { entries_.clear(); }

    // True when the scene was last sized before the current stamp (a scale change skipped it).
    bool needsResize(const std::string& id, uint32_t sizeStamp) const {
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
        return it != entries_.end() && it->sizeStamp != sizeStamp;
    }

    void markSized(const std::string& id, uint32_t sizeStamp) {
        auto it = find(id);
        if (it != entries_.end()) it->sizeStamp = sizeStamp;
    }

    // Takes the scenes over budget out of the list and returns their ids, least recent last, for the
    // caller to release. `pinned(id)` says which scenes are in use this frame; they are always kept.
    template <typename Pinned>
    std::vector<std::string> evict(Pinned pinned, int budget) {
        std::vector<std::string> victims;
        // The pinned scenes past the heaviest one take their weight out of the budget first.
        int pinnedTotal = 0;
        int pinnedMost = 0;
        for (const Entry& e : entries_) {
            if (!pinned(e.id)) continue;
            pinnedTotal += e.weight;
            pinnedMost = std::max(pinnedMost, e.weight);
        }
        int used = pinnedTotal - pinnedMost;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (pinned(it->id)) {
                ++it;
                continue;
            }
            if (used + it->weight <= budget) {
                used += it->weight;
                ++it;
                continue;
            }
            victims.push_back(it->id);
            it = entries_.erase(it);
        }
        return victims;
    }

    const std::vector<Entry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

private:
    std::vector<Entry>::iterator find(const std::string& id) {
        return std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
    }

    std::vector<Entry> entries_;  // most recent first
};

}  // namespace geode::viz
