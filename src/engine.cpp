#include "engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "clock.h"

namespace {
constexpr double kSilenceS = 0.35;   // no newer packet for this long: nothing to follow
constexpr double kGiveUpS = 10.0;    // nothing to follow for this long: stop, do not start by surprise later
constexpr double kIdleHz = 60;
}  // namespace

Engine::Engine(Telemetry& telemetry) : telemetry_(telemetry) {
    thread_ = std::thread([this] { run(); });
}

Engine::~Engine() {
    quit_ = true;
    if (thread_.joinable()) thread_.join();
}

void Engine::select(const std::optional<WheelInfo>& wheel) {
    std::lock_guard<std::mutex> lock(mutex_);
    selection_ = wheel;
    selectionChanged_ = true;
}

void Engine::configure(const EngineSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
}

void Engine::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    wantStart_ = true;
    wantStop_ = false;
}

void Engine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    wantStop_ = true;
    wantStart_ = false;
}

EngineView Engine::view() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return view_;
}

void Engine::run() {
    std::unique_ptr<Wheel> wheel;
    WheelInfo info;
    Controller controller;
    bool engaged = false;
    uint64_t heldAtStart = 0;
    int steerAxis = 0;          // which axis of the device is the steering
    WheelReading reading, atStart;
    double waitingSince = -1;
    EngineView view;
    double next = monotonicSeconds();

    const auto release = [&](EngineView::Stop why, Fault fault, const std::string& detail) {
        if (wheel && engaged) wheel->disengage();
        engaged = false;
        controller.stop();
        view.lastStop = why;
        view.lastFault = fault;
        view.detail = detail;
        view.force = 0;
    };

    while (!quit_) {
        EngineSettings settings;
        std::optional<WheelInfo> selection;
        bool selectionChanged, wantStart, wantStop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            settings = settings_;
            selectionChanged = selectionChanged_;
            if (selectionChanged) selection = selection_;
            wantStart = wantStart_;
            wantStop = wantStop_;
            selectionChanged_ = wantStart_ = wantStop_ = false;
        }

        if (selectionChanged) {
            release(EngineView::Stop::None, Fault::None, "");
            wheel.reset();
            controller.forgetWheel();
            steerAxis = 0;
            reading = WheelReading{};
            view = EngineView{};
            if (selection) {
                info = *selection;
                std::string error;
                wheel = openWheel(info, error);
                view.detail = error;
            }
        }
        if (wantStop && engaged) release(EngineView::Stop::User, Fault::None, "");

        const double t = monotonicSeconds();
        if (wheel) {
            wheel->setRange(settings.rangeDeg);
            if (!wheel->read(reading)) {  // unplugged
                const bool wasRunning = engaged;
                release(wasRunning ? EngineView::Stop::WheelLost : EngineView::Stop::None, Fault::None, "");
                wheel.reset();
                reading = WheelReading{};
            }
        }
        if (steerAxis >= reading.axisCount) steerAxis = 0;
        const uint64_t buttons = reading.buttons;
        view.wheelDeg = reading.axes[steerAxis] * settings.rangeDeg / 2;
        view.canTurn = wheel && info.usable();

        // Where the replay has the wheel: the game counts left as positive, the wheel right.
        double degLeft = 0, late = 0;
        const bool have = telemetry_.steering(t, settings.delayS, degLeft, late) && late < kSilenceS;
        view.haveTarget = have;
        view.targetDeg = (settings.invert ? 1.0 : -1.0) * degLeft * settings.factor;

        if (wantStart && wheel && !engaged && info.usable()) {
            std::string error;
            if (wheel->engage(error)) {
                engaged = true;
                heldAtStart = buttons;
                atStart = reading;
                waitingSince = -1;
                controller.start(t, view.wheelDeg);
                view.lastStop = EngineView::Stop::None;
                view.lastFault = Fault::None;
                view.detail.clear();
            } else {
                release(EngineView::Stop::Driver, Fault::None, error);
            }
        }

        if (engaged) {
            // Any button on the wheel is an emergency stop. Buttons that were already down
            // at the start (some wheels report a mode switch that way) do not count.
            heldAtStart &= buttons;
            if (buttons & ~heldAtStart) {
                release(EngineView::Stop::Button, Fault::None, "");
            } else {
                // The push of the probe moves exactly one axis. If that is not the one being
                // watched (a wheel that reports its steering somewhere else), watch that one
                // from now on - a wheel must never turn here without the loop seeing it.
                if (controller.phase() == Phase::Probe && std::fabs(reading.axes[steerAxis] - atStart.axes[steerAxis]) < 0.004) {
                    for (int i = 0; i < reading.axisCount; ++i) {
                        if (i != steerAxis && std::fabs(reading.axes[i] - atStart.axes[i]) > 0.02) {
                            steerAxis = i;
                            atStart = reading;
                            view.wheelDeg = reading.axes[i] * settings.rangeDeg / 2;
                            controller.start(t, view.wheelDeg);
                            break;
                        }
                    }
                }
                ControlParams params;
                params.cap = std::clamp(settings.cap, 0.0, 1.0);
                params.rangeDeg = settings.rangeDeg;
                params.stiffness = settings.stiffness;
                ControlInput in;
                in.t = t;
                in.wheelDeg = view.wheelDeg;
                in.targetValid = have;
                in.targetDeg = view.targetDeg;
                const double force = controller.step(in, params);
                waitingSince = controller.waiting() ? (waitingSince < 0 ? t : waitingSince) : -1;
                if (controller.phase() == Phase::Fault) {
                    if (controller.fault() == Fault::NoResponse) steerAxis = 0;  // a pedal pressed during the probe may have misled it
                    release(EngineView::Stop::Fault, controller.fault(), "");
                } else if (waitingSince >= 0 && t - waitingSince > kGiveUpS) {
                    release(EngineView::Stop::NoData, Fault::None, "");
                } else if (!wheel->setForce(force)) {
                    release(EngineView::Stop::Driver, Fault::None, SDL_GetError());
                } else {
                    view.force = force;
                }
            }
        }

        view.gain = controller.adapt();
        if (!wheel) {
            view.state = EngineView::State::NoWheel;
        } else if (!engaged) {
            view.state = EngineView::State::Ready;
        } else if (controller.phase() == Phase::Probe) {
            view.state = EngineView::State::Probing;
        } else {
            view.state = controller.waiting() ? EngineView::State::Waiting : EngineView::State::Following;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            view_ = view;
        }

        // Keep the beat without piling up lateness after a stall.
        next += 1.0 / (engaged && wheel ? wheel->rateHz() : kIdleHz);
        const double now = monotonicSeconds();
        if (next < now) next = now;
        else SDL_DelayPrecise(Uint64((next - now) * 1e9));
    }

    release(EngineView::Stop::None, Fault::None, "");
}
