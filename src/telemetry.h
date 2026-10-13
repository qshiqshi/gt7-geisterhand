// Receives the telemetry of Gran Turismo 7 from a PlayStation in the home network.
//
// The game only sends to a computer that keeps asking for it: one byte to UDP port 33739
// every second ("heartbeat"); the packets then arrive on port 33740. Without an address the
// whole home network is asked until a PlayStation answers.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "playout.h"

struct TelemetryStatus {
    enum class Link {
        Starting,
        PortBusy,     // another program on this computer already listens for the game
        Searching,    // no packet yet
        Receiving,
        NoSteering,   // packets arrive, but in the short format without the steering angle
        Silent,       // packets came and stopped
    };
    Link link = Link::Starting;
    std::string address;        // where the packets come from
    double packetsPerSecond = 0;
    double speedKmh = 0;
    bool paused = false;
};

class Telemetry {
public:
    struct Options {
        std::string address;    // empty: search the home network
        int receivePort = 33740;
        int sendPort = 33739;
        bool demo = false;      // no network at all: a made-up drive
    };

    ~Telemetry();
    void start(const Options& options);
    void stop();
    void setAddress(const std::string& address);
    TelemetryStatus status() const;

    // Steering wheel angle as the game reports it (degrees, positive = left) for the moment
    // `now - delay`. `late`: for how long nothing newer has arrived. False before any packet.
    bool steering(double now, double delay, double& degLeft, double& late) const;

private:
    void run();
    void runDemo();

    Options options_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    Playout playout_;
    TelemetryStatus status_;
    std::string wanted_;       // address typed by the user
    bool wantedChanged_ = false;
};
