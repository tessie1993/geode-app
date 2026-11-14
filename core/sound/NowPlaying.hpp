#pragma once

// Which of the other apps' media sessions the app names as playing (Settings › Other apps, the Player's
// title while a capture runs). The shell lists the sessions it may see (its own left out) in the order
// MediaSessionManager.getActiveSessions gives them, newest first, and says which are in the playing
// state; this picks one. The first playing session wins, so the app that just started playing is named;
// with none playing the first listed, the one most recently active. -1 with none at all.
//
// Pure, any thread.
namespace geode::sound::now_playing {

inline int pick(const int* playing, int count) {
    if (!playing || count <= 0) return -1;
    for (int i = 0; i < count; ++i) {
        if (playing[i] != 0) return i;
    }
    return 0;
}

}  // namespace geode::sound::now_playing
