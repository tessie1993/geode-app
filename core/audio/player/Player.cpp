#include "audio/player/Player.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

namespace geode::audio::player {

namespace {

constexpr int64_t kRingSeconds = 2;
constexpr size_t kDecodeChunk = 1024;
constexpr int64_t kPrerollFrames = 4096;

int64_t usToFrames(int64_t us, int rate) { return us * rate / 1'000'000; }

int64_t framesToUs(int64_t frames, int rate) { return rate > 0 ? frames * 1'000'000 / rate : 0; }

}  // namespace

struct Player::Track {
    std::unique_ptr<Decoder> decoder;
    Resampler resampler;
    Deck* deck = nullptr;
    int64_t token = 0;
    int inputRate = 0;
    bool padded = false;
    bool drained = false;
    std::vector<float> decodeBuf = std::vector<float>(kDecodeChunk * Mixer::kChannels);
    std::vector<float> resampleBuf = std::vector<float>(kDecodeChunk * Mixer::kChannels);
};

Player::Player() : thread_([this] { run(); }) {}

Player::~Player() {
    {
        std::lock_guard<std::mutex> guard(lock_);
        quit_ = true;
    }
    wake_.notify_all();
    thread_.join();
    output_.close();
}

void Player::open(int fd, int64_t offset, int64_t length, int64_t token) {
    enqueue({Command::Kind::Open, fd, offset, length, token});
}

void Player::setNext(int fd, int64_t offset, int64_t length, int64_t token) {
    enqueue({Command::Kind::SetNext, fd, offset, length, token});
}

void Player::play() {
    playWhenReady_.store(true, std::memory_order_release);
    enqueue({Command::Kind::Play});
}

void Player::pause() {
    playWhenReady_.store(false, std::memory_order_release);
    enqueue({Command::Kind::Pause});
}

void Player::stop() {
    playWhenReady_.store(false, std::memory_order_release);
    enqueue({Command::Kind::Stop});
}

void Player::seek(int64_t positionUs) {
    Command command{Command::Kind::Seek};
    command.positionUs = positionUs;
    enqueue(command);
}

void Player::setCrossfade(int durationMs, int curve) {
    Command command{Command::Kind::Crossfade};
    command.durationMs = durationMs;
    command.curve = curve;
    enqueue(command);
}

std::string Player::lastError() const {
    std::lock_guard<std::mutex> guard(errorLock_);
    return lastError_;
}

void Player::enqueue(Command command) {
    {
        std::lock_guard<std::mutex> guard(lock_);
        queue_.push_back(command);
    }
    wake_.notify_one();
}

void Player::run() {
    std::deque<Command> batch;
    for (;;) {
        {
            std::unique_lock<std::mutex> guard(lock_);
            const auto wait = current_ ? std::chrono::milliseconds(5) : std::chrono::milliseconds(100);
            wake_.wait_for(guard, wait, [&] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            batch.swap(queue_);
        }
        reconcile();
        for (const Command& command : batch) handle(command);
        batch.clear();
        reconcile();
        if (output_.disconnected()) reopenOutput();
        pump();
        updateState();
    }
    doStop();
}

void Player::handle(const Command& command) {
    switch (command.kind) {
        case Command::Kind::Open: doOpen(command); break;
        case Command::Kind::SetNext: doSetNext(command); break;
        case Command::Kind::Play: break;
        case Command::Kind::Pause:
            if (output_.running()) output_.pause();
            break;
        case Command::Kind::Stop: doStop(); break;
        case Command::Kind::Seek: doSeek(command.positionUs); break;
        case Command::Kind::Crossfade:
            crossfadeMs_ = std::max(command.durationMs, 0);
            curve_ = command.curve;
            mixer_.setCrossfade(crossfadeFrames(), curve_);
            break;
    }
}

int64_t Player::crossfadeFrames() const {
    return usToFrames(static_cast<int64_t>(crossfadeMs_) * 1000, outRate_.load(std::memory_order_relaxed));
}

std::unique_ptr<Player::Track> Player::loadTrack(int fd, int64_t offset, int64_t length, int64_t token) {
    std::string error;
    auto decoder = Decoder::open(fd, offset, length, error);
    if (!decoder) {
        noteError(error);
        return nullptr;
    }
    auto track = std::make_unique<Track>();
    track->decoder = std::move(decoder);
    track->token = token;
    newDeck(*track, 0);
    return track;
}

void Player::doOpen(const Command& command) {
    if (!ensureOutput()) {
        ::close(command.fd);
        return;
    }
    auto track = loadTrack(command.fd, command.offset, command.length, command.token);
    if (!track) {
        fail(lastError());
        return;
    }
    noteError({});
    postCommand({DeckCommand::Op::SetCurrent, track->deck});
    if (next_) {
        postCommand({DeckCommand::Op::ClearNext, nullptr});
        next_.reset();
    }
    current_ = std::move(track);
    state_.store(PlayerState::Buffering, std::memory_order_release);
}

void Player::doSetNext(const Command& command) {
    if (next_) {
        postCommand({DeckCommand::Op::ClearNext, nullptr});
        next_.reset();
    }
    if (command.fd < 0) return;
    if (!ensureOutput()) {
        ::close(command.fd);
        return;
    }
    auto track = loadTrack(command.fd, command.offset, command.length, command.token);
    if (!track) return;
    postCommand({DeckCommand::Op::SetNext, track->deck});
    next_ = std::move(track);
}

void Player::doSeek(int64_t positionUs) {
    if (!current_) return;
    Track& track = *current_;
    if (!track.decoder->seek(positionUs)) {
        fail(track.decoder->error());
        return;
    }
    track.resampler.reset();
    newDeck(track, usToFrames(std::max<int64_t>(positionUs, 0), outRate_.load(std::memory_order_relaxed)));
    postCommand({DeckCommand::Op::SetCurrent, track.deck});
    state_.store(PlayerState::Buffering, std::memory_order_release);
}

void Player::doStop() {
    if (output_.running()) output_.pause();
    postCommand({DeckCommand::Op::SetCurrent, nullptr});
    postCommand({DeckCommand::Op::ClearNext, nullptr});
    current_.reset();
    next_.reset();
    reconcile();
    state_.store(PlayerState::Idle, std::memory_order_release);
}

bool Player::ensureOutput() {
    if (output_.isOpen()) return true;
    std::string error;
    if (!output_.open(error)) {
        fail(error);
        return false;
    }
    outRate_.store(output_.sampleRate(), std::memory_order_release);
    mixer_.setCrossfade(crossfadeFrames(), curve_);
    return true;
}

// After a route change the device may run at another rate; both tracks are rebuilt at the old position.
void Player::reopenOutput() {
    const int64_t positionUs = positionUs_.load(std::memory_order_relaxed);
    std::string error;
    if (!output_.open(error)) {
        fail(error);
        return;
    }
    const int rate = output_.sampleRate();
    if (rate == outRate_.load(std::memory_order_relaxed)) return;
    outRate_.store(rate, std::memory_order_release);
    mixer_.setCrossfade(crossfadeFrames(), curve_);
    if (current_) {
        current_->inputRate = 0;
        doSeek(positionUs);
    }
    if (next_) {
        next_->inputRate = 0;
        next_->decoder->seek(0);
        next_->resampler.reset();
        newDeck(*next_, 0);
        postCommand({DeckCommand::Op::SetNext, next_->deck});
    }
}

void Player::reconcile() {
    while (Deck* retired = mixer_.takeRetired()) {
        decks_.erase(std::remove_if(decks_.begin(), decks_.end(), [&](const auto& d) { return d.get() == retired; }),
                     decks_.end());
    }
    Deck* playing = mixer_.currentDeck();
    if (next_ && playing == next_->deck) {
        current_ = std::move(next_);
        next_.reset();
    }
}

void Player::pump() {
    if (current_) fill(*current_);
    if (next_) fill(*next_);
}

void Player::fill(Track& track) {
    if (track.drained || !track.deck) return;
    Decoder& decoder = *track.decoder;
    const int outRate = outRate_.load(std::memory_order_relaxed);
    for (int round = 0; round < 8; ++round) {
        size_t space = track.deck->ring.writable() / Mixer::kChannels;
        while (space > 0) {
            const size_t n = track.resampler.pull(track.resampleBuf.data(), std::min(space, kDecodeChunk));
            if (n == 0) break;
            track.deck->ring.push(track.resampleBuf.data(), n * Mixer::kChannels);
            space -= n;
        }
        if (space < kDecodeChunk * 2) return;
        if (decoder.sampleRate() != track.inputRate) {
            track.inputRate = decoder.sampleRate();
            track.resampler.setRatio(track.inputRate, outRate);
        }
        const size_t got = decoder.readStereo(track.decodeBuf.data(), kDecodeChunk);
        if (decoder.failed()) {
            fail(decoder.error());
            return;
        }
        if (got > 0) {
            track.resampler.push(track.decodeBuf.data(), got);
            continue;
        }
        if (!decoder.ended()) return;
        if (!track.padded) {
            // The interpolator needs two frames of look-ahead to release the tail.
            const std::array<float, 3 * Mixer::kChannels> silence{};
            track.resampler.push(silence.data(), 3);
            track.padded = true;
            continue;
        }
        if (!track.resampler.ready()) {
            track.deck->endOfStream.store(true, std::memory_order_release);
            track.drained = true;
        }
        return;
    }
}

Deck* Player::newDeck(Track& track, int64_t startFrame) {
    const int outRate = outRate_.load(std::memory_order_relaxed);
    const int64_t durationFrames = usToFrames(track.decoder->durationUs(), outRate);
    decks_.push_back(std::make_unique<Deck>(static_cast<size_t>(kRingSeconds * outRate), track.token, startFrame,
                                            durationFrames));
    track.deck = decks_.back().get();
    track.padded = false;
    track.drained = false;
    return track.deck;
}

void Player::postCommand(DeckCommand command) {
    while (!mixer_.post(command)) {
        if (output_.running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            mixer_.drainCommands();
        }
    }
    if (!output_.running()) mixer_.drainCommands();
}

void Player::updateState() {
    if (!current_) {
        if (state_.load(std::memory_order_relaxed) != PlayerState::Error) state_.store(PlayerState::Idle, std::memory_order_release);
        token_.store(-1, std::memory_order_release);
        positionUs_.store(0, std::memory_order_release);
        durationUs_.store(0, std::memory_order_release);
        return;
    }
    const int outRate = outRate_.load(std::memory_order_relaxed);
    Deck& deck = *current_->deck;
    const bool applied = mixer_.currentDeck() == &deck;
    positionUs_.store(framesToUs(applied ? deck.position() : deck.startFrame, outRate), std::memory_order_release);
    durationUs_.store(current_->decoder->durationUs(), std::memory_order_release);
    token_.store(current_->token, std::memory_order_release);
    if (state_.load(std::memory_order_relaxed) == PlayerState::Error) return;
    PlayerState state = PlayerState::Buffering;
    if (applied && mixer_.ended()) {
        state = PlayerState::Ended;
    } else {
        const int64_t buffered = static_cast<int64_t>(deck.ring.readable() / Mixer::kChannels);
        if (buffered >= kPrerollFrames || deck.endOfStream.load(std::memory_order_acquire)) state = PlayerState::Ready;
    }
    state_.store(state, std::memory_order_release);
    const bool wantRunning = playWhenReady_.load(std::memory_order_acquire) && state == PlayerState::Ready;
    if (wantRunning && !output_.running()) {
        output_.start();
    } else if (!wantRunning && output_.running() && state != PlayerState::Buffering) {
        output_.pause();
    }
}

void Player::noteError(const std::string& message) {
    std::lock_guard<std::mutex> guard(errorLock_);
    lastError_ = message;
}

void Player::fail(const std::string& message) {
    noteError(message);
    if (output_.running()) output_.pause();
    postCommand({DeckCommand::Op::SetCurrent, nullptr});
    postCommand({DeckCommand::Op::ClearNext, nullptr});
    current_.reset();
    next_.reset();
    reconcile();
    state_.store(PlayerState::Error, std::memory_order_release);
}

}  // namespace geode::audio::player
