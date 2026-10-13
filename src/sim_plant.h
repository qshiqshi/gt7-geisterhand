// A steering wheel as physics: a rim with inertia and friction, a motor that answers a little
// late, end stops, and a 16-bit angle sensor. The demo entry in the device list and the
// self-test both drive this instead of real hardware.
#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

constexpr double kSimPi = 3.14159265358979323846;

struct SimPlant {
    // what kind of wheel
    double inertia = 0.030;      // kg m2, rim plus rotor
    double peakNm = 8.0;         // torque at force 1
    double viscous = 0.010;      // Nm per rad/s
    double coulomb = 0.06;       // Nm while turning
    double stiction = 0.08;      // Nm to break loose
    double rangeDeg = 900.0;     // lock to lock
    double latencyS = 0.004;     // command to torque
    int polarity = 1;            // -1: this wheel's driver turns left on a positive force
    bool gatedSensor = false;    // like SDL: no reading changes until the first larger move

    // state
    double angle = 0;            // rad, right positive
    double velocity = 0;         // rad/s
    double clock = 0;
    std::deque<std::pair<double, double>> pending;  // (time it takes effect, force)
    double activeForce = 0;
    bool gateOpen = false;
    double gateAngleDeg = 0;

    void command(double force) { pending.emplace_back(clock + latencyS, std::clamp(force, -1.0, 1.0)); }

    void advance(double dt) {
        const double h = 0.00025;
        const double stop = rangeDeg / 2 * kSimPi / 180.0;
        for (double left = dt; left > 1e-9; left -= h) {
            const double step = std::min(h, left);
            clock += step;
            while (!pending.empty() && pending.front().first <= clock) {
                activeForce = pending.front().second;
                pending.pop_front();
            }
            double torque = polarity * activeForce * peakNm;
            if (angle > stop) torque -= (angle - stop) * 400.0 + velocity * 2.0;   // the base's own end stop
            if (angle < -stop) torque -= (angle + stop) * 400.0 + velocity * 2.0;
            if (std::fabs(velocity) < 1e-4 && std::fabs(torque) <= stiction) {
                velocity = 0;                                                      // held by friction
                continue;
            }
            const double direction = std::fabs(velocity) > 1e-4 ? (velocity > 0 ? 1.0 : -1.0) : (torque > 0 ? 1.0 : -1.0);
            const double net = torque - direction * coulomb - viscous * velocity;
            const double before = velocity;
            velocity += net / inertia * step;
            if (before * velocity < 0 && std::fabs(torque) <= stiction) velocity = 0;  // came to rest
            angle += velocity * step;
        }
    }

    // What a program reads from the device: 16 bits across the range.
    double readDeg() {
        const double half = rangeDeg / 2;
        double deg = std::clamp(angle * 180.0 / kSimPi, -half, half);
        deg = std::round(deg / half * 32767.0) / 32767.0 * half;
        if (gatedSensor && !gateOpen) {
            if (std::fabs(deg - gateAngleDeg) <= half / 80.0) return gateAngleDeg;
            gateOpen = true;
        }
        return deg;
    }

    void placeAt(double deg) {
        angle = deg * kSimPi / 180.0;
        velocity = 0;
        gateAngleDeg = deg;
        gateOpen = false;
    }
};
