// Turns packets that arrive in bursts into a steady angle.
//
// The game makes one packet per frame (about 60 a second), but a home network delivers them
// unevenly, sometimes many at once. Each packet carries its frame number, so this buffer
// plays them back by that number, a little later than they arrive, and interpolates between
// two frames. The wheel then moves as evenly as the car did.
#pragma once
#include <cstdint>
#include <deque>
#include <utility>

class Playout {
public:
    static constexpr double kFrame = 1.0 / 59.94;  // seconds between two packets of the game

    void reset();
    void push(int32_t packetId, double angleDeg, double arrival);

    // Angle for the moment `now - delay`. `late` says for how long the buffer has had nothing
    // newer to offer (0 while data flows); the caller decides when that counts as silence.
    // False until the first packet.
    bool sample(double now, double delay, double& angleDeg, double& late) const;

private:
    struct Frame {
        int32_t id;
        double angle;
    };
    std::deque<Frame> frames_;                    // ascending by id, the last few seconds
    std::deque<std::pair<double, double>> lags_;  // (arrival, arrival - id * kFrame)
    double offset_ = 0;                           // smallest lag of the last seconds: the clock offset
    int behind_ = 0;                              // packets in a row with an id below the newest
};
