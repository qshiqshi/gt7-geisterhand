#include "wheel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "clock.h"
#include "sim_plant.h"

namespace {

constexpr uint16_t kLogitech = 0x046d;

// RS50 (c276, c272 in its compatibility mode) and G PRO Racing Wheel (c272 Xbox/PC, c268 PS/PC).
bool isLogiDirectDrive(uint16_t vendor, uint16_t product) {
    return vendor == kLogitech && (product == 0xc276 || product == 0xc272 || product == 0xc268);
}

// These wheels show up as three USB interfaces: the joystick, the settings channel, and a
// third one (vendor usage page 0xFFFD) that takes the torque.
std::string findLogiForcePath(uint16_t product) {
    std::string path;
    SDL_hid_device_info* list = SDL_hid_enumerate(kLogitech, product);
    for (SDL_hid_device_info* d = list; d; d = d->next) {
        if (d->usage_page == 0xFFFD || (d->usage_page == 0 && d->interface_number == 2)) {
            path = d->path;
            break;
        }
    }
    SDL_hid_free_enumeration(list);
    return path;
}

// Whether the system offers force feedback for a joystick. Asked once per plug-in: the id
// changes with every plug-in, so the answer stays right.
bool offersForceFeedback(SDL_JoystickID id) {
    static std::map<SDL_JoystickID, bool> known;
    const auto it = known.find(id);
    if (it != known.end()) return it->second;
    bool haptic = false;
    if (SDL_Joystick* joystick = SDL_OpenJoystick(id)) {
        haptic = SDL_IsJoystickHaptic(joystick);
        SDL_CloseJoystick(joystick);
    }
    known[id] = haptic;
    return haptic;
}

// ---- a wheel that SDL sees as a joystick: angle and buttons -------------------------------

class JoystickWheel : public Wheel {
public:
    ~JoystickWheel() override {
        if (joystick_) SDL_CloseJoystick(joystick_);
    }

    bool openJoystick(SDL_JoystickID id, std::string& error) {
        joystick_ = SDL_OpenJoystick(id);
        if (!joystick_) {
            error = SDL_GetError();
            return false;
        }
        buttons_ = std::min(64, SDL_GetNumJoystickButtons(joystick_));
        return true;
    }

    bool read(double& axis, uint64_t& buttons) override {
        SDL_UpdateJoysticks();
        if (!SDL_JoystickConnected(joystick_)) return false;
        axis = std::clamp(SDL_GetJoystickAxis(joystick_, 0) / 32767.0, -1.0, 1.0);  // steering is the first axis
        buttons = 0;
        for (int i = 0; i < buttons_; ++i) {
            if (SDL_GetJoystickButton(joystick_, i)) buttons |= uint64_t(1) << i;
        }
        return true;
    }

protected:
    SDL_Joystick* joystick_ = nullptr;
    int buttons_ = 0;
};

// ---- force feedback through the operating system -----------------------------------------

class HapticWheel : public JoystickWheel {
public:
    ~HapticWheel() override { disengage(); }

    bool engage(std::string& error) override {
        haptic_ = SDL_OpenHapticFromJoystick(joystick_);
        if (!haptic_) {
            error = SDL_GetError();
            return false;
        }
        const Uint32 features = SDL_GetHapticFeatures(haptic_);
        if (!(features & SDL_HAPTIC_CONSTANT)) {
            error = "no constant force";
            disengage();
            return false;
        }
        // The wheel's own centring spring would pull against the replay.
        if (features & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(haptic_, 0);
        if (features & SDL_HAPTIC_GAIN) SDL_SetHapticGain(haptic_, 100);

        SDL_zero(effect_);
        effect_.type = SDL_HAPTIC_CONSTANT;
        effect_.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect_.constant.length = SDL_HAPTIC_INFINITY;
        effect_.constant.level = 0;
        effectId_ = SDL_CreateHapticEffect(haptic_, &effect_);
        if (effectId_ < 0) {  // an older driver: name the direction the long way
            effect_.constant.direction.type = SDL_HAPTIC_CARTESIAN;
            effect_.constant.direction.dir[0] = 1;
            effectId_ = SDL_CreateHapticEffect(haptic_, &effect_);
        }
        if (effectId_ < 0 || !SDL_RunHapticEffect(haptic_, effectId_, 1)) {
            error = SDL_GetError();
            disengage();
            return false;
        }
        level_ = 0;
        errors_ = 0;
        return true;
    }

    bool setForce(double force) override {
        if (!haptic_ || effectId_ < 0) return false;
        const Sint16 level = Sint16(std::lround(std::clamp(force, -1.0, 1.0) * 32767.0));
        if (level == level_) return true;
        effect_.constant.level = level;
        if (SDL_UpdateHapticEffect(haptic_, effectId_, &effect_)) {
            level_ = level;
            errors_ = 0;
            return true;
        }
        return ++errors_ < 25;  // a single refused update is no reason to give up
    }

    void disengage() override {
        if (!haptic_) return;
        if (effectId_ >= 0) {
            effect_.constant.level = 0;
            SDL_UpdateHapticEffect(haptic_, effectId_, &effect_);
            SDL_StopHapticEffect(haptic_, effectId_);
            SDL_DestroyHapticEffect(haptic_, effectId_);
            effectId_ = -1;
        }
        SDL_CloseHaptic(haptic_);
        haptic_ = nullptr;
    }

    // Drivers differ in how fast they take updates; this rate suits the slow ones too.
    double rateHz() const override { return 125; }

private:
    SDL_Haptic* haptic_ = nullptr;
    SDL_HapticEffect effect_{};
    SDL_HapticEffectID effectId_ = -1;
    Sint16 level_ = 0;
    int errors_ = 0;
};

// ---- Logitech RS50 / G PRO without a driver -----------------------------------------------
//
// Protocol as documented by the logitech-trueforce-linux-driver project (mescon): a 64-byte
// output report on the third interface. Bytes 6-7 and again 8-9 carry the torque target,
// 0x0000 = full left, 0x8000 = none, 0xFFFF = full right; byte 5 counts up. The wheel wants
// it as a stream - it lets go by itself when the stream stops.

class LogiDirectWheel : public JoystickWheel {
public:
    explicit LogiDirectWheel(std::string path) : path_(std::move(path)) {}
    ~LogiDirectWheel() override { disengage(); }

    bool engage(std::string& error) override {
        device_ = SDL_hid_open_path(path_.c_str());
        if (!device_) {
            error = SDL_GetError();
            return false;
        }
        return true;
    }

    bool setForce(double force) override {
        if (!device_) return false;
        const long value = std::lround(32768.0 + std::clamp(force, -1.0, 1.0) * 32767.0);
        const Uint16 torque = Uint16(std::clamp(value, 0L, 65535L));
        unsigned char report[64] = {0};
        report[0] = 0x01;  // report id
        report[4] = 0x01;  // stream packet
        report[5] = ++sequence_;
        report[6] = report[8] = Uint8(torque & 0xFF);
        report[7] = report[9] = Uint8(torque >> 8);
        if (SDL_hid_write(device_, report, sizeof report) == int(sizeof report)) {
            errors_ = 0;
            return true;
        }
        return ++errors_ < 25;
    }

    void disengage() override {
        if (!device_) return;
        for (int i = 0; i < 8; ++i) {  // say "no torque" more than once, then fall silent
            setForce(0);
            SDL_Delay(2);
        }
        SDL_hid_close(device_);
        device_ = nullptr;
    }

    double rateHz() const override { return 500; }

private:
    std::string path_;
    SDL_hid_device* device_ = nullptr;
    Uint8 sequence_ = 0;
    int errors_ = 0;
};

// ---- no hardware --------------------------------------------------------------------------

class DemoWheel : public Wheel {
public:
    DemoWheel() {
        plant_.inertia = 0.032;
        plant_.peakNm = 8.0;
        plant_.viscous = 0.012;
        plant_.coulomb = 0.05;
        plant_.stiction = 0.07;
    }

    bool read(double& axis, uint64_t& buttons) override {
        const double t = monotonicSeconds();
        if (last_ >= 0) plant_.advance(std::min(0.1, t - last_));
        last_ = t;
        axis = plant_.readDeg() / (plant_.rangeDeg / 2);
        buttons = 0;
        return true;
    }

    bool engage(std::string&) override { return true; }

    bool setForce(double force) override {
        plant_.command(force);
        return true;
    }

    void disengage() override { plant_.command(0); }
    double rateHz() const override { return 250; }
    void setRange(double degrees) override { plant_.rangeDeg = degrees; }

private:
    SimPlant plant_;
    double last_ = -1;
};

}  // namespace

std::vector<WheelInfo> listWheels() {
    std::vector<WheelInfo> result;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int i = 0; ids && i < count; ++i) {
        const SDL_JoystickID id = ids[i];
        if (SDL_IsGamepad(id)) continue;
        const char* name = SDL_GetJoystickNameForID(id);
        const Uint16 vendor = SDL_GetJoystickVendorForID(id);
        const Uint16 product = SDL_GetJoystickProductForID(id);

        WheelInfo wheel;
        wheel.name = name && *name ? name : "Wheel";
        wheel.joystick = id;
        char key[16];
        std::snprintf(key, sizeof key, "%04x:%04x", vendor, product);
        wheel.key = key;

        if (offersForceFeedback(id)) {
            wheel.kind = WheelInfo::Kind::Haptic;
        } else if (isLogiDirectDrive(vendor, product)) {
            wheel.hidPath = findLogiForcePath(product);
            wheel.kind = wheel.hidPath.empty() ? WheelInfo::Kind::NoForce : WheelInfo::Kind::LogiDirect;
        } else if (SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL) {
            wheel.kind = WheelInfo::Kind::NoForce;  // a wheel, but nothing here can turn it
        } else {
            continue;  // pedals, shifters, flight sticks
        }
        result.push_back(wheel);
    }
    SDL_free(ids);

    WheelInfo demo;
    demo.kind = WheelInfo::Kind::Demo;
    demo.key = "demo";
    demo.name = "Demo";
    result.push_back(demo);
    return result;
}

std::unique_ptr<Wheel> openWheel(const WheelInfo& info, std::string& error) {
    switch (info.kind) {
    case WheelInfo::Kind::Demo:
        return std::make_unique<DemoWheel>();
    case WheelInfo::Kind::Haptic: {
        auto wheel = std::make_unique<HapticWheel>();
        if (!wheel->openJoystick(info.joystick, error)) return nullptr;
        return wheel;
    }
    case WheelInfo::Kind::LogiDirect: {
        auto wheel = std::make_unique<LogiDirectWheel>(info.hidPath);
        if (!wheel->openJoystick(info.joystick, error)) return nullptr;
        return wheel;
    }
    case WheelInfo::Kind::NoForce: {
        // Readable but not movable: still open it, so that the angle shows.
        class ReadOnlyWheel : public JoystickWheel {
        public:
            bool engage(std::string& e) override {
                e = "no force feedback";
                return false;
            }
            bool setForce(double) override { return false; }
            void disengage() override {}
            double rateHz() const override { return 60; }
        };
        auto wheel = std::make_unique<ReadOnlyWheel>();
        if (!wheel->openJoystick(info.joystick, error)) return nullptr;
        return wheel;
    }
    }
    return nullptr;
}
