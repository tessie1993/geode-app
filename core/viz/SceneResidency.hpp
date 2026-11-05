#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "viz/ThermalGovernor.hpp"

// Which built scenes stay resident on the GPU (#378). No GL here, so core/tests/scene_residency_test.cpp
// drives it on the host; Renderer owns the scenes and asks this unit which ids to release.
//
// Why: the renderer kept every style it ever built. A shader style holds a linked program, a VAO and a
// sound texture; a fluid style holds a whole FluidSim (at Ultra the dye grid alone is ~38 MB, plus
// velocity, pressure, multigrid, particles and bloom). An hour of Auto, the wallpaper or the dream
// visits dozens of styles, so resident memory grew until the memory limiter or LMK killed the process,
// or GL allocations failed. And every tier or scale change resized all of them in one frame.
//
// The policy, an LRU with pins and a weighted spare budget:
//  - Pinned scenes (the one drawn, the one fading out, the layer, the born style, the one just built or
//    requested) never count and are never released.
//  - The rest, most recent first, keep their place while their weight fits the tier's spare budget.
//    A fluid scene weighs kHeavyWeight: one of them costs what several shader styles cost.
//  - A cool phone keeps a few spares, so back and forth between two looks (a beat cut, ‹ ›) stays a
//    cache hit; a hot one keeps none, since a rebuild is cheaper than the memory and a later resize.
//  - Resizes are lazy: a tier or scale change bumps the size stamp, only scenes drawn this frame are
//    resized, and a spare is resized when it is drawn again (needsResize), so a hot phone pays one
//    resize per scene it actually shows, not one per scene it ever showed.
//
// Worked example at Full (budget 3): Neon, Fluid, Arbor, Glass were shown in that order, Glass is
// drawn. Spares by recency: Arbor (1, total 1, kept), Fluid (3, total 4 > 3, released), Neon (1,
// total 2, kept). At Minimal (budget 0) every spare goes and only Glass stays.
namespace geode::viz {

class SceneResidency {
public:
    // What one fluid-solver scene weighs against a shader style in the spare budget.
    static constexpr int kHeavyWeight = 3;

    struct Entry {
        std::string id;
        int weight = 1;
        uint32_t sizeStamp = 0;
    };

    // The weight of unpinned scenes a tier keeps resident: shed spares as the phone heats.
    static int spareBudget(ThermalTier tier) {
        switch (tier) {
            case ThermalTier::Full: return 3;
            case ThermalTier::Eased: return 2;
            case ThermalTier::Reduced: return 1;
            case ThermalTier::Minimal: return 0;
        }
        return 0;
    }

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

    // True when the scene was last sized before the current stamp (a tier or scale change skipped it).
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
        int used = 0;
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
