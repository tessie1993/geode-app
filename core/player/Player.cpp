#include "player/Player.hpp"

#include <utility>

namespace geode::player {

namespace {
// Tracks in a row that would not play before the player stops and says the source is gone. The same rule
// as audio::errorAction (core/audio/dsp/PlaybackControls), which this replaces.
constexpr int kMaxConsecutiveFailures = 3;
}  // namespace

void Player::apply(const Command& command) {
    const bool wasPlaying = transport_.playing();
    std::visit([this](const auto& c) { handle(c); }, command);
    if (transport_.playing() == wasPlaying) return;
    Event e;
    e.kind = Event::Kind::PlayingChanged;
    e.playing = transport_.playing();
    events_.push_back(e);
}

PlayerState Player::state() const {
    PlayerState s;
    s.playing = transport_.playing();
    s.ended = transport_.ended();
    s.size = queue_.size();
    s.position = queue_.position();
    if (const Track* t = queue_.current()) s.current = t->id;
    s.positionMs = transport_.positionMs();
    s.durationMs = transport_.durationMs();
    s.repeat = queue_.repeat();
    s.shuffle = queue_.shuffled();
    s.restartAtEnd = restartAtEnd_;
    s.abLoop = transport_.loop();
    s.hasNext = queue_.hasNext();
    if (const Track* t = queue_.following()) s.upNext = t->id;
    s.order = queue_.ids();
    return s;
}

std::vector<Event> Player::takeEvents() { return std::exchange(events_, {}); }

void Player::enterTrack(Event::Reason reason, std::int64_t startMs) {
    const Track* t = queue_.current();
    transport_.load(t->durationMs, startMs);
    Event e;
    e.kind = Event::Kind::TrackChanged;
    e.reason = reason;
    e.id = t->id;
    e.position = queue_.position();
    e.positionMs = transport_.positionMs();
    events_.push_back(e);
}

void Player::emitSeeked(bool loop) {
    Event e;
    e.kind = Event::Kind::Seeked;
    e.positionMs = transport_.positionMs();
    e.loop = loop;
    events_.push_back(e);
}

void Player::emitEnded() {
    Event e;
    e.kind = Event::Kind::QueueEnded;
    e.id = queue_.current()->id;
    events_.push_back(e);
}

namespace {
Event queueChanged() {
    Event e;
    e.kind = Event::Kind::QueueChanged;
    return e;
}
}  // namespace

void Player::handle(const SetQueue& c) {
    queue_.set(c.tracks, c.startIndex);
    events_.push_back(queueChanged());
    if (queue_.empty()) {
        transport_.unload();
        return;
    }
    transport_.setPlayWhenReady(c.play);
    enterTrack(Event::Reason::Requested, c.startPositionMs);
}

void Player::handle(const Play&) {
    if (queue_.empty()) return;
    if (transport_.ended()) {
        transport_.restart();
        emitSeeked(false);
    }
    transport_.setPlayWhenReady(true);
}

void Player::handle(const Pause&) { transport_.setPlayWhenReady(false); }

void Player::handle(const Seek& c) {
    if (queue_.empty()) return;
    if (transport_.seek(c.positionMs)) emitSeeked(false);
}

void Player::handle(const Next&) {
    if (queue_.empty()) return;
    queue_.skipNext();
    enterTrack(Event::Reason::Skip);
}

void Player::handle(const Previous&) {
    if (queue_.empty()) return;
    if (transport_.pastRestartPoint()) {
        transport_.restart();
        emitSeeked(false);
        return;
    }
    queue_.skipPrevious();
    enterTrack(Event::Reason::Skip);
}

void Player::handle(const PlayPosition& c) {
    if (!queue_.select(c.position)) return;
    transport_.setPlayWhenReady(true);
    enterTrack(Event::Reason::Requested);
}

void Player::handle(const SetRepeat& c) { queue_.setRepeat(c.repeat); }

void Player::handle(const SetShuffle& c) {
    const std::vector<TrackId> before = queue_.ids();
    queue_.setShuffle(c.on, c.seed);
    if (queue_.ids() != before) events_.push_back(queueChanged());
}

void Player::handle(const SetRestartAtEnd& c) { restartAtEnd_ = c.on; }

void Player::handle(const CycleAbLoop&) {
    if (!queue_.empty()) transport_.cycleLoop();
}

void Player::handle(const ClearAbLoop&) { transport_.clearLoop(); }

void Player::handle(const Insert& c) {
    if (c.tracks.empty()) return;
    const bool first = queue_.insert(c.at == Insert::At::End ? Queue::At::End : Queue::At::AfterCurrent, c.tracks);
    events_.push_back(queueChanged());
    if (first) enterTrack(Event::Reason::Requested);
}

void Player::handle(const Remove& c) {
    const int before = queue_.size();
    const Queue::Removal r = queue_.remove(c.position);
    if (queue_.size() == before) return;  // nothing was there to remove
    events_.push_back(queueChanged());
    if (!r.current) return;
    if (queue_.empty()) {
        transport_.unload();
        return;
    }
    enterTrack(Event::Reason::Edit);
    if (!r.ended) return;
    transport_.finish();
    emitEnded();
}

void Player::handle(const Move& c) {
    const std::vector<TrackId> before = queue_.ids();
    queue_.move(c.from, c.to);
    if (queue_.ids() != before) events_.push_back(queueChanged());
}

void Player::handle(const SetDuration& c) {
    queue_.setDuration(c.id, c.durationMs);
    const Track* t = queue_.current();
    if (t != nullptr && t->id == c.id) transport_.setDuration(c.durationMs);
}

void Player::handle(const Tick& c) {
    if (queue_.empty() || transport_.ended()) return;
    if (transport_.playing()) failures_ = 0;
    if (transport_.tick(c.positionMs) != kNoPoint) emitSeeked(true);
}

void Player::handle(const TrackEnded&) {
    if (queue_.empty() || transport_.ended()) return;
    switch (queue_.advance()) {
        case Queue::Auto::Repeated: enterTrack(Event::Reason::Repeat); return;
        case Queue::Auto::Advanced: enterTrack(Event::Reason::Auto); return;
        case Queue::Auto::End: break;
    }
    if (restartAtEnd_) {
        queue_.select(0);
        transport_.setPlayWhenReady(true);
        enterTrack(Event::Reason::QueueRestart);
        return;
    }
    transport_.finish();
    emitEnded();
}

void Player::handle(const TrackFailed&) {
    if (queue_.empty()) return;
    ++failures_;
    Event e;
    e.kind = Event::Kind::PlaybackFailed;
    e.id = queue_.current()->id;
    if (failures_ >= kMaxConsecutiveFailures) {
        e.failure = Event::Failure::SourceUnavailable;
    } else if (queue_.hasNext()) {
        e.failure = Event::Failure::Skipped;
    } else {
        e.failure = Event::Failure::EndOfQueue;
    }
    events_.push_back(e);
    if (e.failure != Event::Failure::Skipped) {
        transport_.setPlayWhenReady(false);
        return;
    }
    queue_.skipNext();
    enterTrack(Event::Reason::Error);
}

}  // namespace geode::player
