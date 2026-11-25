// Chopagotchi - the game's name hash (Jenkins one-at-a-time, case-insensitive),
// used for model, stat and script names. No game or Windows calls.
#pragma once
#include <cstdint>

inline uint32_t joaat(const char* s)
{
    uint32_t h = 0;
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c += 32;
        h += (uint8_t)c;
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}
