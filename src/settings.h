// What the program remembers between runs: a small text file in the user's settings folder.
#pragma once
#include <string>

struct Settings {
    std::string address;      // PlayStation; empty = search
    std::string wheelKey;     // last wheel chosen
    int forcePercent = 20;
    int rangeDeg = 900;
    bool invert = false;
    float factor = 1.0f;
    int bufferMs = 80;
    float stiffness = 1.0f;
    std::string language;     // "de" / "en"; empty = as the system

    bool operator==(const Settings& o) const {
        return address == o.address && wheelKey == o.wheelKey && forcePercent == o.forcePercent && rangeDeg == o.rangeDeg &&
               invert == o.invert && factor == o.factor && bufferMs == o.bufferMs && stiffness == o.stiffness && language == o.language;
    }
};

Settings loadSettings();
void saveSettings(const Settings& settings);
