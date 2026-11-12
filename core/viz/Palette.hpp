#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "viz/Colour.hpp"
#include "viz/Params.hpp"

// Customize › Look's palettes: the one colour source every style kind will draw from (owner rule: once
// the user picks palettes, no other colours appear). A Palette is up to 16 colours with its gradient
// options; a Stack is the look's palettes, the primary first, which compose() turns into one ramp of
// colours over t in 0..1. The look keeps its stack by value as one opaque string (encode/decode), so a
// look is self-contained for export, share and backup and Kotlin parses and decides nothing (ADR 0003).
// How much each palette is used is not in the stack: those are modulatable scene params (one amount per
// layer), handed to compose() beside it. Pure: no GL, no JNI.
namespace geode::viz::palette {

// Bumped whenever a row type or field changes meaning; decode answers the unset stack to any other.
constexpr int kTextVersion = 1;
constexpr int kMaxStops = 16;
// The primary plus seven.
constexpr int kMaxLayers = 8;
// UTF-8 bytes, cut on a character boundary.
constexpr int kMaxNameBytes = 40;
// A style id or a .milk file name.
constexpr int kMaxRefBytes = 160;
// What the renderer uploads: one texel per entry.
constexpr int kRampSize = 256;

// Smooth blends from colour to colour; Steps paints each stop's colour as a band, the nearest stop wins.
enum class Interp { Smooth, Steps };
// Mirror runs the stops forward over the first half of t and back over the second (A..Z..A, so a wrapped
// t shows no seam); Loop runs them once and blends the last back into the first.
enum class Wrap { Mirror, Loop };
// Where Smooth blends: straight RGB (what the old ramps did), the hue wheel's short way round, or OKLab,
// whose even steps look even.
enum class Space { Rgb, Hue, Oklab };
// How a palette of one colour draws: its shades from dark to light, the colour flat, or colour to black.
enum class Single { Shades, Flat, ToBlack };
// Where a palette came from. ref names it: a library id for BuiltIn and ColourMap, a catalogue style id
// for Style (a style's own tint, its default palette), a .milk file name for Milk (derived from the
// preset), nothing for Artwork and Yours.
enum class Source { BuiltIn, ColourMap, Style, Milk, Artwork, Yours };
// How the stack's palettes share the ramp. Share: each takes its amount's share of t, in stack order, so
// every colour stays as picked. Mix: every palette is drawn over the whole of t and blended by amount
// (what an old look's second palette did).
enum class Blend { Share, Mix };
// Where two palettes meet under Share. Soft: the colours fade into each other across the line, over at most
// kSoftReach of t each way and never more than a quarter of either share, so a small share keeps its middle
// pure. Hard: each pure up to a sharp line.
enum class Edge { Soft, Hard };
constexpr float kSoftReach = 0.05f;

struct Stop {
    int rgb = 0;
    // Where along the palette, 0..1. The stops keep the order the user made them in; drawing sorts a copy.
    float pos = 0.0f;
    bool operator==(const Stop&) const = default;
};

struct Palette {
    std::string name;
    Source source = Source::Yours;
    std::string ref;
    std::vector<Stop> stops;
    Interp interp = Interp::Smooth;
    Wrap wrap = Wrap::Mirror;
    Space space = Space::Oklab;
    Single single = Single::Shades;
    bool operator==(const Palette&) const = default;
};

struct Stack {
    // The primary first. Empty is unset: the style's own palette draws (resolveDefault).
    std::vector<Palette> layers;
    Blend blend = Blend::Share;
    Edge edge = Edge::Soft;
    // Off: colour cycles, hue shift and moment colours walk the stack's colours only. On (the user's
    // opt-in): they may turn the whole hue wheel.
    bool fullRange = false;
    bool operator==(const Stack&) const = default;
};

using Amounts = std::array<float, kMaxLayers>;
// A look's amounts: its paletteAmount0..7 rows, the primary first.
Amounts amountsOf(const SceneParams& p);
// The amount rows' defaults: the primary at 1, every added palette at 0.5.
Amounts defaultAmounts();

const char* name(Interp v);
const char* name(Wrap v);
const char* name(Space v);
const char* name(Single v);
const char* name(Source v);
const char* name(Blend v);
const char* name(Edge v);

// Even positions for n stops: i/(n-1) for Mirror, so the run reaches both ends; i/n for Loop, so the
// last stop's blend back into the first is one step like the others.
std::vector<float> spread(int n, Wrap wrap);

// Names cleaned, at most kMaxStops valid colours (a palette with none is not one: valid() is false),
// positions finite and clamped to 0..1.
Palette normalized(Palette p);
bool valid(const Palette& p);
Stack normalized(Stack s);

// A palette from colours, in order (invalid ones skipped, the first kMaxStops kept), evenly spread, the
// defaults otherwise. The one door for a palette made of colours: your colours, the artwork's, a style's
// own tint, a .milk preset's. Empty stops when no colour was valid.
Palette fromColours(Source source, std::string_view ref, std::string_view name, const int* rgb, int n);

// The palette at t (wrapped into 0..1) by its wrap, interp and space. One stop draws by `single`.
colour::Rgb at(const Palette& p, float t);
// The palette's own run, u in 0..1 from its first stop to its last, ignoring the wrap: the editor's swatch.
colour::Rgb along(const Palette& p, float u);

// The stack at t: Share or Mix of its layers by amount (non-finite reads 0, clamped to 0..1; all at 0
// draws the primary alone). An unset stack draws unsetFallback().
colour::Rgb at(const Stack& s, const Amounts& amounts, float t);
// `samples` colours of the stack at texel centres, t = (i + 0.5) / samples: what the ramp texture holds.
std::vector<colour::Rgb> compose(const Stack& s, const Amounts& amounts, int samples = kRampSize);

// Unset: the style's default draws. resolveDefault puts `styleDefault` in as the only layer of an unset
// stack and leaves a set one alone: a default, never a lock.
bool isUnset(const Stack& s);
Stack resolveDefault(const Stack& s, const Palette& styleDefault);
// What an unset stack draws until the styles' own palettes land: the classic Spectrum ramp as a look saved
// on the default palette drew it.
const Palette& unsetFallback();

// Pure edits: each returns the edited, normalized value; an index out of range changes nothing.
Stack addLayer(Stack s, const Palette& p);
Stack removeLayer(Stack s, int layer);
Stack moveLayer(Stack s, int layer, int delta);
Stack setLayer(Stack s, int layer, const Palette& p);
// pos < 0 keeps the stop's place.
Palette setStop(Palette p, int stop, int rgb, float pos);
// pos < 0 appends after the last stop and spreads every stop evenly again.
Palette addStop(Palette p, int rgb, float pos);
// The last stop is never removed.
Palette removeStop(Palette p, int stop);
// One of the option names above (SMOOTH, STEPS, MIRROR, LOOP, RGB, HUE, OKLAB, SHADES, FLAT, TO_BLACK);
// false for any other. Changing the wrap respreads stops that sat evenly for the old one.
bool setOption(Palette& p, std::string_view option);
Palette spreadEvenly(Palette p);

// Text. A palette is one line:
//   palette|<SOURCE>|<ref>|<name>|<SMOOTH|STEPS>|<MIRROR|LOOP>|<RGB|HUE|OKLAB>|<SHADES|FLAT|TO_BLACK>|<stops>
// stops "RRGGBB@pos" joined by ','. A stack is a header and its layers, the primary first:
//   palettes|<kTextVersion>|<SHARE|MIX>|<fullRange 0|1>|<SOFT|HARD>
//   palette|...
// decode drops what it cannot read; an unreadable header is the unset stack.
std::string encode(const Palette& p);
// An invalid palette (no stops) when the line is not one.
Palette decodePalette(std::string_view line);
std::string encode(const Stack& s);
Stack decode(std::string_view text);

// The built-in library: rows 0..20 the old `palette` hue ramps by their index, 21..25 the old colour maps
// (`paletteLut` 0..4), then the newer built-ins. Built once; the reference lives for the process.
struct Entry {
    std::string id;
    Palette palette;
};
const std::vector<Entry>& library();
int libraryIndex(std::string_view id);
constexpr int kClassicCount = 21;
constexpr int kColourMapFirst = 21;
constexpr int kColourMapCount = 5;
// The library as the shell lists it (no colours: geode_palette_library_entry has those):
//   library|<kTextVersion>|<count>
//   entry|<index>|<id>|<SOURCE>|<name>|<INTERP>|<WRAP>|<SPACE>|<SINGLE>|<stop count>
const std::string& libraryText();

// An old look's colours as a stack, so it draws as it did: the first slot from its stops (your colours),
// else its hue gradient (a saved override or built-in `palette`) sampled to stops; a second palette under
// `paletteMix` as layer 2 with blend Mix; a colour map (`paletteLut`), which replaced both, as the only
// layer. Every layer Loops, as the old ramps did, and palettes meet Hard. `amounts` gets the layers' amounts.
Stack fromLegacy(const SceneParams& p, Amounts& amounts);
// The old cosine ramp (base, range) as kMaxStops stops, in RGB, looped: at i/kMaxStops for a ramp of whole
// turns, which meets itself; else from base to base + range inclusive, so t just short of 1 is the ramp's end.
Palette fromHueRamp(std::string_view name, Source source, std::string_view ref, float base, float range);

}  // namespace geode::viz::palette
