#include "viz/MacroBank.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "viz/Catalogue.hpp"
#include "viz/ModTargets.hpp"
#include "viz/TableNumber.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz::macrobank {

namespace {

constexpr int kTargetCount = modtargets::kTargetCount;
static_assert(kTargetCount <= 64, "reachMask is a bit per target");
// A raw text may repeat a target; normalized keeps one link each, so parsing stops well short of a flood.
constexpr size_t kMaxParsedLinks = 64;

struct TargetInfo {
    int slot = -1;
    float lo = 0.0f;
    float hi = 0.0f;
    bool circular = false;
};

std::array<TargetInfo, kTargetCount> buildInfos() {
    std::array<TargetInfo, kTargetCount> out{};
    for (int i = 0; i < kTargetCount; ++i) {
        const auto target = static_cast<LfoTarget>(i);
        const int slot = paramSlotOf(target);
        if (slot < 0 || !modtargets::offered(target)) continue;
        const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(slot)];
        SceneParams top;
        top.setSlot(slot, spec.max);
        SceneParams bottom;
        bottom.setSlot(slot, spec.min);
        TargetInfo& info = out[static_cast<size_t>(i)];
        info.slot = slot;
        info.lo = std::max(spec.min, safety::apply(bottom).getSlot(slot));
        info.hi = std::min(spec.max, safety::apply(top).getSlot(slot));
        info.circular = spec.kind == ParamKind::Circular;
    }
    return out;
}

const std::array<TargetInfo, kTargetCount>& infos() {
    static const std::array<TargetInfo, kTargetCount> kInfos = buildInfos();
    return kInfos;
}

const TargetInfo* infoOf(LfoTarget target) {
    const int i = static_cast<int>(target);
    if (i < 0 || i >= kTargetCount) return nullptr;
    const TargetInfo& info = infos()[static_cast<size_t>(i)];
    return info.slot >= 0 ? &info : nullptr;
}

float unit(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }

constexpr std::array<const char*, kCurveCount> kCurveNames = {"LINEAR", "EASE_IN", "EASE_OUT", "S_CURVE"};

Curve validCurve(Curve curve) {
    const int i = static_cast<int>(curve);
    return i >= 0 && i < kCurveCount ? curve : Curve::Linear;
}

// Text the codec can carry and a label can show: no control characters, no field separator, no
// outer spaces, at most maxBytes without splitting a UTF-8 character.
std::string cleanText(std::string_view raw, size_t maxBytes) {
    std::string out;
    for (const char ch : raw) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f || ch == '|') continue;
        out += ch;
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    out.erase(0, first);
    if (out.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    out.erase(out.find_last_not_of(' ') + 1);
    return out;
}

Knob* knobById(Bank& bank, int id) {
    for (Knob& knob : bank.knobs) {
        if (knob.id == id) return &knob;
    }
    return nullptr;
}

std::vector<Link>::iterator linkOn(Knob& knob, LfoTarget target) {
    return std::find_if(knob.links.begin(), knob.links.end(), [target](const Link& l) { return l.target == target; });
}

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    size_t pos = 0;
    while (true) {
        const size_t bar = line.find('|', pos);
        if (bar == std::string_view::npos) {
            out.push_back(line.substr(pos));
            return out;
        }
        out.push_back(line.substr(pos, bar - pos));
        pos = bar + 1;
    }
}

bool parseInt(std::string_view s, int& out) {
    const char* end = s.data() + s.size();
    const auto result = std::from_chars(s.data(), end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseFloat(std::string_view s, float& out) {
    if (s.empty() || s.size() > 31) return false;
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

struct Seed {
    const char* name;
    const char* hint;
    std::vector<Link> links;
};

// The built-in knobs: four jobs that do not overlap (no target is on two knobs), its shape, its motion,
// its colour and its depth. Each spends a full turn on params every engine shows strongly (the view's
// geometry, the clocks and the camera, a hue turn, zoom and the lens), not on the ones the safety clamp
// keeps small (Bloom and Intensity stop at 0.25 over neutral, so a knob made of them barely shows), and
// most of each param's range, so a full turn is a different picture on every style. Weights are
// fractions of rangeOf, which already stops at the safety maxima; the motion fields are the ones
// safety::apply scales under reduced motion. core/tests/macro_bank_test.cpp pins their strength.
const std::vector<Seed>& seeds() {
    static const std::vector<Seed> kSeeds = {
        {"Shape", "Down twists the picture into mirrored tiles, up melts, warps and twists it",
         {{LfoTarget::Morph, 0.9f, 0.0f},
          {LfoTarget::Warp, 0.8f, 0.0f},
          {LfoTarget::Turbulence, 0.5f, 0.0f},
          {LfoTarget::Twist, 0.6f, -0.6f},
          {LfoTarget::Tile, 0.0f, 0.5f}}},
        {"Motion", "Down slows the picture and stills the camera, up speeds, sways and spins it",
         {{LfoTarget::Speed, 0.5f, -0.22f},
          {LfoTarget::Sway, 0.6f, 0.0f},
          {LfoTarget::Rotation, 0.08f, 0.0f},
          {LfoTarget::CamFly, 0.3f, -0.1f},
          {LfoTarget::CamOrbit, 0.3f, -0.35f},
          {LfoTarget::FluidForce, 0.4f, -0.25f}}},
        {"Colour", "Down turns the colours cool and dim, up warm, vivid and glowing",
         {{LfoTarget::ColorShift, 0.35f, -0.35f},
          {LfoTarget::Temperature, 0.5f, -0.5f},
          {LfoTarget::Saturation, 0.25f, 0.0f},
          {LfoTarget::PaletteMix, 0.7f, 0.0f},
          {LfoTarget::Bloom, 1.0f, 0.0f},
          {LfoTarget::Intensity, 0.2f, -0.3f}}},
        {"Depth", "Down pulls in close, up opens a wide, deep view",
         {{LfoTarget::Zoom, -0.22f, 0.35f},
          {LfoTarget::CamFov, 0.4f, -0.25f},
          {LfoTarget::Fisheye, 0.35f, -0.35f},
          {LfoTarget::CamPush, -0.4f, 0.4f},
          {LfoTarget::Vignette, 0.0f, 0.5f}}},
    };
    return kSeeds;
}

// The built-in knobs as they first shipped (Glow, Heat, Bend, Flow, #1001), by id. A saved knob that still
// matches one exactly (the user turned it but never edited it) is that built-in knob, not the user's
// own, so decode hands it back as the knob that now has its id, centred: its old turn was a turn of a
// different job. Anything the user edited stays as they left it.
const std::vector<Seed>& retiredSeeds() {
    static const std::vector<Seed> kRetired = {
        {"Glow", "Down dims the picture into a dark frame, up makes it bright and blooming",
         {{LfoTarget::Bloom, 1.0f, 0.0f}, {LfoTarget::Intensity, 0.2f, -0.5f}, {LfoTarget::Vignette, 0.0f, 0.5f}}},
        {"Heat", "Down turns the colours cool and pale, up warm and vivid",
         {{LfoTarget::Temperature, 0.3f, -0.3f}, {LfoTarget::Saturation, 0.2f, -0.35f}, {LfoTarget::ColorShift, 0.06f, -0.06f}}},
        {"Bend", "Down pinches and unwinds the shape, up warps, twists and bulges it",
         {{LfoTarget::Warp, 0.6f, 0.0f},
          {LfoTarget::Twist, 0.2f, -0.2f},
          {LfoTarget::Morph, 0.5f, 0.0f},
          {LfoTarget::Fisheye, 0.15f, -0.25f}}},
        {"Flow", "Down slows the picture and turns it back, up speeds it up and sends it spinning",
         {{LfoTarget::Speed, 0.25f, -0.15f},
          {LfoTarget::Rotation, 0.03f, -0.03f},
          {LfoTarget::Sway, 0.4f, 0.0f},
          {LfoTarget::Turbulence, 0.4f, 0.0f},
          {LfoTarget::CamFly, 0.2f, -0.1f},
          {LfoTarget::FluidForce, 0.3f, -0.2f}}},
    };
    return kRetired;
}

Bank buildDefault() {
    Bank bank;
    for (const Seed& seed : seeds()) {
        Knob knob;
        knob.id = bank.nextId++;
        knob.name = seed.name;
        knob.hint = seed.hint;
        knob.links = seed.links;
        bank.knobs.push_back(std::move(knob));
    }
    return normalized(bank);
}

const Seed* seedOf(int id) {
    return id >= 1 && id <= static_cast<int>(seeds().size()) ? &seeds()[static_cast<size_t>(id - 1)] : nullptr;
}

std::vector<Knob>::iterator findKnob(Bank& bank, int id) {
    return std::find_if(bank.knobs.begin(), bank.knobs.end(), [id](const Knob& k) { return k.id == id; });
}

const Knob* knobIn(const Bank& bank, int id) {
    for (const Knob& knob : bank.knobs) {
        if (knob.id == id) return &knob;
    }
    return nullptr;
}

// A knob's definition (what Reset puts back) against the built-in one: its value is the user's turn, not part of it.
bool asBuilt(const Knob& knob) {
    const Knob* built = nullptr;
    for (const Knob& k : defaultBank().knobs) {
        if (k.id == knob.id) built = &k;
    }
    return built && knob.name == built->name && knob.hint == built->hint && knob.links == built->links;
}

const char* flag(bool on) { return on ? "1" : "0"; }

// An offset in a param's units for the editor to show: four significant digits, no float noise.
std::string units(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g", static_cast<double>(v) == 0.0 ? 0.0 : static_cast<double>(v));
    return buf;
}

std::mutex gLock;
std::shared_ptr<const Bank> gBank;  // guarded by gLock; null is the default bank

std::string buildTargetsText() {
    std::string out = "macrotargets|" + std::to_string(kTextVersion) + "\n";
    out += "caps|" + std::to_string(kMaxKnobs) + "|" + std::to_string(kMaxLinks) + "|" + std::to_string(kMaxNameBytes) + "\n";
    for (int i = 0; i < kTargetCount; ++i) {
        const auto target = static_cast<LfoTarget>(i);
        const TargetInfo* info = infoOf(target);
        if (!info) continue;
        const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(info->slot)];
        out += std::string("target|") + modtargets::name(target) + "|" + spec.name + "|" + tableNumber(info->lo) + "|" +
               tableNumber(info->hi) + "|" + tableNumber(spec.defaultValue) + "|" + catalogue::name(modtargets::scopeOf(target)) +
               "\n";
    }
    return out;
}

}  // namespace

const char* name(Curve curve) { return kCurveNames[static_cast<size_t>(validCurve(curve))]; }

std::optional<Curve> curveNamed(std::string_view curveName) {
    for (int i = 0; i < kCurveCount; ++i) {
        if (curveName == kCurveNames[static_cast<size_t>(i)]) return static_cast<Curve>(i);
    }
    return std::nullopt;
}

float shape(Curve curve, float t) {
    const float x = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    switch (validCurve(curve)) {
        case Curve::EaseIn:
            return x * x;
        case Curve::EaseOut:
            return x * (2.0f - x);
        case Curve::SCurve:
            return x * x * (3.0f - 2.0f * x);
        case Curve::Linear:
            break;
    }
    return x;
}

bool linkable(LfoTarget target) { return infoOf(target) != nullptr; }

Range rangeOf(LfoTarget target) {
    const TargetInfo* info = infoOf(target);
    return info ? Range{info->lo, info->hi} : Range{0.0f, 0.0f};
}

LfoTarget targetNamed(std::string_view name) {
    for (int i = 0; i < kTargetCount; ++i) {
        const auto target = static_cast<LfoTarget>(i);
        if (linkable(target) && name == modtargets::name(target)) return target;
    }
    return LfoTarget::None;
}

const Bank& defaultBank() {
    static const Bank kDefault = buildDefault();
    return kDefault;
}

bool isBase(int id) { return seedOf(id) != nullptr; }

Bank normalized(const Bank& bank) {
    Bank out;
    int maxId = 0;
    int onScreen = 0;
    for (const Knob& in : bank.knobs) {
        if (static_cast<int>(out.knobs.size()) >= kMaxKnobs) break;
        if (in.id < 1 || in.id > kMaxId || knobById(out, in.id)) continue;
        Knob knob;
        knob.id = in.id;
        knob.name = cleanText(in.name, kMaxNameBytes);
        if (knob.name.empty()) knob.name = "Knob " + std::to_string(in.id);
        knob.hint = cleanText(in.hint, kMaxHintBytes);
        knob.shown = in.shown && onScreen < kMaxShown;
        knob.value = knob.shown ? unit(in.value) : 0.0f;
        if (knob.shown) ++onScreen;
        for (const Link& link : in.links) {
            if (!linkable(link.target)) continue;
            const Link fixed{link.target, unit(link.up), unit(link.down), validCurve(link.curve)};
            const auto same = linkOn(knob, link.target);
            if (same != knob.links.end()) {
                *same = fixed;
            } else if (static_cast<int>(knob.links.size()) < kMaxLinks) {
                knob.links.push_back(fixed);
            }
        }
        maxId = std::max(maxId, knob.id);
        out.knobs.push_back(std::move(knob));
    }
    out.nextId = std::clamp(std::max(bank.nextId, maxId + 1), 1, kMaxId + 1);
    return out;
}

int shownCount(const Bank& bank) {
    return static_cast<int>(std::count_if(bank.knobs.begin(), bank.knobs.end(), [](const Knob& k) { return k.shown; }));
}

bool roomForNew(const Bank& bank) {
    const Bank valid = normalized(bank);
    return static_cast<int>(valid.knobs.size()) < kMaxKnobs && shownCount(valid) < kMaxShown && valid.nextId <= kMaxId;
}

Bank setValue(const Bank& bank, int id, float value) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    if (knob && knob->shown && std::isfinite(value)) knob->value = unit(value);
    return out;
}

Bank rename(const Bank& bank, int id, std::string_view name) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    const std::string cleaned = cleanText(name, kMaxNameBytes);
    if (!knob || cleaned.empty() || cleaned == knob->name) return out;
    knob->name = cleaned;
    knob->hint.clear();
    return out;
}

Bank addKnob(const Bank& bank, std::string_view name) {
    Bank out = normalized(bank);
    if (!roomForNew(out)) return out;
    Knob knob;
    knob.id = out.nextId++;
    knob.name = cleanText(name, kMaxNameBytes);
    if (knob.name.empty()) knob.name = "Knob " + std::to_string(knob.id);
    out.knobs.push_back(std::move(knob));
    return out;
}

Bank duplicateKnob(const Bank& bank, int id) {
    Bank out = normalized(bank);
    const auto at = findKnob(out, id);
    if (at == out.knobs.end() || !roomForNew(out)) return out;
    Knob copy = *at;
    copy.shown = true;
    copy.id = out.nextId++;
    copy.name = cleanText(cleanText(at->name, kMaxNameBytes - 2) + " 2", kMaxNameBytes);
    copy.hint.clear();
    out.knobs.insert(at + 1, std::move(copy));
    return out;
}

Bank moveKnob(const Bank& bank, int id, int delta) {
    Bank out = normalized(bank);
    const auto at = findKnob(out, id);
    if (at == out.knobs.end() || !at->shown) return out;
    // Steps count the knobs on screen only, so a move is always one the screen shows.
    std::vector<int> screen;
    for (const Knob& knob : out.knobs) {
        if (knob.shown) screen.push_back(knob.id);
    }
    const int from = static_cast<int>(std::find(screen.begin(), screen.end(), id) - screen.begin());
    const int to =
        static_cast<int>(std::clamp<long long>(static_cast<long long>(from) + delta, 0, static_cast<long long>(screen.size()) - 1));
    if (to == from) return out;
    Knob moving = std::move(*at);
    out.knobs.erase(at);
    const auto beside = findKnob(out, screen[static_cast<size_t>(to)]);
    out.knobs.insert(to < from ? beside : beside + 1, std::move(moving));
    return out;
}

Bank removeKnob(const Bank& bank, int id) {
    Bank out = normalized(bank);
    if (isBase(id)) return out;
    std::erase_if(out.knobs, [id](const Knob& knob) { return knob.id == id; });
    return out;
}

Bank hideKnob(const Bank& bank, int id) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    if (!knob || !knob->shown) return out;
    knob->shown = false;
    knob->value = 0.0f;
    return out;
}

Bank showKnob(const Bank& bank, int id) {
    Bank out = normalized(bank);
    if (shownCount(out) >= kMaxShown) return out;
    const auto at = findKnob(out, id);
    Knob knob;
    if (at != out.knobs.end()) {
        if (at->shown) return out;
        knob = std::move(*at);
        out.knobs.erase(at);
    } else {
        const Seed* seed = seedOf(id);
        if (!seed || static_cast<int>(out.knobs.size()) >= kMaxKnobs) return out;
        knob.id = id;
        knob.name = seed->name;
        knob.hint = seed->hint;
        knob.links = seed->links;
    }
    knob.shown = true;
    knob.value = 0.0f;
    out.knobs.push_back(std::move(knob));
    return normalized(out);
}

Bank resetKnob(const Bank& bank, int id) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    const Seed* seed = seedOf(id);
    if (!knob || !seed) return out;
    knob->name = seed->name;
    knob->hint = seed->hint;
    knob->links = seed->links;
    return normalized(out);
}

Bank setLink(const Bank& bank, int id, LfoTarget target, float up, float down) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    if (!knob || !linkable(target) || !std::isfinite(up) || !std::isfinite(down)) return out;
    Link link{target, unit(up), unit(down)};
    const auto same = linkOn(*knob, target);
    if (same != knob->links.end()) {
        link.curve = same->curve;
        if (*same == link) return out;
        *same = link;
    } else {
        if (static_cast<int>(knob->links.size()) >= kMaxLinks) return out;
        knob->links.push_back(link);
    }
    knob->hint.clear();
    return out;
}

Bank addLink(const Bank& bank, int id, LfoTarget target) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    if (!knob || linkOn(*knob, target) != knob->links.end()) return out;
    return setLink(out, id, target, kNewLinkWeight, -kNewLinkWeight);
}

Bank removeLink(const Bank& bank, int id, LfoTarget target) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    if (!knob) return out;
    const auto same = linkOn(*knob, target);
    if (same == knob->links.end()) return out;
    knob->links.erase(same);
    knob->hint.clear();
    return out;
}

namespace {

std::uint64_t splitmix(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float between(std::uint64_t& state, float lo, float hi) {
    return lo + (hi - lo) * static_cast<float>(splitmix(state) >> 40) / static_cast<float>(std::uint64_t{1} << 24);
}

}  // namespace

Bank roll(const Bank& bank, int id, std::uint64_t seed) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    if (!knob) return out;
    std::vector<LfoTarget> pool;
    for (int i = 0; i < kTargetCount; ++i) {
        const TargetInfo& info = infos()[static_cast<size_t>(i)];
        if (info.slot >= 0 && info.hi > info.lo) pool.push_back(static_cast<LfoTarget>(i));
    }
    std::uint64_t state = seed;
    const int count = std::min({2 + static_cast<int>(splitmix(state) % 4), kMaxLinks, static_cast<int>(pool.size())});
    std::vector<Link> links;
    for (int n = 0; n < count; ++n) {
        const size_t pick = static_cast<size_t>(splitmix(state) % pool.size());
        const LfoTarget target = pool[pick];
        pool.erase(pool.begin() + static_cast<long>(pick));
        const float sign = (splitmix(state) & 1u) ? 1.0f : -1.0f;
        const float up = sign * between(state, 0.15f, 0.6f);
        const float down = (splitmix(state) & 1u) ? -up * between(state, 0.5f, 1.0f) : 0.0f;
        const auto curve = static_cast<Curve>(splitmix(state) % kCurveCount);
        links.push_back({target, up, down, curve});
    }
    knob->links = std::move(links);
    knob->hint.clear();
    return normalized(out);
}

Bank rollValues(const Bank& bank, std::uint64_t reach, std::uint64_t seed) {
    Bank out = normalized(bank);
    for (Knob& knob : out.knobs) {
        if (!knob.shown) continue;
        // Each knob's draw comes from the seed and its id alone, so one knob's reach never shifts another's turn.
        std::uint64_t state = seed ^ (static_cast<std::uint64_t>(knob.id) * 0xD1B54A32D192ED03ull);
        const bool upward = (splitmix(state) & 1u) != 0;
        const float amount = between(state, kRollMin, 1.0f);
        bool up = false;
        bool down = false;
        for (const Link& link : knob.links) {
            if (((reach >> static_cast<int>(link.target)) & std::uint64_t{1}) == 0) continue;
            up = up || link.up != 0.0f;
            down = down || link.down != 0.0f;
        }
        if (!up && !down) continue;
        knob.value = (up && down ? upward : up) ? amount : -amount;
    }
    return out;
}

Bank rollValues(const Bank& bank, const std::string& styleId, std::uint64_t seed) {
    return rollValues(bank, reachMask(styleId), seed);
}

Bank setCurve(const Bank& bank, int id, LfoTarget target, Curve curve) {
    Bank out = normalized(bank);
    Knob* knob = knobById(out, id);
    const int c = static_cast<int>(curve);
    if (!knob || c < 0 || c >= kCurveCount) return out;
    const auto same = linkOn(*knob, target);
    if (same == knob->links.end() || same->curve == curve) return out;
    same->curve = curve;
    knob->hint.clear();
    return out;
}

std::uint64_t reachMask(const std::string& styleId) {
    std::uint64_t bits = 0;
    for (int i = 0; i < kTargetCount; ++i) {
        const auto target = static_cast<LfoTarget>(i);
        if (linkable(target) && modtargets::reaches(target, styleId)) bits |= std::uint64_t{1} << i;
    }
    return bits;
}

SceneParams apply(const SceneParams& p, const Bank& bank, std::uint64_t reach) {
    std::array<float, kTargetCount> offset{};
    std::uint64_t moved = 0;
    int knobs = 0;
    for (const Knob& knob : bank.knobs) {
        if (knobs++ >= kMaxKnobs) break;
        if (!knob.shown || !std::isfinite(knob.value) || knob.value == 0.0f) continue;
        const float value = std::clamp(knob.value, -1.0f, 1.0f);
        for (const Link& link : knob.links) {
            const TargetInfo* info = infoOf(link.target);
            if (!info) continue;
            const int t = static_cast<int>(link.target);
            if (((reach >> t) & std::uint64_t{1}) == 0) continue;
            const float weight = shape(link.curve, std::fabs(value)) * (value >= 0.0f ? link.up : link.down);
            if (!std::isfinite(weight) || weight == 0.0f) continue;
            offset[static_cast<size_t>(t)] += std::clamp(weight, -1.0f, 1.0f) * (info->hi - info->lo);
            moved |= std::uint64_t{1} << t;
        }
    }
    if (moved == 0) return p;
    SceneParams out = p;
    for (int t = 0; t < kTargetCount; ++t) {
        const float add = offset[static_cast<size_t>(t)];
        if (((moved >> t) & std::uint64_t{1}) == 0 || add == 0.0f) continue;
        const TargetInfo& info = infos()[static_cast<size_t>(t)];
        const float base = p.getSlot(info.slot);
        if (!std::isfinite(base)) continue;
        out.setSlot(info.slot, info.circular ? base + add : std::clamp(base + add, info.lo, info.hi));
    }
    return out;
}

void setGlobal(const Bank& bank) {
    auto next = std::make_shared<const Bank>(normalized(bank));
    std::lock_guard<std::mutex> guard(gLock);
    gBank = std::move(next);
}

std::shared_ptr<const Bank> global() {
    {
        std::lock_guard<std::mutex> guard(gLock);
        if (gBank) return gBank;
    }
    static const std::shared_ptr<const Bank> kDefault = std::make_shared<const Bank>(defaultBank());
    return kDefault;
}

SceneParams apply(const SceneParams& p, const Bank& bank, const std::string& styleId) {
    return apply(p, bank, reachMask(styleId));
}

std::string encode(const Bank& bank) {
    const Bank b = normalized(bank);
    std::string out = "macrobank|" + std::to_string(kTextVersion) + "|" + std::to_string(b.nextId) + "\n";
    for (const Knob& knob : b.knobs) {
        out += "knob|" + std::to_string(knob.id) + "|" + tableNumber(knob.value) + "|" + knob.name + "|" + knob.hint + "\n";
        if (!knob.shown) out += "shelf|" + std::to_string(knob.id) + "\n";
        for (const Link& link : knob.links) {
            out += std::string("link|") + modtargets::name(link.target) + "|" + tableNumber(link.up) + "|" + tableNumber(link.down);
            if (link.curve != Curve::Linear) out += std::string("|") + name(link.curve);
            out += "\n";
        }
    }
    return out;
}

Bank decode(std::string_view text) {
    Bank bank;
    bool header = false;
    // Whether the last knob row was read, so the link rows under a dropped knob go with it.
    bool open = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const std::vector<std::string_view> f = fields(line);
        if (!header) {
            int version = 0;
            int next = 0;
            if (f.size() != 3 || f[0] != "macrobank" || !parseInt(f[1], version) || version != kTextVersion ||
                !parseInt(f[2], next)) {
                return defaultBank();
            }
            bank.nextId = next;
            header = true;
        } else if (f[0] == "knob") {
            open = false;
            Knob knob;
            if (f.size() != 5 || static_cast<int>(bank.knobs.size()) >= kMaxKnobs || !parseInt(f[1], knob.id) ||
                !parseFloat(f[2], knob.value)) {
                continue;
            }
            knob.name = std::string(f[3]);
            knob.hint = std::string(f[4]);
            bank.knobs.push_back(std::move(knob));
            open = true;
        } else if (f[0] == "link" && open) {
            Link link;
            if ((f.size() != 4 && f.size() != 5) || bank.knobs.back().links.size() >= kMaxParsedLinks) continue;
            link.target = targetNamed(f[1]);
            if (link.target == LfoTarget::None || !parseFloat(f[2], link.up) || !parseFloat(f[3], link.down)) continue;
            if (f.size() == 5) link.curve = curveNamed(f[4]).value_or(Curve::Linear);
            bank.knobs.back().links.push_back(link);
        } else if (f[0] == "shelf" && f.size() == 2) {
            int id = 0;
            if (!parseInt(f[1], id)) continue;
            for (Knob& knob : bank.knobs) {
                if (knob.id == id) knob.shown = false;
            }
        }
    }
    if (!header) return defaultBank();
    Bank out = normalized(bank);
    for (Knob& knob : out.knobs) {
        const auto& retired = retiredSeeds();
        if (knob.id < 1 || knob.id > static_cast<int>(retired.size())) continue;
        const Seed& was = retired[static_cast<size_t>(knob.id - 1)];
        const Seed* now = seedOf(knob.id);
        if (!now || knob.name != was.name || knob.hint != was.hint || knob.links != was.links) continue;
        knob.name = now->name;
        knob.hint = now->hint;
        knob.links = now->links;
        knob.value = 0.0f;
    }
    return normalized(out);
}

const std::string& targetsText() {
    static const std::string kText = buildTargetsText();
    return kText;
}

std::string liveText(const Bank& bank, const std::string& styleId) {
    const std::uint64_t reach = reachMask(styleId);
    const Bank valid = normalized(bank);
    std::string out;
    for (const Knob& knob : valid.knobs) {
        if (!knob.shown) continue;
        bool up = false;
        bool down = false;
        for (const Link& link : knob.links) {
            if (((reach >> static_cast<int>(link.target)) & std::uint64_t{1}) == 0) continue;
            up = up || link.up != 0.0f;
            down = down || link.down != 0.0f;
        }
        out += "knob|" + std::to_string(knob.id) + "|" + (up ? "1" : "0") + "|" + (down ? "1" : "0") + "\n";
    }
    return out;
}

std::string editorText(const Bank& bank, const std::string& styleId) {
    const std::uint64_t reach = reachMask(styleId);
    const auto reaches = [reach](LfoTarget target) { return ((reach >> static_cast<int>(target)) & std::uint64_t{1}) != 0; };
    const Bank valid = normalized(bank);
    bool pristine = valid.knobs.size() == defaultBank().knobs.size();
    for (size_t i = 0; pristine && i < valid.knobs.size(); ++i) {
        pristine = valid.knobs[i].id == defaultBank().knobs[i].id && valid.knobs[i].shown && asBuilt(valid.knobs[i]);
    }
    std::string out = "macroeditor|" + std::to_string(kTextVersion) + "\n";
    out += "caps|" + std::to_string(kMaxShown) + "|" + std::to_string(kMaxLinks) + "|" + std::to_string(kMaxNameBytes) + "\n";
    out += std::string("bank|") + flag(roomForNew(valid)) + "|" + flag(pristine) + "\n";
    out += std::string("screen|") + flag(shownCount(valid) < kMaxShown) + "\n";
    for (const Knob& knob : valid.knobs) {
        if (!knob.shown) continue;
        out += "knob|" + std::to_string(knob.id) + "|" + flag(isBase(knob.id)) + "|" + flag(asBuilt(knob)) + "|" +
               flag(static_cast<int>(knob.links.size()) < kMaxLinks) + "|" + tableNumber(knob.value) + "|" + knob.name + "|" +
               knob.hint + "\n";
        for (const Link& link : knob.links) {
            const Range range = rangeOf(link.target);
            const float width = range.max - range.min;
            out += std::string("link|") + modtargets::name(link.target) + "|" + tableNumber(link.up) + "|" + tableNumber(link.down) +
                   "|" + flag(reaches(link.target)) + "|" + name(link.curve) + "|" + units(link.up * width) + "|" +
                   units(link.down * width) + "\n";
        }
    }
    for (int i = 0; i < kTargetCount; ++i) {
        const auto target = static_cast<LfoTarget>(i);
        if (linkable(target)) out += std::string("target|") + modtargets::name(target) + "|" + flag(reaches(target)) + "\n";
    }
    for (const char* curve : kCurveNames) out += std::string("curve|") + curve + "\n";
    const auto libraryRow = [&out](const Knob& knob) {
        out += "library|" + std::to_string(knob.id) + "|" + flag(isBase(knob.id)) + "|" + knob.name + "|" + knob.hint + "\n";
    };
    for (const Knob& built : defaultBank().knobs) {
        const Knob* knob = knobIn(valid, built.id);
        if (!knob) {
            libraryRow(built);
        } else if (!knob->shown) {
            libraryRow(*knob);
        }
    }
    for (const Knob& knob : valid.knobs) {
        if (!knob.shown && !isBase(knob.id)) libraryRow(knob);
    }
    return out;
}

}  // namespace geode::viz::macrobank
