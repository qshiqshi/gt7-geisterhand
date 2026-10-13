#include "playout.h"

#include <algorithm>

namespace {
constexpr int kKeepFrames = 240;       // four seconds of history
constexpr double kLagWindow = 5.0;     // seconds over which the smallest lag is taken
constexpr int kRestartBack = 120;      // an id this far back means a new session, not a stray packet
constexpr int kRestartRun = 10;        // and so do this many older ids in a row
constexpr int kRestartAhead = 600;     // a gap of ten seconds: start over as well
}  // namespace

void Playout::reset() {
    frames_.clear();
    lags_.clear();
    offset_ = 0;
    behind_ = 0;
}

void Playout::push(int32_t packetId, double angleDeg, double arrival) {
    if (!frames_.empty()) {
        const int32_t newest = frames_.back().id;
        if (packetId <= newest) {
            // One older id is a stray packet. Far back, or many in a row: the game counts anew.
            if (newest - packetId < kRestartBack && ++behind_ < kRestartRun) return;
            reset();
        } else if (packetId - newest > kRestartAhead) {
            reset();
        }
    }
    behind_ = 0;
    frames_.push_back({packetId, angleDeg});
    while (frames_.size() > size_t(kKeepFrames)) frames_.pop_front();

    // A packet can only arrive late, never early, so the smallest lag seen lately is the
    // offset between the game's frame clock and ours. Taking it over a window lets it follow
    // a slow drift of the two clocks.
    lags_.emplace_back(arrival, arrival - packetId * kFrame);
    while (!lags_.empty() && arrival - lags_.front().first > kLagWindow) lags_.pop_front();
    offset_ = lags_.front().second;
    for (const auto& lag : lags_) offset_ = std::min(offset_, lag.second);
}

bool Playout::sample(double now, double delay, double& angleDeg, double& late) const {
    if (frames_.empty()) return false;
    const double id = (now - delay - offset_) / kFrame;  // the frame that is due, with fraction
    late = 0;

    const Frame& newest = frames_.back();
    if (id >= newest.id) {
        angleDeg = newest.angle;
        late = (id - newest.id) * kFrame;
        return true;
    }
    if (id <= frames_.front().id) {
        angleDeg = frames_.front().angle;
        return true;
    }
    auto after = std::upper_bound(frames_.begin(), frames_.end(), id,
                                  [](double value, const Frame& frame) { return value < frame.id; });
    const Frame& b = *after;
    const Frame& a = *(after - 1);
    const double part = (id - a.id) / double(b.id - a.id);
    angleDeg = a.angle + (b.angle - a.angle) * part;
    return true;
}
