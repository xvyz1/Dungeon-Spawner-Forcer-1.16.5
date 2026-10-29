#pragma once
#include <stdint.h>

#define JR_MASK ((1ULL << 48) - 1)

typedef struct { uint64_t s; } JRand;

static inline void jr_set(JRand* r, uint64_t seed) { r->s = (seed ^ 0x5DEECE66DULL) & JR_MASK; }

static inline int jr_next(JRand* r, int bits) {
    r->s = (r->s * 0x5DEECE66DULL + 0xBULL) & JR_MASK;
    return (int) (uint32_t) (r->s >> (48 - bits));
}

static inline int jr_int(JRand* r, int bound) {
    if ((bound & -bound) == bound) return (int) (((int64_t) bound * (int64_t) jr_next(r, 31)) >> 31);
    int bits, val;
    do {
        bits = jr_next(r, 31);
        val = bits % bound;
    } while ((int) ((uint32_t) bits - (uint32_t) val + (uint32_t) (bound - 1)) < 0);
    return val;
}

static inline int64_t jr_long(JRand* r) {
    int64_t a = jr_next(r, 32);
    int64_t b = jr_next(r, 32);
    return (int64_t) (((uint64_t) a << 32) + (uint64_t) b);
}

static inline void jr_chunk(JRand* r, uint64_t seed, int cx, int cz) {
    jr_set(r, seed);
    int64_t a = jr_long(r) | 1, b = jr_long(r) | 1;
    jr_set(r, (uint64_t) (((uint64_t) a * (uint64_t) (int64_t) (cx * 16) + (uint64_t) b * (uint64_t) (int64_t) (cz * 16)) ^ seed) + 30002ULL);
}

static inline int jr_mob(JRand* r) {
    static const int mobs[4] = {0, 1, 1, 2};
    return mobs[jr_int(r, 4)];
}

static inline double jr_double(JRand* r) {
    int64_t a = jr_next(r, 26);
    int64_t b = jr_next(r, 27);
    return (double) ((a << 27) + b) * 1.1102230246251565E-16;
}
