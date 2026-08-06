/*
 * prng.c — xoshiro256** PRNG seeded by splitmix64.
 *
 * Both algorithms are by Blackman & Vigna, released to the public domain
 * (https://prng.di.unimi.it/). Chosen for speed, quality, and — critically for
 * reproducible experiment designs — fully deterministic, platform-independent
 * output from a single 64-bit seed.
 */

#include "doe.h"

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void doe_rng_seed(doe_rng_t *rng, uint64_t seed) {
    uint64_t x = seed;
    for (int i = 0; i < 4; i++) {
        rng->s[i] = splitmix64(&x);
    }
}

static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t doe_rng_next(doe_rng_t *rng) {
    uint64_t *s = rng->s;
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);

    return result;
}

double doe_rng_uniform(doe_rng_t *rng) {
    /* Use the top 53 bits to build a uniform double in [0, 1). */
    return (double)(doe_rng_next(rng) >> 11) * 0x1.0p-53;
}
