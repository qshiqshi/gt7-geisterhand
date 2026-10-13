// The thread that owns the wheel: reads its angle, asks the telemetry where it should be,
// runs the control loop and writes the force. The window only sends wishes and reads a
// snapshot, so a window that is being dragged or has frozen never leaves a force standing.
#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "controller.h"
#include "telemetry.h"
#include "wheel.h"

struct EngineSettings {
    double cap = 0.20;          // highest force, 0..1
    double rangeDeg = 900;      // lock to lock as set on the wheel
    bool invert = false;        // turn the replay's direction round
    double factor = 1.0;        // wheel turns this many times the replay's angle
    double delayS = 0.08;       // playout buffer
    double stiffness = 1.0;
};

struct EngineView {
    enum class State { NoWheel, Ready, Probing, Following, Waiting };
    enum class Stop { None, User, Button, Fault, WheelLost, Driver };
    State state = State::NoWheel;
    Stop lastStop = Stop::None;      // why the last run ended
    Fault lastFault = Fault::None;   // with Stop::Fault
    std::string detail;              // what the driver said, with Stop::Driver or a failed open
    bool canTurn = false;            // the chosen device takes forces
    double wheelDeg = 0;
    bool haveTarget = false;
    double targetDeg = 0;
    double force = 0;                // as applied, -1..1
    double gain = 1;                 // loop gain in use for this wheel, 0..1
};

class Engine {
public:
    explicit Engine(Telemetry& telemetry);
    ~Engine();

    void select(const std::optional<WheelInfo>& wheel);
    void configure(const EngineSettings& settings);
    void start();
    void stop();
    EngineView view() const;

private:
    void run();

    Telemetry& telemetry_;
    std::thread thread_;
    std::atomic<bool> quit_{false};

    mutable std::mutex mutex_;
    EngineSettings settings_;
    std::optional<WheelInfo> selection_;
    bool selectionChanged_ = false;
    bool wantStart_ = false;
    bool wantStop_ = false;
    EngineView view_;
};
