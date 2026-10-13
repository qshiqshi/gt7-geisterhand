// One telemetry packet of Gran Turismo 7: decryption and the few fields this tool needs.
// Layout after MacManley/gt7-udp and Nenkai/PDTools; the steering wheel angle only exists
// in the packets the game sends for heartbeat "B" and later (316 bytes and more).
#pragma once
#include <cstddef>
#include <cstdint>

struct Gt7Frame {
    int32_t packetId = 0;      // counts up by one per packet, 60 a second
    uint16_t flags = 0;
    float speedMps = 0;
    bool hasSteering = false;  // false for the short packet (heartbeat "A")
    float steeringRad = 0;     // steering wheel angle, positive = turned left
    size_t size = 0;

    bool onTrack() const { return flags & 0x0001; }
    bool paused() const { return flags & 0x0002; }
    bool loading() const { return flags & 0x0004; }
};

constexpr size_t kGt7MinPacket = 296;
constexpr size_t kGt7MaxPacket = 368;

// False when the datagram is not a GT7 packet (wrong size or magic).
bool gt7Decode(const uint8_t* data, size_t len, Gt7Frame& out);
