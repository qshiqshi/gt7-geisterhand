// Self-test without any hardware: `Geisterhand --selftest [recording.gt7r]`.
//
// Checks the decryption against packets made by another implementation, the playout buffer
// against bursty arrival, and the control loop against simulated wheels of very different
// strength - including the cases in which it has to switch itself off.
#include "selftest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "controller.h"
#include "demo_signal.h"
#include "gt7_packet.h"
#include "playout.h"
#include "sim_plant.h"
#include "test_packets.inc"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Deterministic noise, so that every run is the same.
struct Lcg {
    uint32_t state = 12345;
    double next() {  // 0..1
        state = state * 1664525u + 1013904223u;
        return (state >> 8) / double(1 << 24);
    }
};

using Signal = std::function<double(double)>;

// ---- packets ---------------------------------------------------------------------------

void testPackets() {
    std::printf("Packets\n");
    Gt7Frame f;
    check(gt7Decode(kTestPacketB, sizeof kTestPacketB, f), "decrypts a packet B made by pycryptodome");
    check(f.packetId == 123456 && f.flags == 0x0199, "packet id and flags");
    check(std::fabs(f.speedMps - 41.5f) < 1e-6, "speed");
    check(f.hasSteering && std::fabs(f.steeringRad + 0.75f) < 1e-6, "steering wheel angle");
    check(f.onTrack() && !f.paused() && !f.loading(), "flag bits");

    Gt7Frame a;
    check(gt7Decode(kTestPacketA, sizeof kTestPacketA, a), "decrypts a packet A");
    check(a.packetId == 777 && !a.hasSteering && a.paused(), "packet A has no steering angle");

    unsigned char broken[sizeof kTestPacketB];
    std::memcpy(broken, kTestPacketB, sizeof broken);
    broken[0x41] ^= 0x40;  // a wrong seed: nothing decrypts
    check(!gt7Decode(broken, sizeof broken, f), "rejects a damaged packet");
    check(!gt7Decode(kTestPacketB, 120, f), "rejects a short datagram");
}

// ---- playout ---------------------------------------------------------------------------

void testPlayout() {
    std::printf("Playout\n");
    const double frame = Playout::kFrame;
    const auto angleOf = [](int id) { return 30.0 * std::sin(id * 0.05); };

    {   // even arrival: the output is the input, shifted by the delay
        Playout p;
        double worst = 0;
        for (int id = 1000; id < 1600; ++id) {
            const double arrival = 5.0 + (id - 1000) * frame;
            p.push(id, angleOf(id), arrival);
            double angle, late;
            if (id > 1010 && p.sample(arrival, 0.08, angle, late)) {
                const double due = id - 0.08 / frame;
                worst = std::max(worst, std::fabs(angle - 30.0 * std::sin(due * 0.05)));
            }
        }
        check(worst < 0.05, "even arrival is reproduced");
    }
    {   // bursts: packets held back for up to 60 ms and then delivered together
        Playout p;
        Lcg noise;
        double held = 0, worst = 0, last = 0;
        bool have = false;
        int next = 2000;
        for (double now = 10.0; now < 22.0; now += 0.002) {
            if (held <= now) {
                while (10.0 + (next - 2000) * frame <= now) {
                    p.push(next, angleOf(next), now);
                    ++next;
                }
                held = now + noise.next() * 0.06;
            }
            double angle, late;
            if (now > 11.0 && p.sample(now, 0.10, angle, late)) {
                if (have) worst = std::max(worst, std::fabs(angle - last));
                last = angle;
                have = true;
            }
        }
        // 30 deg * 0.05 per frame = 1.5 deg per frame at most, i.e. 0.18 deg per 2 ms
        check(worst < 0.25, "bursty arrival comes out even");
    }
    {   // silence and a restart of the frame counter
        Playout p;
        for (int id = 50; id < 110; ++id) p.push(id, 12.0, 1.0 + (id - 50) * frame);
        double angle = 0, late = 0;
        p.sample(4.0, 0.08, angle, late);
        check(late > 1.5 && std::fabs(angle - 12.0) < 1e-9, "silence is reported, the last angle is held");
        p.push(108, 99.0, 4.05);  // one stray older packet changes nothing
        p.sample(4.06, 0.08, angle, late);
        check(std::fabs(angle - 12.0) < 1e-9, "a stray older packet is ignored");
        for (int id = 60; id < 75; ++id) p.push(id, -40.0, 4.1 + (id - 60) * frame);  // the game counts anew
        p.sample(4.6, 0.08, angle, late);
        check(std::fabs(angle + 40.0) < 1e-9, "a restarted counter starts a new session");
    }
}

// ---- control loop ----------------------------------------------------------------------

struct RunResult {
    Fault fault = Fault::None;
    int polarity = 0;
    double probeTravel = 0;   // how far the probe moved the wheel
    double rms = 0, worst = 0;
    double chatter = 0;       // mean change of force per second, as a share of the cap
    double adapt = 1;
    double faultAt = -1;
    double travelAfterFault = 0;
    double forceInGap = 0;    // highest force while the target was invalid and faded out
    double agreeMin = 0;      // lowest value of the force/acceleration check
    double shake = 0;         // rms of the wheel's acceleration, deg/s2: how rough it runs
    double speedAtFault = 0;
};

struct Scenario {
    std::string name;
    SimPlant plant;
    double hz = 125;
    ControlParams params;
    double seconds = 40;
    double gapFrom = -1, gapTo = -1;       // target invalid in between
    double flipPolarityAt = -1;            // the driver turns the force round mid-run
    bool flipAtRunStart = false;           // the probe was fooled: the loop starts the wrong way round
};

RunResult run(Scenario s, const Signal& target) {
    RunResult r;
    Lcg noise;
    Controller c;
    s.plant.gatedSensor = true;
    s.plant.placeAt(25.0);
    double t = 0;
    c.start(t, s.plant.readDeg());

    const double startDeg = 25.0;
    double sum = 0, lastForce = 0, chatter = 0, runSince = -1, faultAngle = 0, lastVel = 0, accSum = 0;
    long accN = 0;
    long n = 0;
    while (t < s.seconds) {
        const double dt = (1.0 / s.hz) * (0.8 + 0.4 * noise.next());  // the scheduler is never exact
        t += dt;
        s.plant.advance(dt);
        if (s.flipPolarityAt > 0 && t >= s.flipPolarityAt) {
            s.plant.polarity = -s.plant.polarity;
            s.flipPolarityAt = -1;
        }
        ControlInput in;
        in.t = t;
        in.wheelDeg = s.plant.readDeg();
        in.targetValid = !(t >= s.gapFrom && t < s.gapTo);
        in.targetDeg = target(t);
        const Phase before = c.phase();
        const double force = c.step(in, s.params);
        s.plant.command(force);

        const double trueDeg = s.plant.angle * 180.0 / kSimPi;
        if (before == Phase::Probe) r.probeTravel = std::max(r.probeTravel, std::fabs(trueDeg - startDeg));
        if (before == Phase::Probe && c.phase() == Phase::Run) {
            runSince = t;
            if (s.flipAtRunStart) s.plant.polarity = -s.plant.polarity;
        }
        if (c.phase() == Phase::Run) r.agreeMin = std::min(r.agreeMin, c.agreement());
        if (c.phase() == Phase::Fault && r.faultAt < 0) {
            r.faultAt = t;
            r.fault = c.fault();
            faultAngle = trueDeg;
            r.speedAtFault = std::fabs(s.plant.velocity) * 180.0 / kSimPi;
        }
        if (r.faultAt >= 0) r.travelAfterFault = std::max(r.travelAfterFault, std::fabs(trueDeg - faultAngle));
        if (c.phase() == Phase::Run && runSince >= 0 && t > runSince + 2.0 && in.targetValid && !(t < s.gapTo + 2.0 && s.gapTo > 0)) {
            const double limit = s.params.rangeDeg / 2 - 10;
            const double want = std::max(-limit, std::min(limit, in.targetDeg));
            const double e = want - trueDeg;
            sum += e * e;
            r.worst = std::max(r.worst, std::fabs(e));
            ++n;
        }
        if (!in.targetValid && t > s.gapFrom + 0.5) r.forceInGap = std::max(r.forceInGap, std::fabs(force));
        const double velDeg = s.plant.velocity * 180.0 / kSimPi;
        if (c.phase() == Phase::Run) {
            const double acc = (velDeg - lastVel) / dt;
            accSum += acc * acc;
            ++accN;
        }
        lastVel = velDeg;
        chatter += std::fabs(force - lastForce);
        lastForce = force;
    }
    r.polarity = c.polarity();
    r.rms = n ? std::sqrt(sum / n) : 0;
    r.chatter = s.params.cap > 0 ? chatter / s.seconds / s.params.cap : 0;
    r.adapt = c.adapt();
    r.shake = accN ? std::sqrt(accSum / accN) : 0;
    return r;
}

SimPlant directDrive(double nm) {
    SimPlant p;
    p.inertia = 0.032;
    p.peakNm = nm;
    p.viscous = 0.012;
    p.coulomb = 0.05;
    p.stiction = 0.07;
    return p;
}

SimPlant gearDriven() {  // strong friction, little torque
    SimPlant p;
    p.inertia = 0.020;
    p.peakNm = 2.1;
    p.viscous = 0.02;
    p.coulomb = 0.22;
    p.stiction = 0.30;
    return p;
}

SimPlant beltDriven() {
    SimPlant p;
    p.inertia = 0.030;
    p.peakNm = 3.9;
    p.viscous = 0.05;
    p.coulomb = 0.13;
    p.stiction = 0.17;
    return p;
}

// The same drive with what a real one adds at its worst: opposite lock thrown in faster than
// a force of a few Nm can follow, and the rattle of kerbs that a wheel user's replay carries.
double harshSteeringDeg(double t) {
    const double phase = std::fmod(t, 5.9) - 2.0;
    const double flick = 70.0 * std::exp(-phase * phase / (2.0 * 0.05 * 0.05)) * (std::fmod(t, 11.8) < 5.9 ? -1.0 : 1.0);
    const double rattle = std::fmod(t, 5.0) < 1.5 ? 1.5 * std::sin(2 * kSimPi * 14.0 * t) : 0.0;
    return demoSteeringDeg(t) + flick + rattle;
}

void testControl(const Signal& target, const char* source, bool harsh) {
    std::printf("Control loop (%s)\n", source);
    std::printf("  %-48s %6s %6s %6s %7s %6s %6s %6s\n", "wheel", "rms", "worst", "probe", "chatter", "gain", "agree", "shake");

    struct Case {
        Scenario s;
        double rmsLimit;   // 0: only has to stay in control
    };
    std::vector<Case> cases;
    const auto add = [&](const char* name, SimPlant plant, double hz, double latency, double cap, double rmsLimit) {
        for (int polarity : {1, -1}) {
            Scenario s;
            s.name = std::string(name) + (polarity > 0 ? "" : ", force inverted");
            s.plant = plant;
            s.plant.latencyS = latency;
            s.plant.polarity = polarity;
            s.hz = hz;
            s.params.cap = cap;
            cases.push_back({s, harsh ? 0 : rmsLimit});
        }
    };
    add("direct drive 8 Nm, direct, 500 Hz", directDrive(8), 500, 0.003, 0.20, 5);
    add("direct drive 8 Nm, driver, 125 Hz", directDrive(8), 125, 0.012, 0.20, 5);
    add("direct drive 5 Nm setting, 125 Hz", directDrive(5), 125, 0.012, 0.20, 6);
    add("direct drive 20 Nm, driver, 125 Hz", directDrive(20), 125, 0.014, 0.20, 5);
    add("direct drive 20 Nm, force 60 %", directDrive(20), 125, 0.014, 0.60, 5);
    add("direct drive 25 Nm, slow driver 60 Hz", directDrive(25), 60, 0.025, 0.20, 8);
    add("belt 3.9 Nm, 125 Hz, force 40 %", beltDriven(), 125, 0.010, 0.40, 8);
    add("gears 2.1 Nm, 125 Hz, force 70 %", gearDriven(), 125, 0.012, 0.70, 10);
    add("gears 2.1 Nm at the default 20 %", gearDriven(), 125, 0.012, 0.20, 0);
    add("belt 3.9 Nm at the default 20 %", beltDriven(), 125, 0.010, 0.20, 0);

    for (auto& c : cases) {
        const RunResult r = run(c.s, target);
        std::printf("  %-48s %6.1f %6.1f %6.1f %7.1f %6.2f %6.2f %6.0f\n", c.s.name.c_str(), r.rms, r.worst, r.probeTravel, r.chatter, r.adapt, r.agreeMin, r.shake);
        bool ok = r.polarity == c.s.plant.polarity && r.probeTravel < 30;
        if (c.rmsLimit > 0) ok = ok && r.fault == Fault::None && r.rms < c.rmsLimit;
        else ok = ok && (r.fault == Fault::None || r.fault == Fault::Blocked) && r.worst < 200;
        if (!ok) {
            std::printf("  FAIL  ^ fault %d, polarity %d, rms limit %.0f\n", int(r.fault), r.polarity, c.rmsLimit);
            ++failures;
        } else if (r.fault != Fault::None) {
            std::printf("        ^ gave up as blocked after %.1f s (too little force for this wheel)\n", r.faultAt);
        }
    }
    if (harsh) return;

    std::printf("Switching off\n");
    {   // a wheel that does not move at all
        Scenario s;
        s.plant = directDrive(8);
        s.plant.stiction = 50;
        s.plant.coulomb = 50;
        s.seconds = 8;
        const RunResult r = run(s, target);
        check(r.fault == Fault::NoResponse && r.faultAt < 4.0, "a wheel that never answers ends the start");
    }
    {   // something holds the wheel after the start
        Scenario s;
        s.plant = directDrive(8);
        s.seconds = 20;
        Controller c;
        SimPlant& plant = s.plant;
        plant.placeAt(0);
        double t = 0;
        c.start(t, plant.readDeg());
        Fault fault = Fault::None;
        double at = -1;
        while (t < s.seconds) {
            const double dt = 1.0 / 125;
            t += dt;
            if (t > 6.0) {  // a hand grips it
                plant.stiction = 50;
                plant.coulomb = 50;
            }
            plant.advance(dt);
            ControlInput in{t, plant.readDeg(), true, 90.0 * std::sin(t)};
            plant.command(c.step(in, s.params));
            if (c.phase() == Phase::Fault && at < 0) {
                at = t;
                fault = c.fault();
            }
        }
        check(fault == Fault::Blocked && at > 6.0 && at < 11.0, "a held wheel is given up after a moment");
    }
    {   // the force suddenly works the other way round
        Scenario s;
        s.plant = directDrive(8);
        s.seconds = 14;
        s.flipPolarityAt = 8.0;
        const RunResult r = run(s, target);
        const bool caught = (r.fault == Fault::Runaway || r.fault == Fault::TooFast) && r.faultAt < 8.8;
        std::printf("        (caught %.2f s after the flip at %.0f deg/s)\n", r.faultAt - 8.0, r.speedAtFault);
        check(caught, "a force that turns the wrong way is caught");
    }
    for (double nm : {8.0, 20.0, 2.1}) {  // the probe was fooled, e.g. by a hand on the rim
        Scenario s;
        s.plant = nm < 3 ? gearDriven() : directDrive(nm);
        s.plant.latencyS = 0.012;
        s.params.cap = nm < 3 ? 0.7 : 0.2;
        s.seconds = 10;
        s.flipAtRunStart = true;
        const RunResult r = run(s, target);
        std::printf("        (%.1f Nm: fault %d after %.2f s at %.0f deg/s)\n", nm, int(r.fault), r.faultAt, r.speedAtFault);
        check(r.fault == Fault::Runaway || r.fault == Fault::TooFast, "a loop that starts the wrong way round is stopped");
    }
    {   // the wheel is still turning when the start is pressed: the probe has to wait
        for (int polarity : {1, -1}) {
            Controller c;
            SimPlant plant = directDrive(8);
            plant.polarity = polarity;
            plant.viscous = 0.05;
            plant.placeAt(0);
            plant.velocity = 300.0 * kSimPi / 180.0 * -polarity;   // against what a positive force would do
            double t = 0;
            c.start(t, plant.readDeg());
            ControlParams params;
            while (t < 12 && c.phase() == Phase::Probe) {
                t += 1.0 / 250;
                plant.advance(1.0 / 250);
                ControlInput in{t, plant.readDeg(), true, 0.0};
                plant.command(c.step(in, params));
            }
            check(c.phase() == Phase::Run && c.polarity() == polarity, "a wheel that still turns at the start is not mistaken for an answer");
        }
    }
    {   // the data stops for a while
        Scenario s;
        s.plant = directDrive(8);
        s.seconds = 20;
        s.gapFrom = 8.0;
        s.gapTo = 11.0;
        const RunResult r = run(s, target);
        check(r.fault == Fault::None && r.forceInGap < 1e-9, "no data: the force goes away, no fault");
        check(r.rms < 8, "and following resumes afterwards");
    }
}

// A recording of the Companion: 8 bytes time + 296 bytes packet per record in .gt7r, and
// 8 bytes time + 72 bytes (from 0x128 on) per record in .gt7x next to it.
bool loadRecording(const char* path, std::vector<double>& degRight) {
    std::string base(path);
    const size_t dot = base.rfind('.');
    if (dot != std::string::npos) base.erase(dot);
    FILE* f = std::fopen((base + ".gt7x").c_str(), "rb");
    if (!f) return false;
    unsigned char rec[80];
    while (std::fread(rec, 1, sizeof rec, f) == sizeof rec) {
        float rad;
        std::memcpy(&rad, rec + 8, sizeof rad);
        degRight.push_back(-double(rad) * 180.0 / kSimPi);  // the game counts left as positive
    }
    std::fclose(f);
    return degRight.size() > 600;
}

}  // namespace

int runSelfTest(const char* recording) {
    failures = 0;
    testPackets();
    testPlayout();
    testControl(demoSteeringDeg, "made-up drive", false);
    testControl(harshSteeringDeg, "made-up drive, harsh: only has to stay in control", true);

    std::vector<double> recorded;
    if (recording && loadRecording(recording, recorded)) {
        const Signal fromFile = [&recorded](double t) {
            const double pos = std::fmod(t * 59.94, double(recorded.size() - 1));
            const size_t i = size_t(pos);
            return recorded[i] + (recorded[i + 1] - recorded[i]) * (pos - i);
        };
        testControl(fromFile, "recording", false);
    } else if (recording) {
        std::printf("Could not read %s\n", recording);
        ++failures;
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
