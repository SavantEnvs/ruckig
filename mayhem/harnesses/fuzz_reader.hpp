// Shared byte decoder for the ruckig fuzz harnesses.
//
// Ruckig has no file format: its untrusted input is the numeric InputParameter
// (kinematic state, limits, options). Raw 8-byte doubles are almost always rejected
// by InputParameter::validate, so each number is encoded compactly:
//   tag byte k with the high bit set  -> the next 8 bytes, as a raw IEEE double
//   k == 0x7e / 0x7f                  -> +inf / -inf
//   otherwise                         -> (k - 63) / 8.0   (small values in [-7.875, 7.75])
// Running out of bytes yields 0 / false, so every input is decodable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

struct FuzzReader {
    const uint8_t* data;
    size_t size;

    uint8_t byte() {
        if (size == 0) {
            return 0;
        }
        --size;
        return *data++;
    }

    bool flag() { return byte() & 1; }

    double number() {
        const uint8_t k = byte();
        if (k & 0x80) {
            double v = 0.0;
            if (size >= sizeof(double)) {
                std::memcpy(&v, data, sizeof(double));
                data += sizeof(double);
                size -= sizeof(double);
            }
            return v;
        }
        if (k == 0x7e) {
            return std::numeric_limits<double>::infinity();
        }
        if (k == 0x7f) {
            return -std::numeric_limits<double>::infinity();
        }
        return (static_cast<int>(k) - 63) / 8.0;
    }
};
