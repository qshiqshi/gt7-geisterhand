#include "gt7_packet.h"

#include <cmath>
#include <cstring>

#include "salsa20.h"

namespace {

const char kKeyText[] = "Simulator Interface Packet GT7 ver 0.0";  // the first 32 bytes are the key
constexpr uint32_t kMagic = 0x47375330;                            // "0S7G"
constexpr uint32_t kIvA = 0xDEADBEAF;                              // packet A, 296 bytes
constexpr uint32_t kIvB = 0xDEADBEEF;                              // packets B (316) and C (368)
constexpr uint32_t kIvTilde = 0x55FABB4F;                          // packet "~", 344 bytes

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

float leFloat(const uint8_t* p) {
    const uint32_t bits = le32(p);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

bool gt7Decode(const uint8_t* data, size_t len, Gt7Frame& out) {
    if (len < kGt7MinPacket || len > kGt7MaxPacket) return false;

    // The constant mixed into the nonce depends on the packet format, which shows in the
    // length. Try the matching one first and the others after it.
    const uint32_t first = len == 296 ? kIvA : len == 344 ? kIvTilde : kIvB;
    const uint32_t candidates[4] = {first, kIvA, kIvB, kIvTilde};
    const uint32_t iv1 = le32(data + 0x40);

    uint8_t key[32];
    std::memcpy(key, kKeyText, sizeof key);
    uint8_t plain[kGt7MaxPacket];
    bool ok = false;
    for (int i = 0; i < 4 && !ok; ++i) {
        if (i > 0 && candidates[i] == first) continue;
        const uint32_t iv2 = iv1 ^ candidates[i];
        const uint8_t nonce[8] = {uint8_t(iv2),       uint8_t(iv2 >> 8), uint8_t(iv2 >> 16), uint8_t(iv2 >> 24),
                                  uint8_t(iv1),       uint8_t(iv1 >> 8), uint8_t(iv1 >> 16), uint8_t(iv1 >> 24)};
        salsa20Xor(key, nonce, data, plain, len);
        ok = le32(plain) == kMagic;
    }
    if (!ok) return false;

    out = Gt7Frame{};
    out.size = len;
    out.packetId = int32_t(le32(plain + 0x70));
    out.flags = uint16_t(plain[0x8E] | plain[0x8F] << 8);
    out.speedMps = leFloat(plain + 0x4C);
    if (len >= 316) {
        const float steer = leFloat(plain + 0x128);
        if (std::isfinite(steer)) {
            out.hasSteering = true;
            out.steeringRad = steer;
        }
    }
    return true;
}
