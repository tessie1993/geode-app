# Mixer regression tests

This standalone target builds the production `Mixer.cpp` and exercises stereo
sample scheduling without Android, Oboe, a decoder, or a DSP chain. The only
Android stub is the unused opaque asset-manager type from the shared API header.

```sh
cmake -S core/audio/player/tests -B build/mixer-tests
cmake --build build/mixer-tests --config Release
ctest --test-dir build/mixer-tests -C Release --output-on-failure
```

Requires a C++17 compiler. Tests cover callbacks that straddle a fade boundary,
sample continuity through EOF for all three fade curves, outgoing decoder
starvation before/during a fade, incoming starvation, unknown-duration gapless
playback, and final EOF. These are scheduling tests, not device audio-quality or
thread-race qualification.
