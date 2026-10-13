#include "settings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::string settingsPath() {
    char* folder = SDL_GetPrefPath("qshi", "Geisterhand");
    if (!folder) return "";
    std::string path = std::string(folder) + "settings.txt";
    SDL_free(folder);
    return path;
}

}  // namespace

Settings loadSettings() {
    Settings s;
    const std::string path = settingsPath();
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r");
    if (!f) return s;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string key = line, value = eq + 1;
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
        if (key == "address") s.address = value;
        else if (key == "wheel") s.wheelKey = value;
        else if (key == "force") s.forcePercent = std::atoi(value.c_str());
        else if (key == "range") s.rangeDeg = std::atoi(value.c_str());
        else if (key == "invert") s.invert = value == "1";
        else if (key == "factor") s.factor = float(std::atof(value.c_str()));
        else if (key == "buffer") s.bufferMs = std::atoi(value.c_str());
        else if (key == "stiffness") s.stiffness = float(std::atof(value.c_str()));
        else if (key == "language") s.language = value;
    }
    std::fclose(f);
    // A file edited by hand must not be able to ask for more than the sliders allow.
    s.forcePercent = std::clamp(s.forcePercent, 5, 100);
    s.rangeDeg = std::clamp(s.rangeDeg, 90, 2700);
    s.factor = std::clamp(s.factor, 0.25f, 3.0f);
    s.bufferMs = std::clamp(s.bufferMs, 0, 500);
    s.stiffness = std::clamp(s.stiffness, 0.3f, 2.0f);
    return s;
}

void saveSettings(const Settings& s) {
    const std::string path = settingsPath();
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "address=%s\nwheel=%s\nforce=%d\nrange=%d\ninvert=%d\nfactor=%.2f\nbuffer=%d\nstiffness=%.2f\nlanguage=%s\n",
                 s.address.c_str(), s.wheelKey.c_str(), s.forcePercent, s.rangeDeg, s.invert ? 1 : 0, double(s.factor), s.bufferMs,
                 double(s.stiffness), s.language.c_str());
    std::fclose(f);
}
