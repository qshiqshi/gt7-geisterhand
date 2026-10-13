// Makes a wheel follow an angle with nothing but a force: the position loop and everything
// that switches it off again. No device code in here, so it runs against a simulated wheel
// in the self-test exactly as it runs against a real one.
//
// Angles are in degrees, positive = turned right. Forces are -1..1 of what the wheel can do.
#pragma once
#include <deque>

struct ControlParams {
    double cap = 0.20;           // highest force the loop may ask for
    double rangeDeg = 900.0;     // lock to lock, as set on the wheel
    double stiffness = 1.0;      // user trim on the loop gain
    double kp = 0.021;           // force per degree of error
    double tauD = 0.09;          // seconds: how strongly a speed difference is braked
    double tauI = 0.5;           // seconds: how fast leftover error (friction) is worked off
    double maxRateDegS = 1200;   // the wheel is never asked to turn faster than this
};

enum class Phase { Idle, Probe, Run, Fault };

enum class Fault {
    None,
    NoResponse,   // the wheel did not move when pushed
    Runaway,      // it speeds up although the loop brakes: wrong direction, or somebody pulls
    TooFast,      // faster than any replay needs
    Blocked,      // full force and it does not move
    Oscillation,  // it shakes and backing off did not help
};

struct ControlInput {
    double t = 0;            // seconds, monotonic
    double wheelDeg = 0;     // measured
    bool targetValid = false;
    double targetDeg = 0;    // wanted
};

class Controller {
public:
    void start(double t, double wheelDeg);
    void stop();

    // One tick. Returns the force for the wheel.
    double step(const ControlInput& in, const ControlParams& p);

    Phase phase() const { return phase_; }
    Fault fault() const { return fault_; }
    double refDeg() const { return ref_; }
    int polarity() const { return polarity_; }   // +1: positive force turns right
    double adapt() const { return adapt_; }      // share of the full loop gain in use for this wheel
    bool waiting() const { return phase_ == Phase::Run && gain_ <= 0; }
    double agreement() const { return agree_; }  // above 0: the wheel accelerates the way the force points

    // A new wheel: forget what was learned about the last one.
    void forgetWheel();

private:
    double stepProbe(const ControlInput& in, const ControlParams& p, double dt);
    double stepRun(const ControlInput& in, const ControlParams& p, double dt);
    void enterRun(const ControlInput& in);
    void fail(Fault f);

    Phase phase_ = Phase::Idle;
    Fault fault_ = Fault::None;
    int polarity_ = 1;
    double adapt_ = 0.4;      // the loop gain is felt out from below, wheel by wheel
    double ceiling_ = 1.0;    // and never raised again to where this wheel started to shake

    bool haveLast_ = false;
    double lastT_ = 0;
    double lastWheel_ = 0;
    double wheelVel_ = 0;

    // probe
    bool atRest_ = false;
    double restT_ = 0;
    double restWaitT_ = 0;
    int probeDir_ = 1;
    double probeStageT_ = 0;
    double probeStartDeg_ = 0;

    // run
    double target_ = 0;       // the wanted angle, smoothed
    double ref_ = 0;
    double refVel_ = 0;
    double integ_ = 0;
    double gain_ = 0;

    // watchdogs
    double tooFastT_ = 0;
    bool opposing_ = false;
    double opposingT_ = 0;
    double opposingSpeed0_ = 0;
    double blockedT_ = 0;
    double prevVel_ = 0;
    double lastShare_ = 0;
    double agree_ = 0;
    double disagreeT_ = 0;
    int velSign_ = 0;
    std::deque<double> flips_;
    int refSign_ = 0;
    std::deque<double> refFlips_;
};
