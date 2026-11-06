// Host-side checks for the wallpaper's edge glow (core/viz/frame/EdgeGlowMath.hpp, #439). Built and run
// by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the ABI options parse (and a garbled call turns the glow off), the VU-meter envelope
// (fast attack, slow release, an ease-off while nothing new is heard, the same speed at 30 and 120 fps),
// the window's flip into GL's y-up frame, the band's width from rest to full level, and the black's
// weight across the frame: clear on the rim, black inside and outside the rounded corners.
#include <cmath>
#include <cstdio>
#include <limits>

#include "viz/frame/EdgeGlowMath.hpp"

namespace eg = geode::viz::frame::edge_glow;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

GeodeSoundFrame frameWithRms(float rms) {
    GeodeSoundFrame f{};
    f.rms = rms;
    return f;
}

void optionsParse() {
    const float on[eg::kFloats] = {1.0f, 10.0f, 20.0f, 1080.0f, 2400.0f};
    const eg::Options o = eg::parse(on, eg::kFloats);
    check(o.enabled, "enabled > 0.5 turns the glow on");
    check(o.left == 10.0f && o.top == 20.0f && o.width == 1080.0f && o.height == 2400.0f, "the window is read in order");
    check(!eg::parse(on, eg::kFloats - 1).enabled, "too few floats turn it off");
    check(!eg::parse(nullptr, eg::kFloats).enabled, "no floats turn it off");
    float bad[eg::kFloats] = {1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 100.0f, 100.0f};
    check(!eg::parse(bad, eg::kFloats).enabled, "a non-finite option turns it off");
    const float off[eg::kFloats] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    check(!eg::parse(off, eg::kFloats).enabled, "enabled 0 is off");
}

void envelopeAttacksFastReleasesSlow() {
    eg::Envelope env;
    const float dt = 1.0f / eg::kRefFps;
    // A loud frame: the target is clamped to 1 and half the gap closes in one reference frame.
    GeodeSoundFrame loud = frameWithRms(0.5f);
    check(near(eg::targetLevel(&loud), 1.0f), "a loud RMS clamps to a level of 1");
    env.step(&loud, dt);
    check(near(env.level, eg::kAttack), "one reference frame of attack closes kAttack of the gap");
    for (int i = 0; i < 30; ++i) {
        loud.rms = 0.5f + 0.001f * static_cast<float>(i + 1);  // a playing signal never repeats its RMS exactly
        env.step(&loud, dt);
    }
    check(env.level > 0.99f, "half a second of loud music holds the band wide");
    // Silence that is still fresh (a quiet passage): release is slow.
    GeodeSoundFrame quiet = frameWithRms(0.0001f);
    const float before = env.level;
    env.step(&quiet, dt);
    check(near(env.level, before * (1.0f - eg::kRelease), 1e-3f), "release closes only kRelease of the gap a frame");
    check(env.level > 0.9f, "one quiet frame barely narrows the band");
}

void envelopeEasesOffWhenNothingNewIsHeard() {
    eg::Envelope env;
    const float dt = 1.0f / eg::kRefFps;
    GeodeSoundFrame f = frameWithRms(0.2f);
    for (int i = 0; i < 20; ++i) {
        f.rms = 0.2f + 0.0001f * static_cast<float>(i);
        env.step(&f, dt);
    }
    const float playing = env.level;
    // Paused: the engine's RMS stops changing; the band must settle rather than hold.
    for (int i = 0; i < 600; ++i) env.step(&f, dt);
    check(playing > 0.5f, "a mastered level opens the band");
    check(env.level < 0.05f, "ten seconds without new sound let the band settle");
    eg::Envelope none;
    none.level = 0.8f;
    for (int i = 0; i < 600; ++i) none.step(nullptr, dt);
    check(none.level < 0.01f, "no sound frame at all is silence");
}

void envelopeIsFrameRateIndependent() {
    // A tenth of a second of attack from rest, at 30, 60 and 120 fps, lands in the same place, short of
    // the target (0.35), so the rate and not the clamp is what is compared.
    float levels[3];
    const float rates[3] = {30.0f, 60.0f, 120.0f};
    for (int r = 0; r < 3; ++r) {
        eg::Envelope env;
        GeodeSoundFrame f = frameWithRms(0.1f);
        const int frames = static_cast<int>(std::lround(rates[r] / 10.0f));
        for (int i = 0; i < frames; ++i) {
            f.rms = 0.1f + 1e-5f * static_cast<float>(i);
            env.step(&f, 1.0f / rates[r]);
        }
        levels[r] = env.level;
    }
    check(near(levels[0], levels[1], 2e-3f) && near(levels[1], levels[2], 2e-3f), "the envelope moves at the same speed at any fps");
    eg::Envelope stall;
    GeodeSoundFrame f = frameWithRms(1.0f);
    stall.step(&f, 10.0f);
    check(std::isfinite(stall.level) && stall.level <= 1.0f, "a long stall steps as at most kMaxDt");
}

void windowFlipsToGlAndShapes() {
    eg::Options whole;
    whole.enabled = true;
    const eg::Rect full = eg::glWindow(whole, 1080, 2400);
    check(full.x == 0.0f && full.y == 0.0f && full.w == 1080.0f && full.h == 2400.0f, "a zero window is the whole surface");
    eg::Options panned;
    panned.enabled = true;
    panned.left = 540.0f;
    panned.top = 0.0f;
    panned.width = 1080.0f;
    panned.height = 2400.0f;
    const eg::Rect r = eg::glWindow(panned, 2160, 2400);
    check(r.x == 540.0f && r.y == 0.0f && r.w == 1080.0f, "a panned window keeps its x");
    panned.top = 100.0f;
    panned.height = 2000.0f;
    check(eg::glWindow(panned, 2160, 2400).y == 300.0f, "y down becomes y up from the surface's bottom");

    const eg::Shape rest = eg::shape(full, 0.0f);
    const eg::Shape peak = eg::shape(full, 1.0f);
    check(near(rest.bandPx, 1080.0f * eg::kBandMin), "the band at rest is kBandMin of the short side");
    check(near(peak.bandPx, 1080.0f * (eg::kBandMin + eg::kBandSwing)), "the band at full level adds kBandSwing");
    check(near(rest.radiusPx, 1080.0f * eg::kCornerFraction) && rest.radiusPx == peak.radiusPx, "the corners do not breathe");
}

void blackWeightAcrossTheFrame() {
    eg::Rect full{0.0f, 0.0f, 1080.0f, 2400.0f};
    const eg::Shape s = eg::shape(full, 0.5f);
    // A pixel in from the rim (exactly on it, float rounding may put d a hair outside, where it is black).
    check(near(eg::blackAt(s, 540.0f, 1.0f), 0.0f), "the rim is clear");
    check(near(eg::blackAt(s, 1.0f, 1200.0f), 0.0f), "the side rim is clear");
    check(near(eg::blackAt(s, 540.0f, 1200.0f), 1.0f), "the middle of the screen is black");
    check(near(eg::blackAt(s, 540.0f, s.bandPx + 1.0f), 1.0f), "a band's width in is fully black");
    const float halfway = eg::blackAt(s, 540.0f, s.bandPx * 0.5f);
    check(halfway > 0.2f && halfway < 0.8f, "the band fades between the rim and the black");
    check(eg::blackAt(s, 540.0f, s.bandPx * 0.25f) < halfway, "the fade rises inward");
    check(near(eg::blackAt(s, 0.5f, 0.5f), 1.0f), "outside the rounded corner is black");
    check(near(eg::blackAt(s, -5.0f, 1200.0f), 1.0f), "off the window is black");
    // The colour: black is the only colour; the weight is a 0..1 alpha everywhere.
    bool inRange = true;
    for (float y = -10.0f; y <= 2410.0f; y += 37.0f) {
        for (float x = -10.0f; x <= 1090.0f; x += 41.0f) {
            const float a = eg::blackAt(s, x, y);
            inRange = inRange && std::isfinite(a) && a >= 0.0f && a <= 1.0f;
        }
    }
    check(inRange, "the black's weight is a finite 0..1 alpha across the frame");
}

// ---- #503 follow-up: behaviour the checks above leave open. Each case fails against the named mutant of
// the PR's EdgeGlowMath.hpp and passes on it as written. Expected values come from the ABI comment in
// geode_api.h, the spec comments in EdgeGlowMath.hpp, or a hand-worked literal, never from the code's maths.

// Kills `opts[0] > 0.5f` -> `>= 0.5f`: the ABI says "> 0.5 = on", so exactly 0.5 is off.
void enabledThresholdIsStrictlyAboveHalf() {
    const float half[eg::kFloats] = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
    const float clearlyOn[eg::kFloats] = {0.75f, 0.0f, 0.0f, 0.0f, 0.0f};
    check(!eg::parse(half, eg::kFloats).enabled, "an enabled flag of exactly 0.5 leaves the edge glow off");
    check(eg::parse(clearlyOn, eg::kFloats).enabled, "an enabled flag of 0.75 turns the edge glow on");
}

// Kills a finiteness check that stops short (`i < kFloats - 1`, or only the flag checked): a garbled
// option in any slot, the window's height included, turns the glow off.
void aGarbledOptionInAnySlotTurnsTheGlowOff() {
    bool allOff = true;
    for (int slot = 0; slot < eg::kFloats; ++slot) {
        float opts[eg::kFloats] = {1.0f, 0.0f, 0.0f, 1080.0f, 2400.0f};
        opts[slot] = std::numeric_limits<float>::infinity();
        allOff = allOff && !eg::parse(opts, eg::kFloats).enabled;
    }
    check(allOff, "an infinite option in any slot turns the edge glow off");
}

// Kills `count < kFloats` -> `count != kFloats`: the ABI only rejects fewer floats, so a longer array (a
// newer shell) still turns the glow on with the window as sent.
void extraTrailingOptionsAreIgnored() {
    const float longer[eg::kFloats + 1] = {1.0f, 0.0f, 0.0f, 1080.0f, 2400.0f, 42.0f};
    const eg::Options o = eg::parse(longer, eg::kFloats + 1);
    check(o.enabled && o.width == 1080.0f && o.height == 2400.0f, "an extra trailing float leaves the options as sent");
}

// Kills dropping the lower clamp or the isfinite guard on dt in rateOver(): a negative or NaN frame time
// (a clock that stepped back, a garbled dt) leaves the band where it was instead of throwing it.
void aBackwardOrGarbledFrameTimeLeavesTheBandStill() {
    GeodeSoundFrame loud = frameWithRms(0.5f);
    eg::Envelope back;
    back.level = 0.25f;
    back.step(&loud, -0.1f);
    check(back.level == 0.25f, "a negative dt leaves the band where it was");
    eg::Envelope garbled;
    garbled.level = 0.25f;
    garbled.step(&loud, std::numeric_limits<float>::quiet_NaN());
    check(garbled.level == 0.25f, "a NaN dt leaves the band where it was");
}

// Kills removing the kMaxDt cap: the spec says a longer frame "steps the envelope as if it were this long",
// so a 10 s stall releases exactly as far as one kMaxDt frame, and leaves the band open (a quarter second
// of release from full is 0.92^15, about 0.29, by hand) rather than shut as 10 s of release would.
void aStallReleasesNoFurtherThanTheCap() {
    GeodeSoundFrame silent = frameWithRms(0.0f);
    eg::Envelope stalled;
    stalled.level = 1.0f;
    stalled.step(&silent, 10.0f);
    eg::Envelope capped;
    capped.level = 1.0f;
    capped.step(&silent, eg::kMaxDt);
    check(near(stalled.level, capped.level, 1e-6f), "a 10 s stall releases exactly as far as one kMaxDt frame");
    check(stalled.level > 0.2f, "a stall leaves the band open instead of snapping it shut");
}

// Kills a pause that drops the target to 0 (0.92 of the level after one frame), to level * rate (0.928),
// or holds it (1.0): the spec eases a paused band off by a tenth of its level per reference frame, followed
// at the release rate, so one paused frame narrows it by 1 - 0.1 * 0.08 = 0.992 (by hand). The check
// allows anything between a 0 % and a 1.5 % drop.
void aPausedSongEasesTheBandOffGently() {
    eg::Envelope env;
    env.level = 0.8f;
    GeodeSoundFrame f = frameWithRms(0.2f);
    env.lastRms = f.rms;  // the engine already delivered this RMS: nothing new is heard
    env.step(&f, 1.0f / eg::kRefFps);
    check(env.level < 0.8f, "a paused frame narrows the band");
    check(env.level > 0.8f * 0.985f, "a paused frame narrows the band by under 1.5 %, not by a release step");
}

// Kills removing the isfinite guard in targetLevel(): a NaN RMS (a garbled sound frame) is silence.
void aGarbledRmsIsSilence() {
    GeodeSoundFrame garbled = frameWithRms(std::numeric_limits<float>::quiet_NaN());
    check(eg::targetLevel(&garbled) == 0.0f, "a NaN RMS reaches for a level of 0");
    eg::Envelope env;
    env.level = 0.5f;
    for (int i = 0; i < 10; ++i) env.step(&garbled, 1.0f / eg::kRefFps);
    check(std::isfinite(env.level) && env.level < 0.5f, "a NaN RMS lets the band settle and stay finite");
}

// Kills `o.width <= 0 || o.height <= 0` -> `&&`, and `<= 0` -> `< 0`: either side zero or negative means
// the whole surface (geode_api.h), not a zero-sized window.
void aWindowWithOneEmptySideIsTheWholeSurface() {
    const float sides[][2] = {{1080.0f, 0.0f}, {0.0f, 2400.0f}, {-1.0f, 2400.0f}, {1080.0f, -5.0f}};
    bool allWhole = true;
    for (const auto& side : sides) {
        eg::Options o;
        o.enabled = true;
        o.left = 30.0f;
        o.top = 40.0f;
        o.width = side[0];
        o.height = side[1];
        const eg::Rect r = eg::glWindow(o, 1080, 2400);
        allWhole = allWhole && r.x == 0.0f && r.y == 0.0f && r.w == 1080.0f && r.h == 2400.0f;
    }
    check(allWhole, "a window with an empty or negative side is the whole surface");
}

// Kills removing the max(surface, 1) guard: a surface not sized yet (0 x 0) is one pixel, and its mask is
// finite.
void anUnsizedSurfaceGivesAFiniteMask() {
    eg::Options whole;
    whole.enabled = true;
    const eg::Rect r = eg::glWindow(whole, 0, 0);
    check(r.w == 1.0f && r.h == 1.0f, "an unsized surface is treated as one pixel");
    check(std::isfinite(eg::blackAt(eg::shape(r, 0.5f), 0.5f, 0.5f)), "the mask over an unsized surface is finite");
}

// Kills `shortSide = window.w` (or `window.h`): the band and corners are fractions of the SHORT side, so a
// landscape window breathes exactly like a portrait one of the same short side.
void theBandFollowsTheShortSideInLandscape() {
    const eg::Rect portrait{0.0f, 0.0f, 1080.0f, 2400.0f};
    const eg::Rect landscape{0.0f, 0.0f, 2400.0f, 1080.0f};
    bool same = true;
    for (float level : {0.0f, 0.5f, 1.0f}) {
        const eg::Shape p = eg::shape(portrait, level);
        const eg::Shape l = eg::shape(landscape, level);
        same = same && p.bandPx == l.bandPx && p.radiusPx == l.radiusPx;
    }
    check(same, "landscape and portrait of one short side share the band and the corners");
}

// Kills removing the level clamp in shape(): a level past full does not widen the band past full, and one
// below rest does not narrow it below rest.
void theBandStaysBetweenRestAndFull() {
    const eg::Rect w{0.0f, 0.0f, 1080.0f, 2400.0f};
    check(eg::shape(w, 3.0f).bandPx == eg::shape(w, 1.0f).bandPx, "a level past full draws the full band");
    check(eg::shape(w, -2.0f).bandPx == eg::shape(w, 0.0f).bandPx, "a level below rest draws the resting band");
}

// Kills dropping or flipping the window's offset in blackAt() (`s.window.x + hx` -> `hx` or `- hx`): on a
// panned wallpaper the clear rim sits on the screen's edges, and the surface off screen is black.
void theRimFollowsAPannedWindow() {
    const eg::Rect panned{540.0f, 0.0f, 1080.0f, 2400.0f};  // the middle page of a 2160 px wide surface
    const eg::Shape s = eg::shape(panned, 0.5f);
    check(near(eg::blackAt(s, 541.0f, 1200.0f), 0.0f), "the screen's left edge on a panned surface is clear");
    check(near(eg::blackAt(s, 1619.0f, 1200.0f), 0.0f), "the screen's right edge on a panned surface is clear");
    check(near(eg::blackAt(s, 1080.0f, 1200.0f), 1.0f), "the screen's middle on a panned surface is black");
    check(near(eg::blackAt(s, 10.0f, 1200.0f), 1.0f), "the surface off screen to the left is black");
    check(near(eg::blackAt(s, 2150.0f, 1200.0f), 1.0f), "the surface off screen to the right is black");
}

// Kills using the half width for y (or the half height for x), and dropping abs() on either axis: the rim
// is clear on all four sides of a tall window, and the mask is mirror symmetric both ways.
void theRimIsClearOnAllFourSides() {
    const eg::Shape s = eg::shape(eg::Rect{0.0f, 0.0f, 1080.0f, 2400.0f}, 0.5f);
    check(near(eg::blackAt(s, 540.0f, 2399.0f), 0.0f), "the top rim is clear");
    check(near(eg::blackAt(s, 1079.0f, 1200.0f), 0.0f), "the right rim is clear");
    bool mirrored = true;
    for (float y = 3.0f; y < 2400.0f; y += 97.0f) {
        for (float x = 3.0f; x < 540.0f; x += 29.0f) {
            const float a = eg::blackAt(s, x, y);
            mirrored = mirrored && near(a, eg::blackAt(s, 1080.0f - x, y), 1e-3f) &&
                       near(a, eg::blackAt(s, x, 2400.0f - y), 1e-3f);
        }
    }
    check(mirrored, "the mask is the same mirrored left to right and top to bottom");
}

// Kills replacing the corner's Euclidean distance (hypot) by a Manhattan sum or a max: one pixel inside the
// rounded corner's arc, on its 45 degree diagonal, is on the rim and so clear; one pixel outside is black.
void theCornerIsRoundNotSquareOrBevelled() {
    const eg::Shape s = eg::shape(eg::Rect{0.0f, 0.0f, 1080.0f, 2400.0f}, 0.5f);
    const float r = s.radiusPx;  // the corner circle's centre is at (r, r)
    const float in = (r - 1.0f) / std::sqrt(2.0f);
    check(eg::blackAt(s, r - in, r - in) < 0.01f, "one pixel inside the rounded corner is clear");
    const float out = (r + 1.0f) / std::sqrt(2.0f);
    check(near(eg::blackAt(s, r - out, r - out), 1.0f), "one pixel outside the rounded corner is black");
}

}  // namespace

int main() {
    optionsParse();
    envelopeAttacksFastReleasesSlow();
    envelopeEasesOffWhenNothingNewIsHeard();
    envelopeIsFrameRateIndependent();
    windowFlipsToGlAndShapes();
    blackWeightAcrossTheFrame();
    enabledThresholdIsStrictlyAboveHalf();
    aGarbledOptionInAnySlotTurnsTheGlowOff();
    extraTrailingOptionsAreIgnored();
    aBackwardOrGarbledFrameTimeLeavesTheBandStill();
    aStallReleasesNoFurtherThanTheCap();
    aPausedSongEasesTheBandOffGently();
    aGarbledRmsIsSilence();
    aWindowWithOneEmptySideIsTheWholeSurface();
    anUnsizedSurfaceGivesAFiniteMask();
    theBandFollowsTheShortSideInLandscape();
    theBandStaysBetweenRestAndFull();
    theRimFollowsAPannedWindow();
    theRimIsClearOnAllFourSides();
    theCornerIsRoundNotSquareOrBevelled();
    if (failures != 0) {
        std::printf("%d edge glow check(s) failed\n", failures);
        return 1;
    }
    std::printf("edge glow: all checks passed\n");
    return 0;
}
