#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "viz/Director.hpp"
#include "viz/DirectorReach.hpp"
#include "viz/MacroBank.hpp"
#include "viz/Palette.hpp"

namespace geode::signal {

// The process-wide values the scene params resolver reads (GLOSSARY "Engine settings"). A renderer latches
// one copy per frame (SettingsLatch), and the same copy re-tunes its sound engine, so nothing changes
// mid-frame.
struct EngineSettings {
    // The user's macro bank (Quick knobs), shared and never edited in place: a frame copies the pointer,
    // not the knobs. Null moves nothing.
    std::shared_ptr<const viz::macrobank::Bank> bank;
    viz::DirectorConfig director{};   // the auto-director's amount and play seed
    viz::director::Reach reach = viz::director::Reach::Look;  // how far the director reaches past its look
    std::shared_ptr<const viz::director::Kept> kept;          // Customize's keep set; null keeps nothing
    // Customize › Look's palette stack (viz/Palette). Null: the look keeps none and its old colour params draw.
    std::shared_ptr<const viz::palette::Stack> palettes;
    bool notesOn = false;             // Settings › Behaviour › Note & key reactions
    std::uint32_t handMask = 0;       // Customize › Music: the moments set by hand (viz::hand); an export pins it for its route merge
    bool reducedMotion = false;       // the system's reduced motion, applied once by the motion field and the clamp
    std::uint64_t noteEntropy = 0;    // mixed into the note layer's seed (the play seed ^ this)
};

// The process globals as they are now (macrobank::global, director::global, director::reach and kept,
// palette::global, note_response::globalEnabled, hand::global), as a live surface reads them. reducedMotion and noteEntropy are the renderer's own and
// are left at their defaults here.
EngineSettings globalSettings();

// An export's settings (#350), taken once when it is prepared: the macro bank, the palette stack, the Note & key switch, the
// hand mask and the director's reach and keep set as they are now; the director from the user's switch, `entitled` (Pro or the trial) and
// `durationMs` (director::forExport, never the live amount, which the live free taste may have cut); and
// no note entropy, so an export is deterministic: the same play exported twice draws the same notes.
EngineSettings exportSettings(bool entitled, long long durationMs);

// What a renderer reads each frame: the process globals live, or, once pinned (an export), the one copy
// pinned at prepare, so live edits to the macro bank, the Note & key switch, the hand mask or the director
// never reach a render in progress (#350). GL-free, so the rule is host-tested (core/tests/resolver_test).
class SettingsLatch {
public:
    // Pins exportSettings(entitled, durationMs) for the rest of this latch's life. Any thread. Returns the
    // pinned hand mask, the one the export's reactions (geode_viz_set_reactions) then play under.
    std::uint32_t pinForExport(bool entitled, long long durationMs);
    bool pinned() const;
    // The director's play seed as the renderer sees it now: the pinned one, else the live one. Any thread.
    std::uint64_t directorSeed() const;
    // The hand mask the renderer's reaction matrix merges under now: the pinned one, else hand::global(). Any thread.
    std::uint32_t handMask() const;
    // This frame's settings. Live: the globals now, with a note entropy drawn fresh whenever the director's
    // play seed changes (so every live play varies on its own; NoteResponse's class comment). Pinned: the
    // pinned copy. `reducedMotion` is the renderer's own and is applied either way. The stepping thread only.
    EngineSettings latch(bool reducedMotion);

private:
    mutable std::mutex lock_;
    std::optional<EngineSettings> pin_;  // guarded by lock_
    // The live note entropy; the stepping thread only.
    std::uint64_t notePlaySeed_ = 0;
    std::uint64_t noteEntropy_ = 0;
    bool noteSeeded_ = false;
};

}  // namespace geode::signal
