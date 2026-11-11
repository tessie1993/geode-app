#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "sound/SoundFrame.h"
#include "viz/Director.hpp"
#include "viz/Lfo.hpp"
#include "viz/Params.hpp"
#include "viz/Reactions.hpp"

namespace geode::viz {

namespace director {
// How far the director reaches past its look (GLOSSARY "Director reach"). Wire ordinals; append only.
//   Look      the look alone: the picture as it was before this setting existed
//   Shuffle   + at a new section, a drop or a long stretch it rolls some Customize categories
//   Modulate  + its own slow waves and sound followers on sliders the style reaches
enum class Reach { Look, Shuffle, Modulate };
// Process-wide like the director's switch. Anything outside the enum is Look.
void setReach(int level);
Reach reach();
// Customize's keep set, which no shuffle and no modulator touches: one control key or "#CATEGORY" a
// line, expanded here (customize::expandLocks). Process-wide; never null.
using Kept = std::set<std::string>;
void setKept(std::string_view lines);
std::shared_ptr<const Kept> kept();
}  // namespace director

// The director's reach past its look. Both halves are offsets over the params the user asked for, never
// a write back to them, so the sliders in Customize stay where the user left them.
//
// Shuffle: at a new section, a drop or a long stretch with neither, some categories are rolled by the
// Customize randomizer (the one roll table) from (play seed, commit number). How many, and how far each
// slider goes toward its rolled value, grows with the feeling's energy; a drop takes every category all
// the way. A slider glides there in seconds; a toggle or a choice flips on the moment and is held until
// the next one. Categories a moment does not roll keep what they had.
//
// Modulation: three slow waves and three slow sound followers, dealt again at every look the director
// commits and crossfaded, each an offset added by the engines the user's own waves and routes run on
// (LfoEngine, ReactionEngine), so the luminance limits there hold here too.
//
// It keeps off: kept controls and categories, controls the style on screen does not reach, the palette
// and the dials the look already decides, a control the user moved since the last moment (until the
// next), and the Drop and New section moments and every source the hand mask gives the user.
//
// No tempo anywhere: moments come from the director, every time is in seconds.
// A pure function of (inputs, dt); state resets with the renderer.
class DirectorReach {
public:
    static constexpr int kWaves = LfoEngine::kSlots;
    static constexpr int kFollowers = 3;
    static constexpr float kSectionGlideSeconds = 4.0f;
    static constexpr float kDropGlideSeconds = 1.5f;
    static constexpr float kLetGoSeconds = 0.4f;
    static constexpr float kCrossSeconds = 2.0f;
    static constexpr float kMaxDepth = 0.25f;
    static constexpr float kMinWaveSeconds = 10.0f;
    static constexpr float kMaxWaveSeconds = 45.0f;

    struct Inputs {
        director::Reach reach = director::Reach::Look;
        std::uint64_t seed = 0;
        std::uint32_t handMask = 0;
        bool reducedMotion = false;
        std::shared_ptr<const director::Kept> kept;
        const std::string* styleId = nullptr;
        // The director this frame: how much it drives, whether it committed a look, why, its commit
        // count and the energy of the feeling the look was made for.
        float weight = 0.0f;
        bool committed = false;
        Director::Reason reason = Director::Reason::First;
        int commit = 0;
        float energy = 0.5f;
        // The frame's wall time, which a control handed back to the user glides home on; < 0 = dt.
        float wallDt = -1.0f;
    };

    void step(const SceneParams& requested, const GeodeSoundFrame& sound, float dt, const Inputs& in);
    // Before the director's look: the rolled offsets and held toggles over the user's params.
    SceneParams applyShuffle(const SceneParams& p) const;
    // After the director's axes, before the user's macro bank.
    SceneParams applyModulation(const SceneParams& p) const;
    void reset();

    // For tests.
    int shuffles() const { return shuffles_; }
    int deals() const { return deals_; }
    float offset(ParamSlot slot) const { return offset_[static_cast<size_t>(slot)]; }
    bool holds(ParamSlot slot) const { return overridden_[static_cast<size_t>(slot)]; }
    bool leftAlone(ParamSlot slot) const { return left_[static_cast<size_t>(slot)]; }
    const std::array<LfoConfig, kWaves>& waves() const { return in_.waves; }
    const std::array<ReactRoute, kFollowers>& followers() const { return in_.followers; }
    // How much of modulator i (waves, then followers) reaches the picture now, 0..1.
    float modulatorMix(int i) const { return mixIn_[static_cast<size_t>(i)]; }

private:
    static constexpr int kFields = SceneParams::kFieldCount;
    using Wire = std::array<float, kFields>;
    using Slots = std::bitset<kFields>;
    struct Deal {
        std::array<LfoConfig, kWaves> waves{};
        std::array<ReactRoute, kFollowers> followers{};
    };
    using Gains = std::array<float, kWaves + kFollowers>;

    void glideTo(int slot, float to, float seconds);
    void letGo(int slot);
    void letGoAll();
    void roll(const SceneParams& requested, const Wire& wire, float seconds);
    Deal deal(int commit, float energy) const;
    void stepModulation(const GeodeSoundFrame& sound, float dt, const Inputs& in);

    bool primed_ = false;
    std::string style_;
    std::uint64_t seed_ = 0;
    std::shared_ptr<const director::Kept> kept_;
    Slots keptSlots_;
    float weight_ = 0.0f;

    // The shuffle. Offsets glide from_ → to_; a toggle or a choice holds override_ while overridden_.
    Wire last_{};
    Wire from_{};
    Wire to_{};
    Wire progress_{};
    Wire seconds_{};
    Wire offset_{};
    Wire override_{};
    Slots overridden_;
    Slots left_;
    Slots home_;
    bool anyShuffle_ = false;
    bool hasMoment_ = false;
    int momentCommit_ = 0;
    float momentStrength_ = 0.0f;
    int shuffles_ = 0;

    // The modulators: the playing deal fades out while the new one fades in, like the director's routes.
    Deal out_{};
    Deal in_{};
    LfoEngine wavesOut_;
    LfoEngine wavesIn_;
    ReactionEngine followOut_;
    ReactionEngine followIn_;
    std::array<float, kWaves> waveOutValues_{};
    std::array<float, kWaves> waveInValues_{};
    Gains gainOut_{};
    Gains gainIn_{};
    Gains mixOut_{};
    Gains mixIn_{};
    float cross_ = 1.0f;
    bool anyModulation_ = false;
    bool dealt_ = false;
    bool pending_ = false;
    bool pendingEmpty_ = false;
    int pendingCommit_ = 0;
    float pendingEnergy_ = 0.5f;
    int deals_ = 0;
};

}  // namespace geode::viz
