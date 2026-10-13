// Salsa20/20 stream cipher (D. J. Bernstein): 256-bit key, 8-byte nonce, block counter from zero.
// GT7 encrypts every telemetry packet with it.
#pragma once
#include <cstddef>
#include <cstdint>

void salsa20Xor(const uint8_t key[32], const uint8_t nonce[8], const uint8_t* in, uint8_t* out, size_t len);
