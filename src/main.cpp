// Geisterhand: one window. Shows where the replay has the steering wheel and where the real
// wheel is, and lets the real one follow.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "clock.h"
#include "engine.h"
#include "fonts.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "selftest.h"
#include "settings.h"
#include "strings.h"
#include "telemetry.h"
#include "wheel.h"

#ifndef GEISTERHAND_VERSION
#define GEISTERHAND_VERSION "0.0.0"
#endif

namespace {

// ---- command line: nothing a user needs, everything a test does ---------------------------

struct Args {
    bool selftest = false;
    std::string recording;   // --selftest [file]
    std::string report;      // --report file: where the self-test writes (a windowed program has no console)
    bool demo = false;       // made-up drive instead of the network
    std::string address;     // --ps5
    int receivePort = 33740;
    int sendPort = 33739;
    std::string wheel;       // --wheel key: choose this device
    bool listWheels = false; // --list-wheels: print what the device list would offer, then quit
    bool autostart = false;  // press "start" as soon as it is possible
    std::string shot;        // --shot file.png: save the window's picture ...
    double shotAfter = 3;    // ... this many seconds after launch
    std::string record;      // --record folder: save the window 20 times a second, from --shot-after on ...
    double recordSeconds = 6;  // ... for this long (pictures for the manual)
    double quitAfter = 0;
    std::string lang;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--selftest") {
            a.selftest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') a.recording = argv[++i];
        } else if (arg == "--report") a.report = value();
        else if (arg == "--demo") a.demo = true;
        else if (arg == "--ps5") a.address = value();
        else if (arg == "--receive-port") a.receivePort = std::atoi(value().c_str());
        else if (arg == "--send-port") a.sendPort = std::atoi(value().c_str());
        else if (arg == "--wheel") a.wheel = value();
        else if (arg == "--list-wheels") a.listWheels = true;
        else if (arg == "--autostart") a.autostart = true;
        else if (arg == "--shot") a.shot = value();
        else if (arg == "--shot-after") a.shotAfter = std::atof(value().c_str());
        else if (arg == "--record") a.record = value();
        else if (arg == "--record-seconds") a.recordSeconds = std::atof(value().c_str());
        else if (arg == "--quit-after") a.quitAfter = std::atof(value().c_str());
        else if (arg == "--lang") a.lang = value();
    }
    return a;
}

// ---- look ----------------------------------------------------------------------------------

constexpr unsigned kBg = 0x0E0F12, kPanel = 0x17191E, kField = 0x20232B, kFieldHot = 0x2A2E38, kLine = 0x2B2F39;
constexpr unsigned kText = 0xECEEF2, kMuted = 0x8A8F9A, kGhost = 0x7CE7F5, kGo = 0x3DDC97, kStop = 0xFF5A5F, kWarn = 0xFFC857;
constexpr float kPi = 3.14159265f;

ImU32 col(unsigned rgb, float alpha = 1.0f) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, int(alpha * 255.0f + 0.5f));
}

ImVec4 vec(unsigned rgb, float alpha = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

void applyTheme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(22, 20);
    s.FramePadding = ImVec2(12, 9);
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 8;
    s.GrabRounding = 6;
    s.PopupRounding = 10;
    s.GrabMinSize = 16;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
    s.FontSizeBase = 15.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = vec(kBg);
    c[ImGuiCol_ChildBg] = vec(kPanel);
    c[ImGuiCol_PopupBg] = vec(kPanel);
    c[ImGuiCol_Border] = vec(kLine);
    c[ImGuiCol_Text] = vec(kText);
    c[ImGuiCol_TextDisabled] = vec(kMuted);
    c[ImGuiCol_FrameBg] = vec(kField);
    c[ImGuiCol_FrameBgHovered] = vec(kFieldHot);
    c[ImGuiCol_FrameBgActive] = vec(kFieldHot);
    c[ImGuiCol_SliderGrab] = vec(kGhost);
    c[ImGuiCol_SliderGrabActive] = vec(kText);
    c[ImGuiCol_CheckMark] = vec(kGhost);
    c[ImGuiCol_Button] = vec(kField);
    c[ImGuiCol_ButtonHovered] = vec(kFieldHot);
    c[ImGuiCol_ButtonActive] = vec(kLine);
    c[ImGuiCol_Header] = vec(kField);
    c[ImGuiCol_HeaderHovered] = vec(kFieldHot);
    c[ImGuiCol_HeaderActive] = vec(kFieldHot);
    c[ImGuiCol_ScrollbarBg] = vec(kPanel);
    c[ImGuiCol_Separator] = vec(kLine);
    c[ImGuiCol_TextSelectedBg] = vec(kGhost, 0.35f);
}

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
};

Fonts loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // the data lives in the program itself
    Fonts f;
    f.regular = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontRegular), int(kFontRegularSize), 15.0f, &config);
    f.semibold = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontSemiBold), int(kFontSemiBoldSize), 15.0f, &config);
    return f;
}

// ---- the dial -------------------------------------------------------------------------------

// A steering wheel seen from the driver's seat, turned by `degrees` (right positive).
void drawWheel(ImDrawList* dl, ImVec2 c, float r, double degrees, ImU32 color, float rim, float spoke, ImU32 mark, bool solid) {
    const float turn = float(degrees) * kPi / 180.0f;
    dl->AddCircle(c, r, color, 96, rim);
    const float hub = r * 0.25f;
    const float spokes[3] = {-kPi / 2, kPi / 2, kPi};  // left, right, down
    for (float at : spokes) {
        const ImVec2 dir(std::sin(turn + at), -std::cos(turn + at));
        dl->AddLine(c + dir * hub, c + dir * (r - rim * 0.5f), color, spoke);
    }
    if (solid) dl->AddCircleFilled(c, hub, color, 48);
    else dl->AddCircle(c, hub, color, 48, spoke * 0.6f);
    const ImVec2 up(std::sin(turn), -std::cos(turn));
    dl->AddLine(c + up * (r - rim), c + up * (r + rim), mark, rim * 0.8f);  // the stripe at twelve o'clock
}

// The replay's wheel: a thin ring just outside the real rim with a dot at twelve o'clock, so
// that it stays visible when the real wheel is exactly where the replay has it.
void drawGhost(ImDrawList* dl, ImVec2 c, float r, float outside, double degrees, ImU32 color, float line, float dot) {
    const float turn = float(degrees) * kPi / 180.0f;
    const float ring = r + outside;
    dl->AddCircle(c, ring, color, 96, line);
    const float spokes[3] = {-kPi / 2, kPi / 2, kPi};
    for (float at : spokes) {
        const ImVec2 dir(std::sin(turn + at), -std::cos(turn + at));
        dl->AddLine(c + dir * (r * 0.25f), c + dir * ring, color, line);
    }
    const ImVec2 up(std::sin(turn), -std::cos(turn));
    dl->AddCircleFilled(c + up * ring, dot, color, 24);
}

std::string degreesText(double value) {
    char text[32];
    const long rounded = std::lround(value);
    std::snprintf(text, sizeof text, "%s%ld°", rounded > 0 ? "+" : rounded < 0 ? "−" : "", std::labs(rounded));
    return text;
}

// ---- small building blocks -------------------------------------------------------------------

struct Ui {
    float scale = 1;
    Fonts fonts;
    Lang lang = Lang::De;
    float px(float v) const { return v * scale; }
    const char* t(Tx id) const { return tr(lang, id); }
};

void label(const Ui& ui, const char* text) {
    ImGui::PushFont(ui.fonts.semibold, 11.5f);
    ImGui::TextColored(vec(kMuted), "%s", text);
    ImGui::PopFont();
}

void statusLine(const Ui& ui, unsigned dot, const char* text, unsigned textColor = kText) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + ui.px(5), at.y + line * 0.5f), ui.px(4), col(dot), 16);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui.px(18));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(textColor));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void mutedWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, vec(kMuted));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool beginCard(const Ui& ui, const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.px(16), ui.px(14)));
    return ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void rowLabel(const Ui& ui, const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(ui.px(128));
    ImGui::SetNextItemWidth(-FLT_MIN);
}

std::string wheelLabel(const Ui& ui, const WheelInfo& w) {
    switch (w.kind) {
    case WheelInfo::Kind::Demo:
        return ui.t(Tx::WheelDemo);
    case WheelInfo::Kind::LogiDirect:
        return w.name + "  ·  " + ui.t(Tx::WheelDirect);
    case WheelInfo::Kind::NoForce:
        return w.name + "  ·  " + ui.t(Tx::WheelNoForce);
    default:
        return w.name;
    }
}

bool sameDevice(const std::optional<WheelInfo>& a, const std::optional<WheelInfo>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->key == b->key && a->joystick == b->joystick && a->kind == b->kind && a->hidPath == b->hidPath;
}

Lang systemLanguage() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    Lang lang = Lang::En;
    if (locales && count > 0 && locales[0]->language && std::strncmp(locales[0]->language, "de", 2) == 0) lang = Lang::De;
    SDL_free(locales);
    return lang;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
    if (args.selftest) {
        if (!args.report.empty() && !std::freopen(args.report.c_str(), "w", stdout)) return 2;
        return runSelfTest(args.recording.empty() ? nullptr : args.recording.c_str());
    }

    // The wheel must keep reporting while another window has the focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef _WIN32
    // On Windows force feedback only exists through DirectInput; keep SDL's other joystick
    // backends from claiming the wheel first.
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#endif
#ifdef __APPLE__
    // macOS claims racing wheels such as the RS50 for its GameController framework, and SDL
    // leaves everything that framework claims to a backend that only passes on gamepads: the
    // wheel would never show up. Gamepads are of no use here anyway.
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
#endif
    SDL_SetAppMetadata("Geisterhand", GEISTERHAND_VERSION, "de.qshi.geisterhand");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Geisterhand", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_hid_init();

    if (args.listWheels) {
        if (!args.report.empty() && !std::freopen(args.report.c_str(), "w", stdout)) return 2;
        for (int i = 0; i < 50; ++i) {  // devices arrive during the first moments
            SDL_PumpEvents();
            SDL_UpdateJoysticks();
            SDL_Delay(20);
        }
        for (const WheelInfo& w : listWheels()) {
            const char* kind = w.kind == WheelInfo::Kind::Haptic       ? "system"
                               : w.kind == WheelInfo::Kind::LogiDirect ? "direct"
                               : w.kind == WheelInfo::Kind::Demo       ? "demo"
                                                                       : "read-only";
            std::printf("%-10s %-10s %s\n", w.key.c_str(), kind, w.name.c_str());
        }
        SDL_Quit();
        return 0;
    }
    // The engine reads the wheel itself, many times a second. Nobody needs each movement as
    // an event; plugging in and out still arrives.
    for (Uint32 type : {SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION, SDL_EVENT_JOYSTICK_HAT_MOTION,
                        SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP}) {
        SDL_SetEventEnabled(type, false);
    }
    SDL_SetGamepadEventsEnabled(false);

    // A run with test options starts from the defaults and leaves the user's settings alone.
    const bool testing = args.demo || !args.address.empty() || !args.wheel.empty() || !args.shot.empty() || !args.record.empty() || args.autostart ||
                         args.quitAfter > 0 || args.receivePort != 33740 || args.sendPort != 33739;
    Settings settings = testing ? Settings{} : loadSettings();
    if (!args.address.empty()) settings.address = args.address;
    if (!args.wheel.empty()) settings.wheelKey = args.wheel;
    if (args.demo && args.wheel.empty()) settings.wheelKey = "demo";
    if (!args.lang.empty()) settings.language = args.lang;
    Settings saved = settings;

    Ui ui;
    ui.scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (ui.scale <= 0) ui.scale = 1;
    const int width = int(460 * ui.scale);
    int height = int(760 * ui.scale);
    SDL_Window* window = SDL_CreateWindow("Geisterhand", width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Geisterhand", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nothing of the layout is worth remembering
    applyTheme(ui.scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    ui.fonts = loadFonts();
    ui.lang = settings.language == "de" ? Lang::De : settings.language == "en" ? Lang::En : systemLanguage();

    Telemetry telemetry;
    Telemetry::Options options;
    options.address = settings.address;
    options.receivePort = args.receivePort;
    options.sendPort = args.sendPort;
    options.demo = args.demo;
    telemetry.start(options);
    {
        Engine engine(telemetry);

        std::vector<WheelInfo> wheels;
        std::optional<WheelInfo> active;
        bool relist = true, advanced = false, shown = false, shotTaken = false, running = true;
        double lastList = -10, lastSave = 0, lastRecorded = -1;
        int frames = 0, recorded = 0;
        char address[64];
        SDL_strlcpy(address, settings.address.c_str(), sizeof address);

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_SPACE || event.key.key == SDLK_ESCAPE)) engine.stop();
                if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED) relist = true;
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(50);
                continue;
            }
            const double now = monotonicSeconds();
            SDL_UpdateJoysticks();  // notices wheels being plugged in and out

            // ---- devices: keep the list fresh, keep the choice across replugging ----------
            if (relist || now - lastList > 2.0) {
                relist = false;
                lastList = now;
                wheels = listWheels();
                if (settings.wheelKey.empty()) {  // first run: take the first wheel that can be turned
                    for (const WheelInfo& w : wheels) {
                        if (w.usable() && w.kind != WheelInfo::Kind::Demo) {
                            settings.wheelKey = w.key;
                            break;
                        }
                    }
                }
                std::optional<WheelInfo> wanted;
                for (const WheelInfo& w : wheels) {
                    if (w.key == settings.wheelKey && (!wanted || (w.usable() && !wanted->usable()))) wanted = w;
                }
                if (!sameDevice(wanted, active)) {
                    active = wanted;
                    engine.select(active);
                }
            }

            EngineSettings es;
            es.cap = settings.forcePercent / 100.0;
            es.rangeDeg = settings.rangeDeg;
            es.invert = settings.invert;
            es.factor = settings.factor;
            es.delayS = settings.bufferMs / 1000.0;
            es.stiffness = settings.stiffness;
            engine.configure(es);

            const TelemetryStatus link = telemetry.status();
            const EngineView view = engine.view();
            const bool engaged = view.state == EngineView::State::Probing || view.state == EngineView::State::Following ||
                                 view.state == EngineView::State::Waiting;
            const bool dataReady = link.link == TelemetryStatus::Link::Receiving;
            const bool canStart = view.state == EngineView::State::Ready && view.canTurn && dataReady;
            if (args.autostart && canStart && view.lastStop == EngineView::Stop::None && frames > 30) engine.start();

            // ---- the window --------------------------------------------------------------
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::PushFont(ui.fonts.regular, 15.0f);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("main", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // title and language
            ImGui::PushFont(ui.fonts.semibold, 26.0f);
            ImGui::TextUnformatted("Geisterhand");
            ImGui::PopFont();
            {
                const char* other = ui.lang == Lang::De ? "EN" : "DE";
                ImGui::PushFont(ui.fonts.semibold, 12.0f);
                const float w = ImGui::CalcTextSize(other).x + ImGui::GetStyle().FramePadding.x * 2;
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - w);
                ImGui::PushStyleColor(ImGuiCol_Text, vec(kMuted));
                if (ImGui::Button(other)) {
                    ui.lang = ui.lang == Lang::De ? Lang::En : Lang::De;
                    settings.language = ui.lang == Lang::De ? "de" : "en";
                }
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ui.px(6));
            mutedWrapped(ui.t(Tx::Subtitle));
            ImGui::Dummy(ImVec2(0, ui.px(4)));

            // PlayStation
            if (beginCard(ui, "playstation")) {
                label(ui, ui.t(Tx::PlayStation));
                switch (link.link) {
                case TelemetryStatus::Link::Receiving: {
                    const std::string text = std::string(ui.t(Tx::LinkReceiving)) + "  ·  " + link.address;
                    statusLine(ui, kGo, text.c_str());
                    break;
                }
                case TelemetryStatus::Link::NoSteering:
                    statusLine(ui, kWarn, ui.t(Tx::LinkNoSteering));
                    break;
                case TelemetryStatus::Link::PortBusy:
                    statusLine(ui, kStop, ui.t(Tx::LinkPortBusy));
                    break;
                case TelemetryStatus::Link::Silent:
                    statusLine(ui, kWarn, ui.t(Tx::LinkSilent));
                    break;
                case TelemetryStatus::Link::Searching:
                    statusLine(ui, kMuted, ui.t(Tx::LinkSearching));
                    if (now > 8.0) mutedWrapped(ui.t(Tx::LinkSearchingHint));
                    break;
                default:
                    statusLine(ui, kMuted, ui.t(Tx::LinkStarting));
                    break;
                }
                if (!args.demo) {
                    rowLabel(ui, ui.t(Tx::AddressLabel));
                    if (ImGui::InputTextWithHint("##address", ui.t(Tx::AddressHint), address, sizeof address, ImGuiInputTextFlags_CharsDecimal)) {
                        settings.address = address;
                        telemetry.setAddress(settings.address);
                    }
                }
            }
            endCard();

            // wheel
            if (beginCard(ui, "wheel")) {
                label(ui, ui.t(Tx::Wheel));
                const bool onlyDemo = wheels.size() <= 1;
                const std::string preview = active ? wheelLabel(ui, *active) : ui.t(onlyDemo ? Tx::WheelNone : Tx::WheelChoose);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::BeginDisabled(engaged);
                if (ImGui::BeginCombo("##wheel", preview.c_str())) {
                    for (size_t i = 0; i < wheels.size(); ++i) {
                        ImGui::PushID(int(i));
                        const bool chosen = active && sameDevice(active, wheels[i]);
                        if (ImGui::Selectable(wheelLabel(ui, wheels[i]).c_str(), chosen)) {
                            settings.wheelKey = wheels[i].key;
                            relist = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                if (active && active->kind == WheelInfo::Kind::NoForce) {
                    statusLine(ui, kWarn, ui.t(Tx::WheelNoForceHint));
                } else if (active && view.state == EngineView::State::NoWheel) {
                    const std::string text = std::string(ui.t(Tx::WheelOpenFailed)) + (view.detail.empty() ? "" : ": " + view.detail);
                    statusLine(ui, kStop, text.c_str());
                } else if (active) {
                    statusLine(ui, kGo, ui.t(Tx::WheelReady));
                }
            }
            endCard();

            // dial: the replay as a ghost, the real wheel in front of it
            {
                const float size = ui.px(212);
                ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, size + ui.px(18)));
                const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                const ImVec2 c((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
                const float r = size * 0.5f - ui.px(22);
                const bool haveWheel = view.state != EngineView::State::NoWheel;
                if (view.haveTarget) drawGhost(dl, c, r, ui.px(13), view.targetDeg, col(kGhost, 0.9f), ui.px(2), ui.px(5));
                drawWheel(dl, c, r, haveWheel ? view.wheelDeg : 0.0, col(kText, haveWheel ? 0.92f : 0.25f), ui.px(9), ui.px(7),
                          col(engaged ? kGo : kStop, haveWheel ? 1.0f : 0.3f), true);

                // what the motor is doing right now, as a bar from the middle
                const float half = ui.px(70), y = hi.y + ui.px(2);
                dl->AddLine(ImVec2(c.x - half, y), ImVec2(c.x + half, y), col(kLine), ui.px(3));
                if (engaged && es.cap > 0) {
                    const float part = float(std::clamp(view.force / es.cap, -1.0, 1.0));
                    dl->AddLine(ImVec2(c.x, y), ImVec2(c.x + half * part, y), col(kGhost), ui.px(3));
                }
                ImGui::Dummy(ImVec2(0, ui.px(2)));
            }

            // the two angles as numbers
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float x0 = ImGui::GetCursorScreenPos().x, y0 = ImGui::GetCursorScreenPos().y;
                const auto readout = [&](float centre, const char* caption, const std::string& value, unsigned color) {
                    ImGui::PushFont(ui.fonts.semibold, 11.5f);
                    const ImVec2 cs = ImGui::CalcTextSize(caption);
                    dl->AddText(ImVec2(centre - cs.x * 0.5f, y0), col(kMuted), caption);
                    ImGui::PopFont();
                    ImGui::PushFont(ui.fonts.semibold, 24.0f);
                    const ImVec2 vs = ImGui::CalcTextSize(value.c_str());
                    dl->AddText(ImVec2(centre - vs.x * 0.5f, y0 + cs.y + ui.px(2)), col(color), value.c_str());
                    ImGui::PopFont();
                };
                const bool haveWheel = view.state != EngineView::State::NoWheel;
                readout(x0 + avail * 0.27f, ui.t(Tx::Replay), view.haveTarget ? degreesText(view.targetDeg) : "–", view.haveTarget ? kGhost : kMuted);
                readout(x0 + avail * 0.73f, ui.t(Tx::WheelAngle), haveWheel ? degreesText(view.wheelDeg) : "–", haveWheel ? kText : kMuted);
                ImGui::Dummy(ImVec2(avail, ui.px(52)));
            }

            // settings
            rowLabel(ui, ui.t(Tx::Force));
            ImGui::SliderInt("##force", &settings.forcePercent, 5, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp);
            rowLabel(ui, ui.t(Tx::Range));
            ImGui::BeginDisabled(engaged);
            if (ImGui::InputInt("##range", &settings.rangeDeg, 10, 90)) settings.rangeDeg = std::clamp(settings.rangeDeg, 90, 2700);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", ui.t(Tx::RangeHint));

            ImGui::PushStyleColor(ImGuiCol_Text, vec(kMuted));
            ImGui::PushStyleColor(ImGuiCol_Button, vec(kBg));
            if (ImGui::Button((std::string(advanced ? "–  " : "+  ") + ui.t(Tx::Advanced)).c_str())) advanced = !advanced;
            ImGui::PopStyleColor(2);
            if (advanced) {
                ImGui::BeginDisabled(engaged);
                ImGui::Checkbox(ui.t(Tx::Invert), &settings.invert);
                ImGui::EndDisabled();
                rowLabel(ui, ui.t(Tx::Factor));
                ImGui::SliderFloat("##factor", &settings.factor, 0.25f, 3.0f, "%.2f ×", ImGuiSliderFlags_AlwaysClamp);
                rowLabel(ui, ui.t(Tx::Stiffness));
                ImGui::SliderFloat("##stiffness", &settings.stiffness, 0.3f, 2.0f, "%.1f ×", ImGuiSliderFlags_AlwaysClamp);
                rowLabel(ui, ui.t(Tx::Buffer));
                ImGui::SliderInt("##buffer", &settings.bufferMs, 0, 500, "%d ms", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::Dummy(ImVec2(0, ui.px(2)));

            // the one big button
            {
                const unsigned base = engaged ? kStop : kGo;
                ImGui::PushFont(ui.fonts.semibold, 17.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.px(12));
                ImGui::PushStyleColor(ImGuiCol_Button, vec(base));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(base, 0.88f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(base, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_Text, vec(kBg));
                ImGui::BeginDisabled(!engaged && !canStart);
                if (ImGui::Button(ui.t(engaged ? Tx::StopButton : Tx::Start), ImVec2(-FLT_MIN, ui.px(54)))) {
                    if (engaged) engine.stop();
                    else engine.start();
                }
                ImGui::EndDisabled();
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                ImGui::PopFont();
            }

            // one line that says what is going on, or why it stopped
            {
                std::string text;
                unsigned color = kMuted;
                if (view.state == EngineView::State::Probing) {
                    text = ui.t(Tx::HintProbing);
                } else if (view.state == EngineView::State::Following) {
                    text = ui.t(Tx::HintFollowing);
                } else if (view.state == EngineView::State::Waiting) {
                    text = ui.t(Tx::HintWaiting);
                    color = kWarn;
                } else if (view.lastStop == EngineView::Stop::Button) {
                    text = ui.t(Tx::StopButtonPressed);
                    color = kWarn;
                } else if (view.lastStop == EngineView::Stop::WheelLost) {
                    text = ui.t(Tx::StopWheelLost);
                    color = kWarn;
                } else if (view.lastStop == EngineView::Stop::NoData) {
                    text = ui.t(Tx::StopNoData);
                    color = kWarn;
                } else if (view.lastStop == EngineView::Stop::Driver) {
                    text = std::string(ui.t(Tx::StopDriver)) + (view.detail.empty() ? "." : ": " + view.detail);
                    color = kStop;
                } else if (view.lastStop == EngineView::Stop::Fault) {
                    const Tx why = view.lastFault == Fault::NoResponse ? Tx::FaultNoResponse
                                   : view.lastFault == Fault::Runaway  ? Tx::FaultRunaway
                                   : view.lastFault == Fault::TooFast  ? Tx::FaultTooFast
                                   : view.lastFault == Fault::Blocked  ? Tx::FaultBlocked
                                                                       : Tx::FaultOscillation;
                    text = ui.t(why);
                    color = kStop;
                } else if (!view.canTurn) {
                    text = ui.t(wheels.size() <= 1 ? Tx::HintPlugIn : Tx::HintNeedWheel);
                } else if (!dataReady) {
                    text = ui.t(Tx::HintNeedData);
                } else {
                    text = ui.t(Tx::HintReady);
                }
                ImGui::BeginChild("hint", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 3), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
                ImGui::PushStyleColor(ImGuiCol_Text, vec(color));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::EndChild();
            }

            // The window is as tall as its content.
            const float wantHeight = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y - ImGui::GetStyle().ItemSpacing.y;
            ImGui::End();
            ImGui::PopFont();
            ImGui::Render();

            SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, (kBg >> 16) & 0xFF, (kBg >> 8) & 0xFF, kBg & 0xFF, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            if (!args.shot.empty() && !shotTaken && now >= args.shotAfter) {
                if (SDL_Surface* picture = SDL_RenderReadPixels(renderer, nullptr)) {
                    SDL_SavePNG(picture, args.shot.c_str());
                    SDL_DestroySurface(picture);
                }
                shotTaken = true;
            }
            if (!args.record.empty() && now >= args.shotAfter && now < args.shotAfter + args.recordSeconds && now - lastRecorded >= 0.05) {
                if (SDL_Surface* picture = SDL_RenderReadPixels(renderer, nullptr)) {
                    char name[32];
                    std::snprintf(name, sizeof name, "/frame_%04d.png", recorded++);
                    SDL_SavePNG(picture, (args.record + name).c_str());
                    SDL_DestroySurface(picture);
                }
                lastRecorded = lastRecorded < 0 ? now : lastRecorded + 0.05;
            }
            SDL_RenderPresent(renderer);

            ++frames;
            if (frames >= 2 && std::fabs(wantHeight - float(height)) > 1.5f) {
                height = int(std::lround(wantHeight));
                SDL_SetWindowSize(window, width, height);
            }
            if (!shown && frames >= 3) {
                SDL_ShowWindow(window);
                shown = true;
            }

            if (!testing && !(settings == saved) && now - lastSave > 0.5) {
                saveSettings(settings);
                saved = settings;
                lastSave = now;
            }
            if (args.quitAfter > 0 && now >= args.quitAfter) running = false;
        }
        engine.stop();
    }  // the engine lets go of the wheel here, before anything else shuts down
    telemetry.stop();
    if (!testing && !(settings == saved)) saveSettings(settings);

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_hid_exit();
    SDL_Quit();
    return 0;
}
