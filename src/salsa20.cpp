#include "salsa20.h"

#include <cstring>

namespace {

inline uint32_t rotl(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

inline uint32_t load32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

inline void store32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

void block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    std::memcpy(x, in, sizeof x);
    for (int round = 0; round < 20; round += 2) {
        // columns
        x[4] ^= rotl(x[0] + x[12], 7);
        x[8] ^= rotl(x[4] + x[0], 9);
        x[12] ^= rotl(x[8] + x[4], 13);
        x[0] ^= rotl(x[12] + x[8], 18);
        x[9] ^= rotl(x[5] + x[1], 7);
        x[13] ^= rotl(x[9] + x[5], 9);
        x[1] ^= rotl(x[13] + x[9], 13);
        x[5] ^= rotl(x[1] + x[13], 18);
        x[14] ^= rotl(x[10] + x[6], 7);
        x[2] ^= rotl(x[14] + x[10], 9);
        x[6] ^= rotl(x[2] + x[14], 13);
        x[10] ^= rotl(x[6] + x[2], 18);
        x[3] ^= rotl(x[15] + x[11], 7);
        x[7] ^= rotl(x[3] + x[15], 9);
        x[11] ^= rotl(x[7] + x[3], 13);
        x[15] ^= rotl(x[11] + x[7], 18);
        // rows
        x[1] ^= rotl(x[0] + x[3], 7);
        x[2] ^= rotl(x[1] + x[0], 9);
        x[3] ^= rotl(x[2] + x[1], 13);
        x[0] ^= rotl(x[3] + x[2], 18);
        x[6] ^= rotl(x[5] + x[4], 7);
        x[7] ^= rotl(x[6] + x[5], 9);
        x[4] ^= rotl(x[7] + x[6], 13);
        x[5] ^= rotl(x[4] + x[7], 18);
        x[11] ^= rotl(x[10] + x[9], 7);
        x[8] ^= rotl(x[11] + x[10], 9);
        x[9] ^= rotl(x[8] + x[11], 13);
        x[10] ^= rotl(x[9] + x[8], 18);
        x[12] ^= rotl(x[15] + x[14], 7);
        x[13] ^= rotl(x[12] + x[15], 9);
        x[14] ^= rotl(x[13] + x[12], 13);
        x[15] ^= rotl(x[14] + x[13], 18);
    }
    for (int i = 0; i < 16; ++i) store32(out + 4 * i, x[i] + in[i]);
}

}  // namespace

void salsa20Xor(const uint8_t key[32], const uint8_t nonce[8], const uint8_t* in, uint8_t* out, size_t len) {
    uint32_t state[16];
    state[0] = 0x61707865;  // "expand 32-byte k"
    state[5] = 0x3320646e;
    state[10] = 0x79622d32;
    state[15] = 0x6b206574;
    for (int i = 0; i < 4; ++i) {
        state[1 + i] = load32(key + 4 * i);
        state[11 + i] = load32(key + 16 + 4 * i);
    }
    state[6] = load32(nonce);
    state[7] = load32(nonce + 4);
    state[8] = 0;
    state[9] = 0;

    uint8_t stream[64];
    while (len > 0) {
        block(state, stream);
        if (++state[8] == 0) ++state[9];
        const size_t n = len < 64 ? len : 64;
        for (size_t i = 0; i < n; ++i) out[i] = in[i] ^ stream[i];
        in += n;
        out += n;
        len -= n;
    }
}
