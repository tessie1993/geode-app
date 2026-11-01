# The musical layer: tempo, harmony and form in `core/sound`

Before this layer, the sound engine heard levels and hits but not music. It had no tempo, bar, phrase,
key or section, so styles could only flash and push in. The musical layer adds those, stepped with the
sound frame and with the same rule as the rest of the engine: the same audio gives the same frames, live or
in an export. The new `GeodeSoundFrame` fields are documented on the struct (`core/sound/SoundFrame.h`).

## Rhythm (`core/sound/Rhythm`)

| Stage | What | Source |
|---|---|---|
| Onset envelope | 7 filter-bank bands, log over each band's 4 s mean, positive change over 20 ms, averaged; 100 Hz hop driven by `write()`, so it depends on the samples only | multi-band onsets: Scheirer, JASA 1998; Klapuri, ICASSP 1999 |
| Tempo | the envelope minus its 0.3 s mean; biased autocorrelation over 6 s every 0.25 s; comb with the double lag; log-normal prior around 120 BPM, one octave wide; 60–200 BPM | autocorrelation tempogram: Grosche & Müller, IEEE TASLP 2011; prior: Ellis, JNMR 2007 |
| Tempo changes | same tempo within 4%: refined; another: 4 estimates in a row (1 s), an octave or triplet relative: 8 (2 s) | |
| Phase | windowed Fourier coefficient of the last 4 s of envelope at the beat period (decay 1.5 s), read at the newest instant: the predominant local pulse, used to predict | PLP: Grosche & Müller, ISMIR 2009 / TASLP 2011 |
| Oscillator | beats as a running count, phase error corrected 20% per 50 ms, spread so it never runs backwards; lock above 0.35 confidence for 1 s, unlock below 0.2 for 2 s | real-time PLP with a PLL: Meier, Chiu & Müller, TISMIR 2024; aubio `beattracking` |
| Downbeat | per beat slot, the kick-band onset (minus half the snare) near the beat, leaky over ~6 bars, plus chord changes; a new slot must lead for a bar | backbeat and harmonic-change cues: Goto, JNMR 2001; Papadopoulos & Peeters, 2011 |
| Phrase | 8 bars from the first downbeat; restarted by a section change or a drop | |

While the tracker is unlocked (ambient music, rubato, a fill), the phases fall back to the hit clock, as the
engine did before, and the beat, downbeat and phrase triggers stay silent.

## Harmony (`core/sound/Harmony`)

- **Chroma** comes from the spectral peaks of the melody tracker's long spectrum (2× the frame FFT) between
  250 Hz and 2 kHz. Each peak goes to its nearest pitch class with log compression (Müller's CLP). Summing
  every bin instead lifted all twelve classes and no scale stood out. It is smoothed three ways: fast 0.1 s,
  medium 0.5 s, slow 4 s.
- **Key**: Pearson correlation of the slow chroma with the 24 rotated Krumhansl–Kessler profiles (1982). A
  new key must fit better for 2 s. `keyHue` places the key on the circle of fifths, with a minor key on its
  relative major.
- **Chord colour** (`chordHue`): the medium chroma's centre on the circle of fifths, a one-circle form of
  Harte, Sandler & Gasser's tonal centroid (2006).
- **Tension**: interval-class roughness of the classes sounding together, ranked after Plomp & Levelt (1965)
  and Huron's interval-class consonance.
- **Harmonic change**: the last 0.5 s of chroma against the 2 s before it (cosine, mean removed). Two
  symmetric 2 s windows would only peak two seconds after the change.
- **Scale and mode**: all 46 scales of the musical intelligence database (`docs/musical-intelligence`) on
  all 12 roots, scored on the slow chroma. See that folder's README.

## Form (`core/sound/Form`)

- **Per-beat features**: the 16 EQ bands in dB over the track's peak, centroid and flatness, and the
  chroma. They are kept in a 32-beat ring. Distance is the mean squared difference per group, and
  similarity is `exp(-d² / typical d²)`.
- **Novelty**: Foote's checkerboard kernel (ICME 2000), live. The newer half is the bar just played, the
  older half the 16 beats before. So a **section change fires one bar after its boundary, on a downbeat**,
  at least 8 bars after the last, and the phrase count is told it began a bar ago.
- **Section id**: the new section's first bar against the stored section centroids (up to 8). A close match
  is the same section returning, such as a chorus.
- **Build-up**: least-squares slopes over 8 s of loudness, brightness, onset rate and bass share. Each cue
  is scaled to a typical build, and two must agree.
- **Drop**: the bass share jumps above twice its 2 s minimum, back near its long-term share, within 4 s of
  a build (Yadati, Larson, Liem & Hanjalic, ISMIR 2014). It fires at once, marks the beat as the downbeat
  and starts a phrase. It counts as a section change down to 4 bars.
- **Section state** (`GEODE_SECTION_*`): Intro until the first section change (or 16 bars), then each new
  section is classified by its first bar's loudness against the loudest recent bar and its bass share.
  Near the top with bass is Drop (a peak section), 6 dB down or without bass is Breakdown, anything else is
  Verse. Build-up above 0.55 is Build. A long fade after the music has peaked is Outro. These are
  heuristics: live analysis cannot see the future.

## Cost

The work per second is about 4 autocorrelations of 600 × 200, 20 phase sums of 400, 4 scale scans of
552 × 12, and one 32-row distance update per beat. That is well under the 0.3 ms-per-frame budget on a
phone. `core/tests/musical_test.cpp` checks the scale catalogue, the detection of eight scales, and
bit-exact determinism.
