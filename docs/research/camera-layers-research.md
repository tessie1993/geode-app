# Research: camera grammar, cross-modal mapping, layering, spawn and zoom techniques, consumer reaction UI

Written 2026-10-31 for MuzViz / Geode (Android, GLSL styles, shared `CameraRig`). This file skips MilkDrop/projectM, TouchDesigner CHOPs, Resolume, VDMX, Synesthesia and Notch audio inputs in general, which earlier research covers. Those tools appear here only where they answer one of the five gaps.

How sure each claim is:
- **[P]** I read the primary source myself (paper text, official docs).
- **[S]** Taken from an abstract, a catalogue entry or a secondary summary. The claim matches what the work is known for, but I did not read the full text.
- **[H]** A practitioner rule of thumb (blogs, tutorials) or my own inference. Treat these as defaults to tune, not findings.

---

## 1. Camera grammar for music video, concert and VJ visuals

### 1.1 When cuts land: the hard numbers

**Prétet, Richard & Peeters, "Is there a 'language of music-video clips'?"** (arXiv 2108.00970, 2021) [P], https://arxiv.org/html/2108.00970
- **Corpus.** 548 official music videos. Mean shot length is **4.76 s**, mean bar length **2.30 s**, mean functional section (verse, chorus and so on) **19.73 s**. So a typical shot lasts about **2 bars**, and a section holds about **4 shots**.
- **Shot length snaps to the musical grid.** The paper counts a clip as "beat-level" when the peak of its shot-duration histogram falls between 0.5 and 1.5 beats, and "bar-level" when it falls between 0.5 and 1.5 bars.
  - About 60% of clips are beat-level (329 videos; the paper says "two thirds").
  - About 17% are bar-level (95 videos; the paper says "one fifth").
  - Only 2 clips lock to a 4-bar pattern.
  - Worked examples: Kanye West's "Heartless" (88 BPM) has its main shot-length peak at **2.72 s**, and Katy Perry's "Firework" (124 BPM) at **1.93 s**. Both are exactly **one 4/4 bar** (4 × 60/88 = 2.73 s; 4 × 60/124 = 1.94 s).
- **Cuts line up with section boundaries more in Pop** than in other genres. The co-occurrence score with section boundaries is 0.36 for Pop against 0.18 for Rock and Dance/Electro. With bar lines, R&B scores 0.31 and Reggaeton 0.28.
  - By video genre, dance and concept videos align with bars more (0.23–0.24) than performance videos do (0.16).
- **Editors interviewed for the paper:**
  - "Cuts have to follow the music's rhythm, even though they might not always co-occur with beats" (Courtès).
  - "Off-beat cuts are used to create dynamics" (Pozzi).
  - The paper also notes anticipation: cuts placed just before the downbeat, for example in Adele's "Rolling in the Deep" at about 2:20.

**Practitioner rules** [H]:
- **Sound On Sound:** cut on the beat in 4/4, with occasional two-beat and two-bar cuts so it does not become monotonous. Cited via a FilmDaft roundup, https://filmdaft.com/how-to-edit-video-clips-to-the-beat-of-music-the-easy-way/
- **FilmDaft:**
  - Skip the first downbeat and cut on the second.
  - A hard cut on the 1/16-note "lift" just before the downbeat reads better than one on the downbeat itself. Same link.
- **Against cutting every beat:**
  - Videomaker: a new shot on every beat "would be overwhelming", https://www.videomaker.com/article/c3/17048-rocking-out-with-a-music-video/
  - CapCut's own guide calls a cut on every beat a beginner mistake that makes pacing feel mechanical, https://www.capcut.com/create/music-video-pacing-emotional-montage-rhythm
- **Frame maths:** frames per beat = 60/BPM × fps, so 120 BPM at 30 fps is 15 frames. Cut on phrases of 2, 4 or 8 beats, and save "the biggest visual for the drop". https://www.claudemarket.ai/skills/iart-ai/motion-design-skills/beat-sync-editing [H, aggregator]
- **Live concert direction:**
  - Switch cameras after the performer finishes a phrase or gesture, not mid-gesture.
  - Make camera moves between sections, then hold for a phrase after a small move.
  - Know the music well enough to anticipate solos and changes.
  - Sources: https://msinthebiz.com/2016/08/26/experience-live-directing-concert-part-24/ and https://symposium.music.org/volume-56/articles-1752877057/video-direction-as-an-application-of-the-study-of-music [H]
- **Festival screens:** "time these shots with the music's peaks", for example the DJ's hand over the button just before the drop. https://www.ticketfairy.com/blog/making-djs-telegenic-on-big-festival-screens.md [H, promoter blog]

**Cut cadence at common tempos** (pure arithmetic from the numbers above):

| BPM | 1 beat | 1 bar | 2 bars (≈ mean shot length) | 4-bar phrase | 8-bar phrase |
|---|---|---|---|---|---|
| 90 | 0.67 s | 2.67 s | 5.3 s | 10.7 s | 21.3 s |
| 120 | 0.50 s | 2.00 s | 4.0 s | 8.0 s | 16.0 s |
| 128 | 0.47 s | 1.88 s | 3.75 s | 7.5 s | 15.0 s |
| 174 | 0.34 s | 1.38 s | 2.76 s | 5.5 s | 11.0 s |

Rules of thumb I take from this [H, grounded in Prétet]:
- **Default cut cadence: every 1–2 bars.** At high tempo (above ~150 BPM), fall back to bar or half-time counting, so cuts stay at least ~1.3 s apart.
- **Beat-rate cuts come in short bursts only**, such as a 1-bar fill before a drop or a drum fill. They are never the default.
- **Always cut on a section change.** Pop editors do this most. A cut on the chorus's first downbeat is the strongest cut in the song.
- **Allow anticipation.** An optional offset of −1/16 note (125 ms at 120 BPM) lets a cut "lead" the downbeat.
- **Hold through a bar after a move**, rather than cutting in the middle of one.

### 1.2 Camera moves and what they mean

None of the following has peer-reviewed numbers. They are film-grammar conventions, collected so the rig has a default meaning for each channel.

- **Push-in (dolly in).** Raises intensity and intimacy. A slow push "builds tension", a fast one "delivers a jolt". Unlike a zoom, the camera moves, so parallax changes. https://filmdaft.com/what-is-a-push-in-shot-in-film-definition/ and https://howtofilmschool.com/dictionary/push/ [H]
  - Bowen's *Grammar of the Shot* (Routledge, 4th ed. 2017; 5th ed. 2023) has a tip, "Motivate Your Dolly-In and Dolly-Out Camera Moves", and treats a move as start frame, move, end frame. https://www.routledge.com/9781032190167 [S]
  - **Music mapping:** tie the push to a build (rising energy or density over 4–16 bars). Push slowly while energy rises, and finish on the drop or downbeat.
- **Pull-out.** Reveals context and puts emotional distance between the viewer and the subject. Use it on a breakdown, outro or fade. [H]
- **Dolly zoom ("Vertigo").** Unsettling. Reserve it for a single riser moment. [H]
- **Crane or jib reveal.** Rising and widening reveals scale.
  - Festival broadcasts put jibs on stage, but I found no primary interview tying crane moves to drops.
  - **Inference:** this is the "big visual on the drop". The drop or first chorus downbeat starts a rise-and-widen move lasting 1–2 bars, or a cut to a high wide shot.
- **Orbit or arc.** Continuous motion with no endpoint. It suits sustained or pad sections, where there is no event to cut on [H]. It matches the "Time" clocks in Synesthesia, which advance with volume rather than per beat (`syn_Time`, `syn_BassTime`). https://app.synesthesia.live/docs/ssf/audio_uniforms.html [P]
- **Roll or dutch angle.** Conventionally means unease or instability [H]. In music, a small roll swing (a few degrees) works best as a two-state toggle on a snare backbeat, or as slow drift on harmonic tension, not as constant wobble.
  - Synesthesia ships `syn_ToggleOnBeat`, a smooth 0↔1 flip on each beat, which is exactly that primitive [P, same docs].
  - Derivative's Audio Analysis 2.0 has a "Logic" toggle for "alternating camera angles". https://derivative.ca/community-post/asset/audio-analysis-20-extended-audio-reactive-components/73946 [P]
- **Handheld or trauma shake.** Squirrel Eiserloh's GDC 2016 talk, "Math for Game Programmers: Juicing Your Cameras With Math", is the standard model. https://www.gamedeveloper.com/programming/video-sprucing-up-cameras-with-math [S]
  - Hits add to a 0–1 **trauma** value, which decays over time.
  - Shake = trauma² (some use ³) × max offset/angle × smooth noise (Perlin), not random or sine. Squaring hides small hits and makes big hits violent.
  - Implementations: https://bevy.org/examples/camera/2d-screen-shake/ and https://github.com/baratgabor/Unity3D-PerlinCameraShake [S]
  - **Music mapping:** feed trauma from kick or snare onsets scaled by onset strength, so only the big hits shake.
- **Speed ramps.** Speed goes 100% → 25% → 100%, with the slowest frame landing on the downbeat. The slow section should arrive on or just before the hit and leave once the payoff reads. Staying slow too long kills it. https://www.kapwing.com/resources/how-to-use-the-speed-ramp-effect/ and https://www.claudemarket.ai/skills/iart-ai/motion-design-skills/beat-sync-editing [H]
  - **In a real-time rig:** time-scale the camera clock, not the music. For example, scale it to 0.25× over the last beat of a build, then snap to 1× (or overshoot to 1.5×) on the drop.
- **Zoom-through (endless zoom) transition.** Hides a cut inside a fast push: accelerate into the frame centre over half a bar, swap the scene at maximum speed or blur, and decelerate out. The technique is in section 4. [H]
- **Fly-through (3D VJ content).** The standard Beeple, Notch and Unreal VJ loop: a camera moving at constant speed down a tunnel or through a field of repeated geometry, looped seamlessly.
  - Beeple released Creative Commons loops used by deadmau5, Tiësto, Amon Tobin and others, sometimes with the Cinema 4D source files. https://www.resolume.com/blog/10412 [S]
  - The music link is usually **speed tracks energy** (a slow trend) plus **forward lurches or FOV kicks on kicks**.
  - Eitan & Granot (section 2) found that musical intensification is heard as *increasing speed* more than as ascent, which supports mapping energy to fly speed.

**How many moves at once** [H]. I found no source with a number. Camera-operator practice (start frame, move, end frame; hold a phrase after a move) and the fact that editors cut about every 2 bars suggest:
- At most **one "big" move** (push, pull, crane or orbit change) per section, plus one continuous drift and a transient layer (shake or roll toggle).
- Move durations: **1 bar** for a punctuating move, **4–16 bars** for a build push.
- **Don't cut in the middle of a big move**, except on a section change.

---

## 2. Cross-modal audio-visual correspondences (for choosing mappings)

### 2.1 Core correspondences

- **Spence (2011), "Crossmodal correspondences: a tutorial review"**, *Attention, Perception & Psychophysics* 73: 971–995, doi:10.3758/s13414-010-0073-7. https://www.psy.ox.ac.uk/publications/115721 [S]
  - People consistently match **high-pitched sounds with small, bright objects high up in space**.
  - Spence argues that correspondences help the brain decide which sounds and sights belong together (the "crossmodal binding" problem). In other words, matched mappings make a visual feel *caused by* the sound.
- **Evans & Treisman (2010)**, *Journal of Vision* 10(1):6. https://pmc.ncbi.nlm.nih.gov/articles/PMC2920420 [S]
  - In speeded classification, pitch mapped spontaneously to **vertical position, size and spatial frequency**: high pitch goes with high position, small size and fine texture. Pitch did **not** map to contrast.
  - Evans (2019, *Cognition*) found these interactions do not need selective attention. They are automatic.
- **Marks (1974, 1987)**, cited in Adeli et al. 2014 (below). Louder tones match **brighter light**, and pitch also maps to lightness. [S]
- **Küssner & Leech-Wilkinson (2014)**, *Psychology of Music*. Participants drew to sound on a tablet. [S]
  - Most drew **pitch as height** and **loudness as line thickness**.
  - In a reanalysis, 98% of trained and 84% of untrained participants drew a pitch rise as an upward line. 93% and 73% drew louder as pressing harder.
  - Untrained participants ignored pitch when it did not change.
  - Related: Küssner, Tidhar, Prior & Leech-Wilkinson (2014), "Musicians are more consistent", *Frontiers in Psychology*, which used gestures to tones varying in pitch, loudness and tempo. https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2014.00789/pdf
- **Lipscomb & Kim (2004)**, ICMPC 8. Later work cites it for pitch matching **vertical location** and loudness matching **size**. I could not find the paper itself, so treat its details (timbre to shape and so on) as unverified. [S]
- **Eitan & Granot (2006), "How music moves"**, *Music Perception* 23(3). https://openscholar.huji.ac.il/roni-granot/publications/how-music-moves-musical-parameters-and-listeners%E2%80%99-images-motion [S] Listeners imagined motion for musical changes, and the results were **asymmetric**:
  - **Abatements** (getting softer, falling or slowing) are strongly heard as **spatial descent**.
  - **Intensifications** are heard mostly as **increasing speed, not ascent**.
  - Single parameters do not predict motion images on their own; they interact.
  - A follow-up with children's movement (ages 5 and 8): pitch mapped to vertical motion, loudness to energy and vertical motion, tempo to speed and energy. https://jyx.jyu.fi/handle/123456789/20885
  - **For the camera:** energy rising should mean the camera moves *faster* (fly or dolly speed). Energy falling should mean the camera *sinks or settles* (crane down, pull back).
- **Adeli, Rouat & Molotchnikoff (2014)**, *Frontiers in Human Neuroscience*, "Audiovisual correspondence between musical timbre and visual shapes". https://www.frontiersin.org/journals/human-neuroscience/articles/10.3389/fnhum.2014.00352/full [S]
  - **Timbre (with loudness envelopes normalised) maps strongly to shape.** Soft, mellow timbres match rounded shapes, and harsh or bright timbres match sharp, angular shapes. That is the well-known "bouba/kiki" pattern; check the exact colours and greys in the full text.
  - Fundamental frequency did **not** map to height, grey level or colour in their stimuli. So pitch effects are weaker than the classic studies suggest when timbre varies.
- **Giannakis, *Sound Mosaics*** (PhD thesis, Middlesex 2001; journal versions around 2006). https://repository.mdx.ac.uk/item/83031 [S]
  - Timbre maps to texture: **brightness (spectral centroid) to texture coarseness and contrast**, **spectral compactness to granularity**, and **dissonance to texture periodicity or repetitiveness**.
- **Palmer, Schloss, Xu & Prado-León (2013)**, *PNAS* 110(22): 8836–8841, "Music–color associations are mediated by emotion". https://pmc.ncbi.nlm.nih.gov/articles/PMC3670360 [S]
  - **Faster, major-key music maps to more saturated, lighter, yellower colours.** Slower, minor-key music maps to desaturated, darker, bluer ones.
  - The pattern held in both the US and Mexico, and emotion mediates it.
  - Use it for **slow palette drift on mode, valence and tempo**, not for per-beat colour flashing.
- **Farbood (2012), "A parametric, temporal model of musical tension"**, *Music Perception* 29(4): 387–428. https://bpb-us-e1.wpmucdn.com/wp.nyu.edu/dist/f/11865/files/2020/08/Farbood_2012_Musical_Tension.pdf [S]
  - Perceived tension is predicted by **harmony, pitch height, melodic expectation, dynamics, onset frequency, tempo and rhythmic regularity**, combined over time.
  - So a "tension" control is many-to-one by nature: loudness, density, pitch-height trend and harmonic change feed one slow value.

### 2.2 Mapping summary

| Sound feature | Strongest visual partner | Evidence |
|---|---|---|
| Pitch height (melody, centroid trend) | Vertical position, smaller size, finer spatial frequency, lighter | Spence 2011; Evans & Treisman 2010; Küssner 2014 |
| Loudness / energy | Size, line weight, brightness; and *speed* when rising | Marks; Küssner 2014; Eitan & Granot 2006 |
| Falling energy | Descent, settling | Eitan & Granot 2006 |
| Timbre brightness / harshness (centroid, flatness) | Angular vs round shape; texture coarseness and contrast; sharpness | Adeli 2014; Giannakis 2001 |
| Dissonance / roughness | Repetitive or periodic texture; rougher surface | Giannakis 2001 |
| Tempo / mode (valence) | Saturation, lightness, warm vs cool hue | Palmer 2013 |
| Tempo | Motion speed and energy | Eitan & Granot; Küssner 2014 (gesture) |
| Tension (multi-cue) | Slow, accumulating change: palette shift, shape complexity, camera distance | Farbood 2012 (inputs); the visual target is inference |

### 2.3 Mapping strategies: one-to-one vs many, and mapping layers

These come from instrument design, but the ideas carry straight over to sound → visual mapping.

- **Rovan, Wanderley, Dubnov & Depalle (1997)**, "Instrumental gestural mapping strategies as expressivity determinants in computer music performance" (KANSEI workshop). https://www-archive.idmil.org/_media/publications/1997/rovan_1997_kansei.pdf [S]
  - Defines three mapping types: **one-to-one**, **divergent** (one input drives many outputs) and **convergent** (many inputs drive one output).
  - Convergent mapping is the most expressive, but not always obvious to implement.
- **Hunt & Kirk (2000), "Mapping strategies for musical performance"**, in *Trends in Gestural Control of Music* (IRCAM). https://www-media.idmil.org/media/Trends_Ircam/DOS/P.HunKir.pdf [P, results section read] They compared three interfaces:
  - mouse with on-screen sliders (one-to-one, one at a time);
  - physical sliders (one-to-one, simultaneous);
  - a coupled "multiparametric" interface (many-to-many, where the user's energy drives volume).

  Findings:
  - The **mouse wins on simple single-parameter tasks**.
  - As soon as several parameters change at once, **the multiparametric interface becomes the best**, and its best scores came on the hardest tests.
  - It was also catching up overall across three sessions: "a 'complex' interface is needed for a complex test".
  - Users said of the mouse: "easier to start with, but has obvious limitations".
- **Hunt, Wanderley & Paradis (2003), "The importance of parameter mapping in electronic instrument design"**, *Journal of New Music Research* 32(4); the NIME 2002 version is at https://nime.org/proc/nime2002_hunt/ [S]
  - Mapping "can define the very essence of an instrument".
  - Proposes a model of **multi-layer mapping**: controls → abstract or perceptual parameters → engine parameters.
- **Hunt & Wanderley (2002), "Mapping performer parameters to synthesis engines"**, *Organised Sound* 7(2): 97–108. https://idmil.org/?p=2858 [S] Guidelines for designing mappings.
- **Arfib, Couturier, Kessous & Verfaille (2002)**, *Organised Sound* 7(2): 127–144. [S]
  - Three layers: gesture data → *gesture perceptual space* → *sound perceptual space* → synthesis parameters.
  - The middle link between perceptual spaces makes the system "highly modular".
- **Lima, Dos Santos & Meiguins (2021), "A survey of music visualization techniques"**, *ACM Computing Surveys* 54(7), doi:10.1145/3461835 [S]. It classifies work by input features, aspects visualised and technique.
- **Khulusi et al. (2020), "A survey on visualizations for musical data"**, *Computer Graphics Forum*. https://diglib.eg.org/handle/10.1111/cgf13905 [S]

**What this means for Geode** [H]: keep three layers.
1. **Sound frame values** (the engine side: 72 `ReactSource`s).
2. A small set of **perceptual "meaning" signals**, such as Energy, Punch, Brightness, Tension, Melody height, Section change.
3. **Visual parameters.**

Users map layer 2 to visual targets. Each meaning signal is itself convergent: Tension is built from loudness trend, density, centroid trend and harmonic change. This is the Arfib/Hunt model, and it is how Synesthesia's "Presence" and "Intensity" uniforms already work.

---

## 3. Layering: not everything moving on the same signal

### 3.1 Patterns tools ship

- **Synesthesia's uniform families** are a ready-made time-scale split. https://app.synesthesia.live/docs/ssf/audio_uniforms.html [P]
  - **Hits** (per band) spike "during the isolated transients" and are meant for percussion.
  - **Level** is smoothed band loudness.
  - **Presence** tracks rising and falling band activity "without reacting to each individual sound".
  - **Time** is a clock that advances faster when loud.
  - **Beat** gives `OnBeat`, `ToggleOnBeat`, `RandomOnBeat` and `BeatTime`.
  - **BPM LFOs** are sine and triangle waves at 1, ½, ¼ and ⅛ of the tempo.
  - **Large features:** `syn_FadeInOut` rises as the song starts and falls as it ends; `syn_Intensity` "accumulates" with how intense the song is.
  - That gives four time scales: **transient (Hits, OnBeat) < envelope (Level) < trend (Presence, BPM/8 LFO) < song (Intensity, FadeInOut)**, each split by band.
- **Notch** [P], https://manual.notch.one/2026.1/en/docs/learning/working-with-audio/
  - Assign each node to one band so it "react[s] to one specific element".
  - Tune **attack** (fast for drums) and **decay** (long for a tail) per modifier.
  - Use the **Sound Modifier** for "long, evolving sounds or full stereo mixes".
  - Use the **FFT Region Modifier** for triggers from "percussive elements" (kick, snare).
  - The Sound Effector lets "different nodes be animated with different frequency bands" inside one cloner.
- **TouchDesigner palette `audioAnalysis`** outputs Low, Mid, High, Spectral Centroid, slow and fast spectral density (Smp, Fmp), Kick detection, Snare detection and Rhythm. https://derivative.ca/UserGuide/Palette:audioAnalysis [S]
  - Kick, snare and rhythm are pulses; the band levels are smooth.
  - Only Low is on by default.
- **Derivative community, "Audio Analysis 2.0"** (Count Analysis, Jumper, Zig_Zagger, blended drivers) [P], https://derivative.ca/community-post/asset/audio-analysis-20-extended-audio-reactive-components/73946
  - Fire on every **4th, 8th or 16th** kick rather than every kick.
  - **Hold a value for a full beat cycle** ("Express").
  - **Toggle** on a count, for alternating camera angles.
  - **Jump** to a new value per trigger (random, loop or zigzag).
  - **Blended drivers** (low + kick) with **asymmetric lag** (fast attack, slow decay) for noise seeds, translations and rotations.
  - Add an "Audio Level Amp" for micro-reactivity between hits.
- **Magic Music Visuals forum (developer posts)** [S], https://magicmusicvisuals.com/forums/viewtopic.php?p=8215
  - **Peak** gives "fast response, slow cool down", good for scale and rotation.
  - **Smooth** and **Average** tame jitter.
  - Modifier order (Peak → Scale → Offset) matters.
- **ossia score audio-reactive example** [P], https://ossia.io/score-docs/examples/video/audioreactive.html
  - Two independent analysis paths: the mix drives zoom, smoothed with a OneEuro filter, while a separate gated RMS on the mic drives a threshold.
  - "The steady rhythm drives the overall motion, while a voice or instrument interrupts the image."
- **Pangolin (laser) forum** [H]: start with high release and use the "filtered" FFT for slower, readable motion. https://forums.pangolin.com/goto/post?id=5922

### 3.2 Layering recipe

1. **Split by source.** Kick → one punctuating channel. Snare or clap → a different one, such as a roll toggle or a flash. Hats or highs → texture or fine detail. Melody or centroid → height or hue offset. Bass sustain → scale or weight. None of these share a target.
2. **Split by time scale.** Each visual target lives in one band:
   - **transient** (decays in under 250 ms);
   - **beat or bar** (held or toggled per beat or bar);
   - **trend** (seconds, from Presence-style rising or falling activity);
   - **section** (events at section changes, plus a song-level intensity).

   Camera *position* belongs to trend and section; camera *shake* belongs to transient.
3. **Divide the triggers down.** Big state changes (cut, palette swap, spawn burst) fire every 4, 8 or 16 beats or on section change, not every kick.
4. **Use asymmetric envelopes.** Fast attack with slow release is the default for anything on a hit. Symmetric smoothing is for trends.
5. **Let the music's own build drive the slow layer.** The buildup → drop structure is a tension device:
   - Breakdowns and builds strip out the bass so they feel incomplete, then the drop releases that. https://en.wikipedia.org/wiki/Drop_(music) [S]
   - Solberg & Jensenius (2016) found dancers move more after the drop. [S; Solberg's 2014 *Dancecult* paper on build-ups and drops exists but I could not retrieve it.]
   - So a slow "build" value should be allowed to rise for 8–16 bars and **reset at the drop**. The reset is what makes the drop read.

---

## 4. Spawn, dissolve, fractal growth and infinite zoom in real-time shaders

| Technique | How it works | Usual audio tie |
|---|---|---|
| **Feedback zoom** (MilkDrop-style) | Each frame re-samples the previous frame slightly scaled, rotated or warped, then fades. In MilkDrop: `zoom` 1.01 = 1% inward per frame, `rot`, `warp`, `decay` (0.98 recommended; 0.9 is a strong fade), `dx/dy` drift. The doc's example: `zoom = zoom + 0.1*(my_volume-1)` with volume = (bass+mid+treb)/3. https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html [P] | Zoom rate on loudness or bass; decay on section energy (longer trails in breakdowns); new content injected on onsets |
| **Droste / log-polar zoom** | In log-polar coordinates (log r, θ), scaling becomes translation. Scroll `log r` over time and wrap it by log(scale ratio) for a seamless endless zoom; shear θ against log r for the Escher spiral. de Smit & Lenstra (2003) showed the *Print Gallery* is a conformal map whose straightened picture has a multiplicative period of **256**. https://pub.math.leidenuniv.nl/~smitbde/papers/2003-de_smit-lenstra-escher.pdf and https://pub.math.leidenuniv.nl/~smitbde/escherdroste/ [S] | Scroll *speed* on energy (in or out). Reverse direction on breakdowns ("falling through itself both ways"). Spiral twist on tension |
| **Layered self-similar zoom** ("infinite fall") | N copies of a texture or fractal at scales s^k, cross-faded by `fract(log(scale))` so a layer fades in as it shrinks in and fades out as it grows past the screen. Fabrice Neyret's "Infinite Fall" (Shadertoy 4sl3RX, short version ltjXWW), Shane's "Quasi Infinite Zoom Voronoi", fizzer's "Infinite Zoom" (MlXGW7), "Infinite Sierpinski" (MdfGR8). https://evasion.inrialpes.fr/~Fabrice.Neyret/demos/Shadertoy/ [S] | Zoom velocity from energy (a trend). A lurch per bar on downbeat. New layer seeds changed on onsets |
| **KIFS (kaleidoscopic IFS)** | Fold space by mirror planes, rotate, scale and translate, then iterate. A distance estimate allows ray marching. Created by Knighty on fractalforums; Syntopia (Hvidtfeldt) adds rotations before and after folds. https://blog.hvidtfeldts.net/index.php/2010/06/folding-space-ii-kaleidoscopic-fractals/ and https://blog.hvidtfeldts.net/index.php/category/kaleidoscopic-ifs/ [S] | Fold angle or rotation on a slow trend (melody, tension). Scale and offset on bass. Iteration count = "growth" (rises through a build). Rotations inside the loop break the distance field, so keep them modest |
| **Fractal outward growth** | Animate iteration depth or escape radius outward. Or use a radial reveal mask (r < growth) over a fractal whose detail is already computed. | Growth front advances on accumulated energy (a Time-style clock). Bursts on onsets |
| **Reaction–diffusion** (Gray–Scott) | Two chemicals A and B; A is fed (f) and B killed (k). Karl Sims' defaults: D_A 1.0, D_B 0.5, f 0.055, k 0.062. **Coral growth** at f 0.0545, k 0.062; **mitosis** (spots that grow and split) at f 0.0367, k 0.0649. Low feed gives faster motion. https://www.karlsims.com/rd.html [P] | **Seed B on onsets** (spawn). Nudge f/k with energy to switch between growth and mitosis (keep it inside the pattern band). Run more steps per frame on loud passages |
| **Particles: birth and death** | An emitter spawns on a trigger. Each particle carries a lifetime, and its alpha or size follows the age curve. | **Spawn count on onset strength** (kick for big bursts, hats for sparkles). Lifetime on tempo (≈ 1–2 beats). Notch recommends FFT-region triggers for kicks and snares [P] |
| **Noise-threshold dissolve** | `alpha = smoothstep(t-w, t, noise(p))`, plus an emissive rim where the threshold passes (the gap between two smoothsteps). Burn width ≈ 0.04. https://godotshaders.com/shader/2d-dissolve-with-burn-edge/ and https://gamedev.net/shaderlab/14-sprite-dissolve-effect/ [S] | Threshold `t` sweeps on decay or release after a hit ("things dissolve"), or across a section change as a transition. Rim brightness on highs |

Why these pair well with music [H]:
- Feedback, Droste and layered zoom give **continuous travel**, so they want a trend signal (speed).
- Particles, R–D seeds and dissolve give **discrete events**, so they want onset or section triggers.
- KIFS and growth give **structure**, so they want slow tension or melody.

That is the time-scale split from section 3, applied to the techniques.

---

## 5. How consumer apps present audio reactivity

- **Synesthesia** (pro VJ tool, yet its audio UI is minimal) [P], https://app.synesthesia.live/docs/manual/controls.html and https://app.synesthesia.live/docs/faq/audio.html
  - There is **no gain, threshold or band setup**. The engine handles level variation itself; "you don't need to set levels carefully". The only input choice is "Audio Device".
  - Users get two global sliders: **"Reactivity"** ("scales how sensitive all visual elements are to the incoming audio") and **"Audio Speed"** ("controls the rate of change for time-based visual variables").
  - Everything else is per-scene controls written by the scene author.
  - **Lesson:** two words cover most of what users want: *how much* and *how fast*.
- **Magic Music Visuals**
  - Features include "Volume" (mean absolute sample value per frame) and "Freq. Range" (volume of a frequency band).
  - Modifiers include Peak, Smooth, Average, Scale and Offset. https://magicmusicvisuals.com/forums/viewtopic.php?p=8215 [S]
  - It is node-based and aimed at semi-pros.
- **Vizzy** (music video maker) [P], https://vizzydocs.igerman.cc/Automations/Types/Audio-Impact-Automation. "Audio Impact Automation" controls:
  - **Min value** (quiet) and **Max value** (loud);
  - **Lower limit** (no reaction below it);
  - **Sensitivity** (0–1) and **Smoothing** (0–1);
  - **Cumulative** (values build up over time);
  - **Delta smooth**, **Delta amount** (how fast values decay) and **Delta threshold**.

  This is the clearest consumer vocabulary I found: *quiet value, loud value, sensitivity, smoothing, build up*.
- **Specterr** (browser music-video maker) [S], https://violetrecording.com/specterr-review/
  - Visual types: "waveforms", "frequency spectrums", "radial bars", "particle bursts" and "simple animated backgrounds".
  - Customisation is colour, logo, text and background, and reactivity is preset rather than exposed. I could not verify its effect names (shake, glitch, zoom).
- **Renderforest / VEED** [S]
  - Templates are described as "automatically generated visuals that react to the music". VEED says "customize your sound waves to mirror your music's rhythm and mood".
  - Neither exposes reaction parameters; the user picks a template. https://www.renderforest.com/music-visualizer-videos and https://veed.io/music-visualisation
- **CapCut / TikTok**
  - CapCut's **"Auto Beat"** puts markers on rhythmic peaks.
  - **"Beat Sync"** inside **Auto Cut** builds a draft edit whose "cuts, zooms, and transitions" land on the markers. https://www.tourboxtech.com/en/news/how-to-add-music-to-capcut.html [S]
  - TikTok's **"Sound Sync"** auto-trims several clips to a sound's beats, for some songs only. https://www.dexerto.com/entertainment/how-to-sync-videos-to-a-sound-on-tiktok-1703964/ [S]
  - **Lesson:** the mass-market idea is the *beat as a moment where something happens* (cut, zoom, transition), not continuous mapping.
- **Kaleidosync** (WebGL sketches on Spotify)
  - It was driven by Spotify's precomputed analysis: **beats, bars, tatums, segments (timbre and pitch), sections** ("large variations in rhythm or timbre, e.g. chorus, verse, bridge"). https://github.com/zachwinter/kaleidosync and https://rubydoc.info/gems/spotify-api-sdk/1.0.0/SpotifyWebApi/AudioAnalysisObject [S]
  - Spotify restricted that endpoint for new apps in late 2024 (from memory, not verified here).
  - **Lesson:** "bar" and "section" are user-legible units.
- **Plane9.** 150+ scenes; I found no public docs on its sound routing. [unverified]
- **Generic sliders seen in configs:** "Sensitivity", and "reactivity" 0–2 (0 = static, 1 = normal, 2 = hypersensitive). https://docs.rs/koan-core/latest/koan_core/config/struct.VisualizerConfig.html [S]

**Words that recur** across consumer and prosumer tools: *Reactivity / Sensitivity*, *Speed*, *Smoothing*, *Beat*, *Bass / Mids / Highs*, *Hits*, *Level*, *Build up (cumulative)*, *Drop*, *Sync to beat*, *Auto*. Users never see "onset", "centroid", "flux" or "RMS".

**Presets that ship:** mostly *looks* (template or scene), each with its reactivity baked in. Plus one or two global scalars (Synesthesia's Reactivity and Audio Speed), or a beat-sync toggle (CapCut, TikTok).

---

## Implications for Geode

### (a) Camera with independent fly / dolly / orbit / crane / roll / shake / cut channels

Each channel gets one default source and one time scale, so no two channels share a signal by default.

| Channel | Default source | Time scale | Shape |
|---|---|---|---|
| **Cut** | Section change (always) + every N bars (N = 2 default; 1 above energy threshold; 4 in breakdowns) | Section / bar | Optional −1/16 anticipation. Beat-rate cut bursts allowed only in the last bar of a build. Minimum gap ≈ 1.3 s |
| **Fly (forward speed)** | Energy trend (Presence-like, seconds) | Trend | Intensification means faster, per Eitan & Granot. Kick adds a small forward lurch (fast attack, ~1 beat release) |
| **Dolly (push / pull)** | Build detector (rising energy or density over 4–16 bars) | Phrase | Slow push through the build, land on the drop. Pull back on breakdowns (abatement means settle) |
| **Orbit** | Constant drift, rate scaled by a sustained-energy or pad level | Trend | Runs as a volume-advanced clock (like `syn_Time`). Never jumps |
| **Crane (height)** | Melody or centroid height trend (pitch means height), plus a rise-and-widen reveal on drop or chorus downbeat | Trend + section event | Reveal lasts 1–2 bars. Falling energy sinks the camera |
| **Roll** | Snare backbeat toggle (`ToggleOnBeat`-style, ± a few degrees), or slow drift on tension | Beat / trend | Small amplitude. Two-state, not wobble |
| **Shake** | Kick / snare onset strength → trauma | Transient | shake = trauma² × noise; trauma decays in about 0.3–0.6 s. Scale down in breakdowns |
| **Time-scale (ramp)** | Last beat of a build → 0.25×, drop → 1× (brief 1.5× overshoot) | Section event | Applies to the camera clock, not the audio |

Global rules:
- At most one big move per section.
- Hold a bar after a big move.
- Don't cut mid-move except on a section change.
- Shake and roll stack on top of position channels; they never drive them.

### (b) Spawn, fractal and zoom-through layers

- **Zoom-through, Droste and feedback:** speed from the energy trend, sign flipped in breakdowns (falling back out). A lurch on downbeats. Spiral twist on tension.
  - Use it as the **transition between styles**: accelerate over half a bar, swap at peak speed on the section downbeat, decelerate.
- **Spawn (particles, R–D seeds):**
  - Count from onset strength. Kick spawns big, few and long-lived (≈ 2 beats); hats spawn small, many and short-lived (≈ ½ beat).
  - Divide big bursts by 4, 8 or 16 kicks, or tie them to section change.
- **Dissolve:** the threshold sweeps with each spawn's release envelope, so things "die" on the decay, not the hit. Rim glow on highs.
- **Fractal growth (KIFS, R–D):** iteration depth or growth front on a song-level accumulator (rises through a build, resets on drop). Fold angle on melody or tension (slow). Feed/kill nudged by energy, within the pattern band (coral f 0.0545/k 0.062 ↔ mitosis f 0.0367/k 0.0649).
- **Shape and texture follow timbre:** a brighter or harsher mix (centroid, flatness) means sharper or angular folds and coarser texture. Mellow means rounder and smoother (Adeli; Giannakis).
- **Palette:** a slow drift toward saturated, light and warm on fast, major, high-energy passages, and toward desaturated, dark and cool on slow or minor ones (Palmer 2013). Never per-beat hue jumps.

### (c) A plain-language reaction editor

- **Three layers, Arfib/Hunt style:**
  - Sources the user sees are about 8 "meaning" signals: **Kick, Snare, Hats, Bass, Melody, Energy, Build, Section change**.
  - Each meaning signal is built natively from the 72 sound-frame values.
  - Targets are camera channels and style params.
  - Expert mode can still expose the raw 72.
- **Per-route words:**
  - **Amount** (how much);
  - **Speed** (Snappy ↔ Smooth: attack/release presets, not milliseconds);
  - **When**: Every hit / Every beat / Every bar / Every 4 bars / Section change;
  - **Quiet → Loud** values (Vizzy's min/max);
  - **Builds up** (a cumulative toggle).
- **Two global sliders, Synesthesia-style:** **Reactivity** (scales every route) and **Motion speed** (scales the time-based clocks).
- **Presets named by feel and music shape** rather than by signal: "Punchy (kick-led)", "Floaty (melody-led)", "Builds & drops", "Calm". Each preset fills the per-channel table in (a), so a beginner gets layered, non-colliding motion without editing.
- **Simple first, coupled for power** (Hunt & Kirk 2000): one-to-one routes are easiest to learn. Coupled "macro" routes (one meaning signal → several targets) do better once several things change at once. Ship macros as presets and let the routes be edited.
- **Collision warning:** if two routes on the same time scale hit the same target, or one source drives more than about three targets, show a "too much on one signal" hint.
