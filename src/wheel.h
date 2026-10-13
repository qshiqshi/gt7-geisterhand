// Steering wheels: finding them, reading their angle, turning them.
//
// Three ways to turn a wheel:
//  - Haptic: the force feedback interface of the operating system (DirectInput on Windows,
//    ForceFeedback on macOS, SDL's own driver for older Logitech wheels). Works with every
//    wheel whose maker supplies a driver - the way racing games do it.
//  - LogiDirect: the Logitech RS50 and G PRO take their torque as a stream of small USB
//    packets. That needs no driver, which is what makes these wheels usable on a Mac.
//  - Demo: no wheel at all, a simulated one, to try the program without hardware.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct WheelInfo {
    enum class Kind { Haptic, LogiDirect, Demo, NoForce };
    Kind kind = Kind::NoForce;
    std::string key;         // the same after a restart, for remembering the choice
    std::string name;        // as the system calls it
    uint32_t joystick = 0;   // SDL's id while the device is plugged in
    std::string hidPath;     // LogiDirect: the interface that takes the torque
    bool usable() const { return kind != Kind::NoForce; }
};

// What is plugged in right now. Call from the main thread.
std::vector<WheelInfo> listWheels();

class Wheel {
public:
    virtual ~Wheel() = default;

    // Angle as -1..1 of the wheel's range (right positive) and the buttons held down.
    // False when the device is gone.
    virtual bool read(double& axis, uint64_t& buttons) = 0;

    // Take hold of the motor. Nothing moves before this, nothing after disengage().
    virtual bool engage(std::string& error) = 0;
    virtual bool setForce(double force) = 0;   // -1..1
    virtual void disengage() = 0;

    virtual double rateHz() const = 0;         // how often the loop should run for this wheel
    virtual void setRange(double /*degrees*/) {}
};

// Opens the device for reading. Call from the thread that will use it.
std::unique_ptr<Wheel> openWheel(const WheelInfo& info, std::string& error);
