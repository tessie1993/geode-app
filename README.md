# Test music

Five tracks the owner supplied to test the app on (~3 min each, 44.1 kHz stereo MP3). Kept on this
orphan branch so the app's history never carries the audio. CI's Sound golden job checks it out,
decodes each track and runs `sound_golden_test --pcm` on it: every field must agree at 30, 60 and
120 fps, and the engine's tempo, key, scale and sections are printed for an ear to judge.
