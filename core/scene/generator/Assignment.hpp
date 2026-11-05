#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "scene/generator/Slot.hpp"

// Which generator each slot shows, and who may change it (#472). The owner's rule: by default only the
// user changes them (OnlyMe). With DirectorSwaps the auto-director may swap the generators the user has
// assigned and left unpinned, at the moments it commits a new look: a drop swaps every one of them, a
// section a seeded subset (at least one). A pinned slot and an empty slot are never touched, so a pin
// is a promise and the director never adds a generator the user did not ask for.
//
// Every pick is a pure function of (play seed, moment index, slot), so a render and a replay of the
// same play swap the same way, and export equals live.

namespace geode::scene::generator {

enum class Policy : int { OnlyMe = 0, DirectorSwaps = 1 };

struct SlotState {
    std::string id;  // a style id from the pool; "" = no generator
    bool pinned = false;
};

struct Assignment {
    std::array<SlotState, kSlotCount> slots;
    Policy policy = Policy::OnlyMe;
};

enum class Moment : int { Section, Drop };

// The director committed a look at a section or a drop. Under DirectorSwaps, swaps unpinned assigned
// slots to other pool ids (never the id a slot already has; a slot whose pool holds nothing else keeps
// it). Under OnlyMe, does nothing. Returns whether any slot changed.
bool onMoment(Assignment& assignment, Moment moment, std::uint64_t playSeed, std::uint32_t momentIndex,
              const std::vector<std::string>& pool);

// The live assignment, process-wide, like director::setGlobal: the live renderer publishes the user's
// choices (ids, pins, policy) whenever a setter changes them, and a renderer that turns offscreen for an
// export adopts that snapshot at the start of the render. So an export shows the generators the user
// set without the shell (OffscreenSceneRenderer) calling any setter, and the live player changing them
// mid-render never reaches the render in progress. Any thread.
void publish(const Assignment& assignment);
Assignment published();

}  // namespace geode::scene::generator
