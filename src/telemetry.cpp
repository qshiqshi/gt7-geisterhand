#include "telemetry.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>

#include "clock.h"
#include "demo_signal.h"
#include "gt7_packet.h"
#include "net.h"

namespace {

constexpr char kHeartbeat = 'B';         // the smallest packet that carries the steering angle
constexpr double kHeartbeatEveryS = 1.0;
constexpr double kSweepEveryS = 4.0;     // while nothing arrives: ask the whole network again
constexpr double kSweepAfterS = 2.5;
constexpr double kFlowingS = 1.0;        // a packet within this time counts as "receiving"
constexpr double kRetryPortS = 2.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

void sendBeat(socket_t s, uint32_t ipHostOrder, int port) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(uint16_t(port));
    to.sin_addr.s_addr = htonl(ipHostOrder);
    sendto(s, &kHeartbeat, 1, 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
}

bool parseAddress(const std::string& text, uint32_t& ipHostOrder) {
    in_addr a{};
    if (inet_pton(AF_INET, text.c_str(), &a) != 1) return false;
    ipHostOrder = ntohl(a.s_addr);
    return true;
}

std::string formatAddress(uint32_t ipHostOrder) {
    char text[32];
    std::snprintf(text, sizeof text, "%u.%u.%u.%u", ipHostOrder >> 24, (ipHostOrder >> 16) & 0xFF, (ipHostOrder >> 8) & 0xFF,
                  ipHostOrder & 0xFF);
    return text;
}

// Every other address in the own networks (a /24 around each own address), plus broadcast.
void sweep(socket_t s, int port) {
    sendBeat(s, 0xFFFFFFFFu, port);
    for (uint32_t own : netLocalAddresses()) {
        const uint32_t base = own & 0xFFFFFF00u;
        for (uint32_t host = 1; host < 255; ++host) {
            if ((base | host) != own) sendBeat(s, base | host, port);
        }
    }
}

socket_t openSocket(int port) {
    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSocket) return kNoSocket;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(uint16_t(port));
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
        netClose(s);
        return kNoSocket;
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
    netSetReceiveTimeout(s, 100);
    netQuietUdp(s);
    return s;
}

void nap(int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }

}  // namespace

Telemetry::~Telemetry() { stop(); }

void Telemetry::start(const Options& options) {
    stop();
    options_ = options;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = options.address;
        wantedChanged_ = false;
        status_ = TelemetryStatus{};
        playout_.reset();
    }
    running_ = true;
    thread_ = std::thread([this] { options_.demo ? runDemo() : run(); });
}

void Telemetry::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void Telemetry::setAddress(const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (address == wanted_) return;
    wanted_ = address;
    wantedChanged_ = true;
}

TelemetryStatus Telemetry::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool Telemetry::steering(double now, double delay, double& degLeft, double& late) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playout_.sample(now, delay, degLeft, late);
}

void Telemetry::run() {
    netStartup();
    socket_t s = kNoSocket;
    double lastPortTry = -1e9, lastBeat = -1e9, lastSweep = -1e9, lastPacket = -1e9;
    bool everReceived = false, lastHadSteering = false;
    uint32_t learned = 0;  // where packets came from last
    std::deque<double> arrivals;

    while (running_) {
        const double t = monotonicSeconds();

        if (s == kNoSocket) {
            if (t - lastPortTry < kRetryPortS) {
                nap(50);
                continue;
            }
            lastPortTry = t;
            s = openSocket(options_.receivePort);
            std::lock_guard<std::mutex> lock(mutex_);
            status_.link = s == kNoSocket ? TelemetryStatus::Link::PortBusy : TelemetryStatus::Link::Searching;
            if (s == kNoSocket) continue;
        }

        std::string wanted;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wanted = wanted_;
            if (wantedChanged_) {  // another console: forget the old one
                wantedChanged_ = false;
                learned = 0;
                lastBeat = lastSweep = -1e9;
            }
        }
        uint32_t fixed = 0;
        const bool haveFixed = parseAddress(wanted, fixed);
        const uint32_t target = haveFixed ? fixed : learned;

        if (target != 0 && t - lastBeat >= kHeartbeatEveryS) {
            sendBeat(s, target, options_.sendPort);
            lastBeat = t;
        }
        if (!haveFixed && t - lastPacket > kSweepAfterS && t - lastSweep >= kSweepEveryS) {
            sweep(s, options_.sendPort);
            lastSweep = t;
        }

        unsigned char datagram[1024];
        sockaddr_in from{};
        socklen_t fromSize = sizeof from;
        const int n = int(recvfrom(s, reinterpret_cast<char*>(datagram), sizeof datagram, 0, reinterpret_cast<sockaddr*>(&from), &fromSize));
        const double arrival = monotonicSeconds();

        Gt7Frame frame;
        const bool packet = n > 0 && gt7Decode(datagram, size_t(n), frame);
        if (packet) {
            learned = ntohl(from.sin_addr.s_addr);
            lastPacket = arrival;
            everReceived = true;
            lastHadSteering = frame.hasSteering;
            arrivals.push_back(arrival);
        }
        while (!arrivals.empty() && arrival - arrivals.front() > 1.0) arrivals.pop_front();

        std::lock_guard<std::mutex> lock(mutex_);
        if (packet) {
            if (frame.hasSteering) playout_.push(frame.packetId, double(frame.steeringRad) * kRadToDeg, arrival);
            status_.address = formatAddress(learned);
            status_.speedKmh = double(frame.speedMps) * 3.6;
            status_.paused = frame.paused();
        }
        status_.packetsPerSecond = double(arrivals.size());
        const bool flowing = arrival - lastPacket < kFlowingS;
        status_.link = flowing ? (lastHadSteering ? TelemetryStatus::Link::Receiving : TelemetryStatus::Link::NoSteering)
                               : (everReceived ? TelemetryStatus::Link::Silent : TelemetryStatus::Link::Searching);
    }
    if (s != kNoSocket) netClose(s);
}

// The made-up drive, delivered like packets of the game: 60 a second with a running number.
void Telemetry::runDemo() {
    const double begin = monotonicSeconds();
    int32_t id = 1000;
    while (running_) {
        const double due = begin + (id - 1000) * Playout::kFrame;
        const double wait = due - monotonicSeconds();
        if (wait > 0) nap(int(std::ceil(wait * 1000)));
        const double t = monotonicSeconds();
        std::lock_guard<std::mutex> lock(mutex_);
        playout_.push(id, -demoSteeringDeg(due - begin), t);  // the game counts left as positive
        status_.link = TelemetryStatus::Link::Receiving;
        status_.address = "Demo";
        status_.packetsPerSecond = 60;
        status_.speedKmh = 120 + 60 * std::sin((due - begin) * 0.3);
        ++id;
    }
}
