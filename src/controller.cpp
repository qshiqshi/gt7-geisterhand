#include "controller.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kTauVel = 0.012;       // smoothing of the measured wheel speed
constexpr double kTauRefVel = 0.020;    // smoothing of the reference speed

// Probe: a slowly rising push until the wheel answers. It tells which way a positive force
// turns this wheel, and it proves that force feedback works at all before the loop closes.
constexpr double kProbeMoveDeg = 1.0;
constexpr double kProbeMaxForce = 0.30;
constexpr double kProbeRampS = 1.2;
constexpr double kProbeHoldS = 0.4;

constexpr double kFadeInS = 0.6;        // force comes up this slowly at the start and after a gap
constexpr double kFadeOutS = 0.3;       // and goes away this quickly when the data stops
constexpr double kStartRateDegS = 240;  // approach speed while fading in
constexpr double kEndMarginDeg = 10;    // stay this far away from the wheel's own end stops

constexpr double kIntegMax = 0.25;
constexpr double kIntegDeadDeg = 0.5;

constexpr double kTooFastDegS = 2200;
constexpr double kTooFastS = 0.03;
constexpr double kOpposingForce = 0.7;  // share of the cap that counts as "braking hard"
constexpr double kOpposingS = 0.15;
constexpr double kOpposingGainDegS = 150;
constexpr double kAgreeAccelDegS2 = 1500;  // an acceleration that counts as a clear answer
constexpr double kAgreeTauS = 0.08;
constexpr double kDisagree = 0.10;
constexpr double kDisagreeS = 0.10;
constexpr double kBlockedS = 1.5;
constexpr double kBlockedErrDeg = 10;
constexpr double kBlockedVelDegS = 10;
constexpr double kFlipVelDegS = 30;     // a reversal counts above this speed
constexpr double kFlipWindowS = 1.5;
constexpr int kFlipLimit = 6;           // reversals per window: two shakes a second, kept up
constexpr int kRefFlipLimit = 2;       // the replay itself shakes (kerbs): then do not judge
constexpr double kAdaptStart = 0.4;    // loop gain a new wheel starts with
constexpr double kAdaptRiseS = 6.0;    // it grows by the factor e in this time
constexpr double kAdaptBackoff = 0.7;
constexpr double kAdaptFloor = 0.1;
constexpr double kTauTarget = 0.025;   // takes kerb rattle out of the wanted angle, keeps the steering

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

void Controller::start(double t, double wheelDeg) {
    phase_ = Phase::Probe;
    fault_ = Fault::None;
    probeDir_ = 1;
    probeStageT_ = t;
    probeStartDeg_ = wheelDeg;
    haveLast_ = false;
    wheelVel_ = 0;
}

void Controller::stop() {
    phase_ = Phase::Idle;
    fault_ = Fault::None;
}

void Controller::forgetWheel() {
    adapt_ = kAdaptStart;
    ceiling_ = 1.0;
}

void Controller::fail(Fault f) {
    phase_ = Phase::Fault;
    fault_ = f;
}

double Controller::step(const ControlInput& in, const ControlParams& p) {
    double dt = 0;
    if (haveLast_) {
        dt = clamp(in.t - lastT_, 0.0, 0.1);  // a stall must not become one huge step
        if (dt > 1e-5) {
            const double raw = (in.wheelDeg - lastWheel_) / dt;
            wheelVel_ += (raw - wheelVel_) * dt / (kTauVel + dt);
        }
    }
    haveLast_ = true;
    lastT_ = in.t;
    lastWheel_ = in.wheelDeg;

    switch (phase_) {
    case Phase::Probe:
        return stepProbe(in, p);
    case Phase::Run:
        return stepRun(in, p, dt);
    default:
        return 0;
    }
}

double Controller::stepProbe(const ControlInput& in, const ControlParams& p) {
    const double moved = in.wheelDeg - probeStartDeg_;
    if (std::fabs(moved) >= kProbeMoveDeg) {
        polarity_ = ((moved > 0) == (probeDir_ > 0)) ? 1 : -1;
        enterRun(in);
        return 0;
    }
    const double elapsed = in.t - probeStageT_;
    if (elapsed > kProbeRampS + kProbeHoldS) {
        if (probeDir_ > 0) {  // perhaps it rests against an end stop: try the other way
            probeDir_ = -1;
            probeStageT_ = in.t;
            probeStartDeg_ = in.wheelDeg;
            return 0;
        }
        fail(Fault::NoResponse);
        return 0;
    }
    const double peak = std::min(p.cap, kProbeMaxForce);
    return probeDir_ * peak * std::min(1.0, elapsed / kProbeRampS);
}

void Controller::enterRun(const ControlInput& in) {
    phase_ = Phase::Run;
    target_ = in.wheelDeg;
    ref_ = in.wheelDeg;
    refVel_ = 0;
    integ_ = 0;
    gain_ = 0;
    tooFastT_ = 0;
    opposing_ = false;
    blockedT_ = 0;
    prevVel_ = wheelVel_;
    lastShare_ = 0;
    agree_ = 0;
    disagreeT_ = 0;
    velSign_ = 0;
    flips_.clear();
    refSign_ = 0;
    refFlips_.clear();
}

double Controller::stepRun(const ControlInput& in, const ControlParams& p, double dt) {
    // Fade in while there is something to follow, fade out when the data runs dry. With the
    // force gone the wheel is free, and the next packet starts from wherever it then is.
    gain_ = clamp(gain_ + (in.targetValid ? dt / kFadeInS : -dt / kFadeOutS), 0.0, 1.0);
    if (!in.targetValid && gain_ <= 0) {
        target_ = in.wheelDeg;
        ref_ = in.wheelDeg;
        refVel_ = 0;
        integ_ = 0;
        blockedT_ = 0;
        opposing_ = false;
        return 0;
    }

    const double refBefore = ref_;
    if (in.targetValid) {
        const double limit = std::max(10.0, p.rangeDeg / 2 - kEndMarginDeg);
        target_ += (clamp(in.targetDeg, -limit, limit) - target_) * dt / (kTauTarget + dt);
        const double rate = kStartRateDegS + (p.maxRateDegS - kStartRateDegS) * gain_ * gain_;
        ref_ += clamp(target_ - ref_, -rate * dt, rate * dt);
    }
    if (dt > 1e-5) {
        const double raw = (ref_ - refBefore) / dt;
        refVel_ += (raw - refVel_) * dt / (kTauRefVel + dt);
    }

    const double error = ref_ - in.wheelDeg;
    const double kp = p.kp * p.stiffness * adapt_;
    const double pd = kp * (error + p.tauD * (refVel_ - wheelVel_));
    const bool saturated = std::fabs(pd + integ_) >= p.cap;
    if (!saturated && std::fabs(error) > kIntegDeadDeg && p.tauI > 0) {
        integ_ = clamp(integ_ + kp * error * dt / p.tauI, -kIntegMax, kIntegMax);
    }
    const double force = clamp(pd + integ_, -p.cap, p.cap);
    const double share = p.cap > 0 ? force / p.cap : 0;  // -1..1 of what is allowed

    // Faster than any replay needs: something is wrong, let go.
    tooFastT_ = std::fabs(wheelVel_) > kTooFastDegS ? tooFastT_ + dt : 0;
    if (tooFastT_ > kTooFastS) {
        fail(Fault::TooFast);
        return 0;
    }

    // Does the wheel do what the force says? A push one way and an acceleration the other,
    // for longer than a moment, means the force works the wrong way round - long before the
    // wheel has gained much speed.
    if (dt > 1e-5) {
        const double accel = clamp((wheelVel_ - prevVel_) / dt / kAgreeAccelDegS2, -1.0, 1.0);
        const double sample = std::fabs(lastShare_) > 0.2 ? lastShare_ * accel : 0.0;
        agree_ += (sample - agree_) * dt / (kAgreeTauS + dt);
    }
    prevVel_ = wheelVel_;
    lastShare_ = share * gain_;
    disagreeT_ = agree_ < -kDisagree ? disagreeT_ + dt : 0;
    if (disagreeT_ > kDisagreeS) {
        fail(Fault::Runaway);
        return 0;
    }

    // Braking hard and the wheel still gains speed: the force works the wrong way round, or
    // somebody is turning the wheel against it. Either way the loop cannot win.
    const bool braking = std::fabs(share) > kOpposingForce && share * wheelVel_ < 0 && std::fabs(wheelVel_) > 60;
    if (braking) {
        if (!opposing_) {
            opposing_ = true;
            opposingT_ = 0;
            opposingSpeed0_ = std::fabs(wheelVel_);
        }
        opposingT_ += dt;
        if (opposingT_ > kOpposingS && std::fabs(wheelVel_) > opposingSpeed0_ + kOpposingGainDegS) {
            fail(Fault::Runaway);
            return 0;
        }
    } else {
        opposing_ = false;
    }

    // Full force, far from the target, and nothing moves.
    const bool stuck = gain_ >= 1.0 && std::fabs(share) >= 0.95 && std::fabs(error) > kBlockedErrDeg &&
                       std::fabs(wheelVel_) < kBlockedVelDegS;
    blockedT_ = stuck ? blockedT_ + dt : 0;
    if (blockedT_ > kBlockedS) {
        fail(Fault::Blocked);
        return 0;
    }

    // Shaking: count reversals of the wheel. A loop that is too stiff for this wheel shows
    // here first, as a tremble of a degree or so. The gain is therefore felt out from below:
    // it rises slowly, steps back as soon as the wheel trembles, and stays under that level.
    // When the replay itself shakes (kerbs), reversals say nothing and are not judged.
    const auto countFlips = [&](double vel, int& sign, std::deque<double>& flips) {
        const int now = vel > kFlipVelDegS ? 1 : vel < -kFlipVelDegS ? -1 : 0;
        if (now != 0 && now != sign) {
            if (sign != 0) flips.push_back(in.t);
            sign = now;
        }
        while (!flips.empty() && in.t - flips.front() > kFlipWindowS) flips.pop_front();
    };
    countFlips(wheelVel_, velSign_, flips_);
    countFlips(refVel_, refSign_, refFlips_);
    if (int(refFlips_.size()) > kRefFlipLimit) {
        flips_.clear();
    } else if (int(flips_.size()) >= kFlipLimit) {
        flips_.clear();
        integ_ = 0;
        adapt_ *= kAdaptBackoff;
        ceiling_ = adapt_;
        if (adapt_ < kAdaptFloor) {
            fail(Fault::Oscillation);
            return 0;
        }
    } else if (gain_ >= 1.0) {
        adapt_ = std::min(ceiling_, adapt_ * (1.0 + dt / kAdaptRiseS));
    }

    return polarity_ * force * gain_;
}
