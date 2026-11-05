// Host-side checks for core/viz/GeneratorBinding (#471): how ShaderScene hands a scene's generator
// textures to lib_generator. Built and run by CI ("Native host tests" in .github/workflows/android.yml)
// with the system compiler; no GL.
//
// What is pinned: slot i samples on texture unit 4 + i (1, 2 and 3 are the flow, the palette LUT and
// the sound), each slot has the sampler name lib_generator declares, and uGenMix is 0 wherever no
// texture is bound, so a style with an empty slot draws exactly as it did before generators existed.
#include <cstdio>
#include <cstring>
#include <limits>

#include "viz/GeneratorBinding.hpp"

namespace gen = geode::viz::generator_binding;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void unitsFollowTheSoundTexture() {
    check(gen::kSlots == 4, "four slots: sky, wall, material, plane");
    check(gen::unit(0) == 4, "the sky samples on unit 4, after the sound texture's unit 3");
    check(gen::unit(1) == 5 && gen::unit(2) == 6 && gen::unit(3) == 7, "wall, material and plane on 5, 6 and 7");
}

void samplerNamesMatchTheLibrary() {
    check(std::strcmp(gen::samplerName(0), "uGenSky") == 0, "slot 0 is uGenSky");
    check(std::strcmp(gen::samplerName(1), "uGenWall") == 0, "slot 1 is uGenWall");
    check(std::strcmp(gen::samplerName(2), "uGenMaterial") == 0, "slot 2 is uGenMaterial");
    check(std::strcmp(gen::samplerName(3), "uGenPlane") == 0, "slot 3 is uGenPlane");
}

void mixIsZeroWithoutATexture() {
    const unsigned tex[4] = {0, 7, 0, 9};
    const float mix[4] = {1.0f, 0.5f, 1.0f, 1.0f};
    const auto m = gen::uploadMix(tex, mix);
    check(m[0] == 0.0f && m[2] == 0.0f, "a slot with no texture uploads mix 0 whatever its param says");
    check(m[1] == 0.5f && m[3] == 1.0f, "a bound slot uploads its mix");
}

void mixIsClampedAndSane() {
    const unsigned tex[4] = {1, 2, 3, 4};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float mix[4] = {-0.5f, 1.5f, nan, 0.25f};
    const auto m = gen::uploadMix(tex, mix);
    check(m[0] == 0.0f, "a negative mix clamps to 0");
    check(m[1] == 1.0f, "a mix over 1 clamps to 1");
    check(m[2] == 0.0f, "a NaN mix (a broken route) uploads 0, today's look");
    check(m[3] == 0.25f, "an in-range mix passes through");
}

void noTexturesMeansNoGenerators() {
    const unsigned none[4] = {0, 0, 0, 0};
    const unsigned one[4] = {0, 0, 0, 5};
    check(!gen::anyBound(none), "nothing bound: the scene skips the generator binding entirely");
    check(gen::anyBound(one), "one bound slot is enough to bind");
}

}  // namespace

int main() {
    unitsFollowTheSoundTexture();
    samplerNamesMatchTheLibrary();
    mixIsZeroWithoutATexture();
    mixIsClampedAndSane();
    noTexturesMeansNoGenerators();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("generator_binding_test: ok\n");
    return 0;
}
