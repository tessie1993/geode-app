#pragma once
#include <chrono>
#include <filesystem>

namespace geode::viz {

// When a program binary that a load just used gets its modified time bumped (the sweep deletes the oldest
// first). A bump is a disk write per load, and a style change loads several programs; one that was
// stamped within the last day is as good as new for the sweep, so it is left alone. A program still on
// probation always moves up to proven, whatever its age.
constexpr std::chrono::hours kTouchFreshFor{24};

constexpr bool touchDue(bool fromProbation, std::filesystem::file_time_type::duration sinceStamp) {
    return fromProbation || sinceStamp >= kTouchFreshFor || sinceStamp < std::filesystem::file_time_type::duration::zero();
}

}  // namespace geode::viz
